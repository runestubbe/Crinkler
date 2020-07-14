;; -*- tab-width: 4 -*-
bits		32
extern	_PackedData
extern	_Models
extern	_HashTable
extern	_UnpackedData
extern	_HashTableSize
extern	_VirtualSize
extern	_ImageBase

global	_header
global	_DepackEntry
global	_WriteBit
global	_ClearHash
global  _LinkerVersionPtr
global	_SubsystemTypePtr
global	_ModelSkipPtr
global	_BaseProbPtr
global	_SpareNopPtr
global	_CharacteristicsPtr
global	_SaturatePtr
global	_SaturateAdjust1Ptr
global	_SaturateAdjust2Ptr

HASH_MULTIPLIER	equ 111

BaseProbDummy	equ	13
ModelSkipDummy	equ	23

section header	align=1

; Initialization state:
; ESI = _Models
; ECX = 0
; EAX = 1
; EBP = 0
; EDI = 0
; EBX > 1
; [ESP] = _UnpackedData
_header:
;dos header
db 'M','Z'
DepackInit:
    mov  edi,_UnpackedData  ;5
    push edi                ;1
    xor  eax,eax            ;2
    jmp  short DepackInit2  ;2

	
; COFF header
db 'P', 'E', 0, 0	;PE signature
dw 014Ch			;Machine, 386+
dw 0h				;Number of sections
db "HASH"			;Timestamp
db "HASH"			;Symbol table pointer
db "HASH"			;Number of symbols
dw 8h				;Size of optional header
_CharacteristicsPtr:
dw 2h				; Characteristics (almost any allowed - bit 1 must be set, bit 13 must be clear)

;optional header (PE-header)
dw 010Bh			;Magic (Image file)
_LinkerVersionPtr:
dw 0h				;Major/Minor linker version
db "HASH"			;Size of code
db "HASH"			;Size of initialized data
db "HASH"			;Size of uninitialized data
dd EntryPoint-_header
db "HASH"			;Base of code
dd 0000000Ch		;Base of data (and PE header offset)
dd _ImageBase		;Image base
dd 4h				;Section alignment (in memory)
dd 4h				;File alignment (on disk)
db "HASH"			;Major/minor OS version
db "HASH"			;Major/minor image version
dw 4				;Major subsystem version
EntryPoint:
jmp short DepackInit;Minor subsystem version
dd 0h				;Reserved
dd _VirtualSize+20000h;Size of image (= Section size + Section alignment)
dd 64				;Size of headers (= Section alignment)
	;Checksum
DepackInit2:
_SpareNopPtr:
	nop ;db 0xCC
	push eax
	inc  eax

	db		0xbb ; mov ebx, const
_SubsystemTypePtr:
	dw		0002h	;Subsystem
	dw		0h		;DLL characteristics
	;Size of stack reserve
	;Size of stack commit
	;Size of heap reserve
	;Size of heap commit
	pop		ebp
	mov		esi, _Models
	push	byte 0
	pop		ecx
	jmp		_DepackEntry
	dw		0

LoaderFlags:
db "HASH"			;Loader flags
dd 0				;Number of RVAs and Sizes

;Data directories
	dd 0				;Exports RVA
	;Exports size
	;Imports RVA
AritDecode2:
	xchg eax, edx		;eax = interval_size, edx = threshold	;1
	sub	eax, edx		;eax = interval_size - threshold		;2
.zero:
	sbb	ebx, ebx		;ebx = -cf = -bit						;2
	ret															;1
	dw 0004h

db "HASH"						;Imports size

;Resources RVA
AritDecodeLoop:
	bt	[_PackedData], ebp		;test bit		;7
	adc	ecx, ecx				;shift bit in
	inc	ebp						;next bit
	add	eax, eax				;shift interval
AritDecode:
	test eax, eax			;msb of interval != 0
	jns	AritDecodeLoop		;loop while msb of interval == 0

	add	ebx, edx		;ebx = p0 + p1								;2
	push eax			;push interval_size							;1
	mul	edx				;edx:eax = p0 * interval_size				;2
	div	ebx				;eax = (p0 * interval_size) / (p0 + p1)		;2
	;; eax = threshold value between 0 and 1
	pop	edx				;edx = interval_size		;1
	cmp	ecx, eax		;data < threshold?			;2
	jb	AritDecode2.zero										;2
	
	;one
	sub	ecx, eax		;data -= threshold			;2
	jmp short AritDecode2
