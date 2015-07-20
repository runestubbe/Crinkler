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

BaseProbDummy	equ	10
ModelSkipDummy	equ	23

zero_offset	equ	20
one_offset	equ	16
	
section header	align=1
_header:
;dos header
db 'M','Z'
_LinkerVersionPtr:
dw 0

;coff header
db 'P', 'E', 0, 0	;PE signature
dw 014Ch			;Machine, 386+
dw 0h				;Number of sections
;12 bytes
;db "HAS2"			;Timestamp
;db "HAS2"			;Symbol table pointer
;db "HAS2"			;Number of symbols
ModelEnd:
	add ebx, ebx															;2
    popa              ;ebx>1      (one count):   jnz to AritDecode			;1
    jg  short AritDecode   ;no need for UnpackedData: it will already be in edi	;2
  WriteBit:
    rcl  byte[edi],1  ;shift the decoded bit in		;2
    jnc  short _DepackEntry ;finished the byte?		;2
    inc  edi										;1
    jmp  short WriteBit ;yes: new byte = 1			;2
dw 8h				; Size of optional header
_CharacteristicsPtr:
dw 2h				; Characteristics (almost any allowed - bit 1 must be set, bit 13 must be clear)

;optional header (PE-header)
dw 010Bh			;Magic (Image file)
;14bytes + entry point + 8bytes
;db "HA"				;Major/Minor linker version
;db "HAS2"			;Size of code
;db "HAS2"			;Size of initialized data
;db "HAS2"			;Size of uninitialized data
AritDecodeLoop2:
	adc	ecx, ecx				;shift bit in							;2
	inc	ebp						;next bit								;1
AritDecode:
	test eax, eax				;test sign								;2
	jns	short AritDecodeLoop	;loop while msb of interval == 0		;2
	add	ebx, edx				;ebx = p0 + p1							;2
	push eax					;push interval_size						;1
	mul	edx						;edx:eax = p0 * interval_size			;2
	nop																	;1
	db 0x3D						;cmp eax, DepackInit-_header
	dd DepackInit-_header

;db "HAS2"			;Base of code
;db "HAS2"			;Base of data	
	div	ebx						;eax = (p0 * interval_size) / (p0 + p1)	;2
	; eax = threshold value between 0 and 1
	cmp	ecx, eax				;data < threshold?						;2
	sbb ebx, ebx				;ebx = -cf = -bit						;2
	jmp short AritDecode3												;2
dd _ImageBase		;Image base
dd 4h				;Section alignment (and PE header offset)
dd 4h				;File alignment (on disk)
;db "HAS2"			;Major/minor OS version
;db "HAS2"			;Major/minor image version
AritDecodeLoop:
	bt	[_PackedData], ebp		;test bit					;7
	db 0x8D						;shift interval: lea eax, [eax*2]
	dw 4h						;Major subsystem version
	jmp short AritDecodeLoop2								;2
dd 0h				;Reserved	(Must be 0 to work on pylles laptop)

ModelEndJumpPad:
dd _VirtualSize + 0xB6EB + 0x20000 ;Size of image (and jmp short ModelEnd)
dd 64							;Size of headers, must be at most 92 on win8 and at least 44 on xp
;Size of headers must be <= entrypoint on win8

	;Checksum
DepackInit:
	;edi=_UnpackedData
	;esi=_Models
	;push ebx
	;push nop?
	;push edi
	;ebp=0
	;ecx=0
	;eax=1
	;ebx=subsystem version
	push	ebx					;ebx=PEB	;1
    xor		ebp, ebp			;ebp=0		;2
	db		0xbb ; mov ebx, const
_SubsystemTypePtr:
	dw		0002h	;Subsystem
	dw		0h		;DLL characteristics
	;Size of stack reserve
	;Size of stack commit
	;Size of heap reserve
	;Size of heap commit
	nop							;1	;90
	mov		esi, _Models	;5		;be 7c 01 40 00
	push	byte 1			;2		;6a 01
	pop		eax				;1		;58
	mov		edi,_UnpackedData  ;5	;bf 00 00 42 00
	mov		cl, 0		;2			;b1, 00

	;Loader flags
_SpareNopPtr:
	nop							;1
	push	edi					;1
	jmp		short _DepackEntry	;2
dd 0				;Number of RVAs and Sizes
;Data directories
dd 0				;Exports RVA
;db "HAS2"			;Exports Size
;dd 0x0009ebe2		;Import RVA
AritDecode3:
	pop	edx						;edx = interval_size						;1
	jb	short .zero															;2
	
	;one
	xchg eax, edx				;eax = interval_size, edx = threshold		;1
	sub	ecx, edx				;data -= threshold							;2
	dw 0004h					;add al, 0 (Import RVA)						;2
	sub	eax, edx				;eax = interval_size - threshold			;2
.zero:
	;; ebx = -bit
	;; ecx = new data
	;; eax = new interval size

_DepackEntry:

EndCheck:
	pusha							;1
    lodsd							;1
    add  eax,edi					;2 (1)
    je   short InitHash				;if (block_end == unpacked_byte_offset) InitHash
    ; carry = 1

Model:
	;; Find probs from model
	;; If ebx is 0 or 1,
	;; update model with bit value in ebx

    push byte BaseProbDummy
BaseProbPtrP1:
    pop  edx
;    mov  [esp+zero_offset],edx
	mov  [esp+edx*2],edx        ;takes advantage of the fact that base prob is always 10
    mov  [esp+one_offset],edx   ;carry flag always clear here, could be useful (also clears upper part of edx)

	;; Init weight
	lodsd				; model weight shift mask
	xor	ebp, ebp		; weight = 0

ModelLoop:
	dec	ebp				;weight--									;1
IncreaseWeight:
	inc	ebp				;weight++									;1
	add	eax, eax		;check next bit in model weight mask		;2 (1)
	jc	short IncreaseWeight										;2
	jz	short ModelEndJumpPad										;2

;NotModelEnd:
	pusha															;1
	lodsb			; model mask									;1
	mov dl, al		; dl = mask										;2

.hashloop:
	xor	al, [edi]													;2
	imul eax, byte HASH_MULTIPLIER									;3
	add	al, byte [dword edi + 0]	;b0-b3 must be 0!!				;6
	dec	eax															;1
.next:
	dec	edi			; next byte										;1
	add	dl, dl		; hash byte?									;2	
	jc	short .hashloop												;2
	jnz	short .next													;2
	;cf=0
	;zf=1

InitHash:
	mov	edi, _HashTable												;5
	mov	ecx, _HashTableSize
	jnc	short UpdateHash

_ClearHash:
	rep stosw														;3
	or	al, [esi]													;2
	popa															;1
	lea	esi, [esi + ModelSkipDummy]									;3
ModelSkipPtrP1:
	jpo	short EndCheck												;2
	ret																;1

UpdateHash:
	div	ecx										;2
	;; edx = hash
	lea	edi, [edi + edx*2]	;edi = hashTableEntry	;3

	;; Calculate weight
	mov	ecx, ebp	;ecx = weight		;2
	xor	eax, eax	;eax = 0			;2
	scasb								;1
	je	short .boost
	add	[edi], al		;00 07
	jne	short .notboost
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
	jp	short .bits
	; eax = -1

	test ebx, ebx
	jg	short SkipUpdate
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

_ModelSkipPtr		equ	ModelSkipPtrP1-1
_BaseProbPtr		equ	BaseProbPtrP1-1
_SaturateAdjust1Ptr	equ SaturateAdjust1PtrP1-1
_SaturateAdjust2Ptr	equ SaturateAdjust2PtrP1-1

