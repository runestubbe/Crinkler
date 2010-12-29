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

BaseProbDummy	equ	13
ModelSkipDummy	equ	23

%define HeaderRVA(l) 10000h+(l-_header)

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

	
;coff header
db 'P', 'E', 0, 0	;PE signature
dw 014Ch			;Machine, 386+
dw 01h				;Number of sections
db "HASH"			;Timestamp
db "HASH"			;Symbol table pointer
db "HASH"			;Number of symbols
dw 0078h			;Size of optional header
dw 030Fh			;Characteristics (32bit, relocs stripped,
					;					executable image + everything stripped)

;optional header (PE-header)
dw 010Bh			;Magic (Image file)
_LinkerVersionPtr:
dw 0h				;Major/Minor linker version
db "HASH"			;Size of code
db "HASH"			;Size of initialized data
db "HASH"			;Size of uninitialized data
dd HeaderRVA(DepackInit);Address of entry point
db "HASH"			;Base of code
dd 0000000Ch		;Base of data (and PE header offset)
dd _ImageBase		;Image base
dd 00010000h		;Section alignment (in memory)
dd 00000200h		;File alignment (on disk)
db "HASH"			;Major/minor OS version
db "HASH"			;Major/minor image version

dw 4,0			;Major/minor subsystem version
dd 0h				;Reserved
dd _VirtualSize+20000h;Size of image (= Section size + Section alignment)
dd 00010000h		;Size of headers (= Section alignment)

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
dd 3				;Number of RVAs and Sizes

;Data directories
;Exports RVA
;Exports size
;Imports RVA
_AritCalculate:
	add	ebx, edx		;ebx = p0 + p1
	push	eax			;push interval_size
	mul	edx			;edx:eax = p0 * interval_size
	div	ebx			;eax = (p0 * interval_size) / (p0 + p1)
	;; eax = threshold value between 0 and 1

	xor	ebx, ebx		;ebx = 0
	jmp	short _AritCalculate2
	db 0

;db "HASH"						;Exports RVA
;db "HASH"						;Exports size
;dd 0x0001F000					;Imports rva
db "HASH"						;Imports size
dd 0							;Resources RVA

;Section headers
;Name
;db "lz32.dll"

_AritCalculate2:
	pop	edx			;edx = interval_size
	cmp	ecx, eax		;data < threshold?
	jb	.zero
	
	;one
	sub	ecx, eax		;data -= threshold
	
	xchg	eax, edx		;eax = interval_size, edx = threshold
	sub	eax, edx		;eax = interval_size - threshold
	inc	ebx			;ebx = 1
.zero:
	;; ebx = bit
	;; ecx = new data
	;; eax = new interval size
	ret
		
dd _VirtualSize+10000h;Virtual size (= Section size)
dd 00010000h		;Virtual address (= Section alignment)
dd 200h				;Size of raw data
dd 1h				;Pointer to raw data
db "HASH"			;Pointer to relocations
dd 0h				;Pointer to line numbers
db "HASH"			;Number of relocations/line numbers
dd 0E00000E0h		;Characteristics (contains everything, is executable, readable and writable)

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

AritDecodeLoop:
	bt	[_PackedData], ebp	;test bit
	adc	ecx, ecx		;shift bit in
	inc	ebp			;next bit
	add	eax, eax		;shift interval
AritDecode:
	test	eax, eax		;msb of interval != 0
	jns	AritDecodeLoop		;loop while msb of interval == 0
	call	_AritCalculate

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
    pop  eax
    mov  [esp+zero_offset],eax
    mov  [esp+one_offset],eax   ;carry flag always clear here, could be useful

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

    shr  ebx,1        ;ebx=0 or 1 (decoded bit): carry flag = 0,1
    popa              ;ebx>1      (one count):   jnz to AritDecode
    jnz  AritDecode   ;no need for UnpackedData: it will already be in edi
  WriteBit:
    rcl  byte[edi],1  ;shift the decoded bit in
    jnc  _DepackEntry ;finished the byte?
    inc  edi
    jmp  short WriteBit ;yes: new byte = 1

NotModelEnd:
	pusha
	lodsb			; model mask
	movzx	edx, al	; edx = mask

.hashloop:
	xor	al, [edi]
	rol	eax, 9
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
	dec	eax
.bits:
	movzx	edx, byte [edi + eax]
	shl	edx, cl
	add	[esp + 8*4 + zero_offset + eax*4], edx
	inc	eax
	jle	.bits

	sub	eax, ebx
	js	.noupdate
	
	;; ebx = correct bit
	;; eax = reverse bit
	dec	edi
	inc	byte [edi + eax]
	
	;half if > 1
	shr	byte [edi + ebx], 1
	jnz	.noupdate
	rcl	byte [edi + ebx], 1
.noupdate:
	popa
	inc	esi		; Next model
	jmp	short ModelLoop

_ModelSkipPtr	equ	ModelSkipPtrP1-1
_BaseProbPtr	equ	BaseProbPtrP1-1