db "HASH"
dd 0				;debugtable (b8-bc) must be 0
times	1000	db "HASH"

zero_offset	equ	20
one_offset	equ	16
section depacker align=1
	;; ebp = source bit index
	;; edi = dest pointer
	;; ecx = data
	;; eax = interval size
	;; edx = zero prob
	;; ebx = one prob

AritDecodeCallPad:
	call	AritDecode

_DepackEntry:

EndCheck:
	pusha

    lodsd
    add  eax,edi
    je   InitHash            ;if (block_end == unpacked_byte_offset) InitHash
    ; carry = 1

Model:
	;; Find probs from model
	;; If ebx is 0 or 1,
	;; update model with bit value in ebx

    push byte BaseProbDummy
BaseProbPtrP1:
    pop  edx
    mov  [esp+zero_offset],edx
    mov  [esp+one_offset],edx   ;carry flag always clear here, could be useful

	;; Init weight
	lodsd				; model weight shift mask
	xor	ebp, ebp		; weight = 0

ModelLoop:
	dec	ebp
IncreaseWeight:
	inc	ebp				;weight++
	add	eax, eax		;check next bit in model weight mask
	jc	IncreaseWeight
	jnz	NotModelEnd		;

    add ebx, ebx		;2
    popa				;1
    jg  AritDecodeCallPad   ;no need for UnpackedData: it will already be in edi
  WriteBit:
    rcl  byte[edi],1  ;shift the decoded bit in
    jnc  _DepackEntry ;finished the byte?
    inc  edi
    jmp  short WriteBit ;yes: new byte = 1

NotModelEnd:
	pusha
	lodsb			; model mask
	mov	dl, al	; edx = mask

.hashloop:
	xor	al, [edi]
	imul eax, byte HASH_MULTIPLIER
	add	al, [edi]
	dec	eax
.next:
	dec	edi			; next byte
	add	dl, dl		; hash byte?
	jc	.hashloop
	jnz	.next
	
	;stc
InitHash:
	mov	edi, _HashTable
	mov	ecx, _HashTableSize
	jnc	short UpdateHash

_ClearHash:
	rep stosw
	or	al, [esi]
	popa
	lea	esi, [esi + ModelSkipDummy]
ModelSkipPtrP1:
	jpo	EndCheck
	ret

UpdateHash:
	div	ecx
	;; edx = hash
	lea	edi, [edi + edx*2]	;edi = hashTableEntry

	;; Calculate weight
	mov	ecx, ebp	;ecx = weight
	xor	eax, eax	;eax = 0
	scasb
	je	.boost
	or	ch, [edi]
	jne	.notboost
.boost:
	inc	ecx
	inc	ecx
.notboost:

	;; Add probs
.bits:
	movzx	edx, byte [edi + eax]
	shl	edx, cl
	add	[esp + 8*4 + zero_offset + eax*4], edx
	dec	eax
	jp .bits

	test ebx, ebx
	jg	SkipUpdate
SaturateAdjust1PtrP1:
	
	;half if > 1
	shr	byte [edi + ebx], 1
	jnz	short .nz
	rcl	byte [edi + ebx], 1
.nz:
	;inc correct bit
	not ebx
	inc	byte [edi + ebx]
_SaturatePtr:
;	jnz	.nowrap
;	dec	byte [edi + ebx]
;.nowrap:

SkipUpdate:
	popa
	inc	esi		; Next model
	jmp	short ModelLoop
SaturateAdjust2PtrP1:

_ModelSkipPtr	equ	ModelSkipPtrP1-1
_BaseProbPtr	equ	BaseProbPtrP1-1
_SaturateAdjust1Ptr	equ SaturateAdjust1PtrP1-1
_SaturateAdjust2Ptr	equ SaturateAdjust2PtrP1-1
