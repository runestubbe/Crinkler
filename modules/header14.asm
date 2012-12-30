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

HASH_MULTIPLIER	equ 111

BaseProbDummy	equ	13
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
	shr  ebx,1        ;ebx=0 or 1 (decoded bit): carry flag = 0,1			;2
    popa              ;ebx>1      (one count):   jnz to AritDecode			;1
    jnz  short AritDecode   ;no need for UnpackedData: it will already be in edi	;2
  WriteBit:
    rcl  byte[edi],1  ;shift the decoded bit in		;2
    jnc  short _DepackEntry ;finished the byte?		;2
    inc  edi										;1
    jmp  short WriteBit ;yes: new byte = 1			;2
dw 8h				;Size of optional header
dw 2h				; Characteristics (almost any allowed - bit 1 must be set, bit 13 must be clear)

;optional header (PE-header)
dw 010Bh			;Magic (Image file)
;14bytes + entry point + 8bytes
;db "HA"				;Major/Minor linker version
;db "HAS2"			;Size of code
;db "HAS2"			;Size of initialized data
;db "HAS2"			;Size of uninitialized data
AritDecodeLoop:
	bt	[_PackedData], ebp		;test bit					;7
	adc	ecx, ecx				;shift bit in				;2
	inc	ebp						;next bit					;1
	add	eax, eax				;shift interval				;2
AritDecode:
	dec	eax
	db 0x3D	;cmp eax, DepackInit-_header
	dd DepackInit-_header
;db "HAS2"			;Base of code
;db "HAS2"			;Base of data

	inc eax																	;1
	jns	short AritDecodeLoop	;loop while msb of interval == 0			;2
	add	ebx, edx				;ebx = p0 + p1								;2
	push eax					;push interval_size							;1
	jmp short AritDecode2													;2
dd _ImageBase		;Image base
dd 4h				;Section alignment (and PE header offset)
dd 4h				;File alignment (on disk)
;db "HAS2"			;Major/minor OS version
;db "HAS2"			;Major/minor image version
AritDecode2:
	mul	edx						;edx:eax = p0 * interval_size				;2
	div	ebx						;eax = (p0 * interval_size) / (p0 + p1)		;2
	;; eax = threshold value between 0 and 1
	xor	ebx, ebx				;ebx = 0									;2
	pop	edx						;edx = interval_size						;1
	nop																		;1
	dw 4h						;Major subsystem version (add al, 0)
	jmp short AritDecode3		;Minor subsystem version
dd 0h				;Reserved	(Must be 0 to work on pylles laptop)

ModelEndJumpPad:
dd _VirtualSize + 0xB6EB + 0x20000 ;Size of image (and jmp short ModelEnd)
dd 0h				;Size of headers

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
	push ebx				;ebx=PEB	;1
    xor  ebp, ebp			;ebp=0		;2
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
	cmp	ecx, eax				;data < threshold?							;2
	jb	short .zero															;2
	
	;one
	sub	ecx, eax				;data -= threshold							;2
	dw 0004h					;add al, 0 (Import RVA)						;2
	xchg eax, edx				;eax = interval_size, edx = threshold		;2
	sub	eax, edx				;eax = interval_size - threshold			;2
	inc	ebx						;ebx = 1									;1
	;; ebx = bit
	;; ecx = new data
	;; eax = new interval size

.zero:
_DepackEntry:

EndCheck:
	pusha

    lodsd
    add  eax,edi
    je   short InitHash            ;if (block_end == unpacked_byte_offset) InitHash
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
	jc	short IncreaseWeight
	jz	short ModelEndJumpPad

NotModelEnd:
	pusha
	lodsb			; model mask
	movzx	edx, al	; edx = mask

.hashloop:
	xor	al, [edi]
	imul eax, byte HASH_MULTIPLIER
	add	al, [edi]
	dec	eax
.next:
	dec	edi			; next byte
	add	dl, dl		; hash byte?
	jc	short .hashloop
	jnz	short .next
	
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
	jpo	short EndCheck
	ret

UpdateHash:
	div	ecx
	;; edx = hash
	lea	edi, [edi + edx*2]	;edi = hashTableEntry

	;; Calculate weight
	mov	ecx, ebp	;ecx = weight
	xor	eax, eax	;eax = 0
	scasb
	je	short .boost
	or	ch, [edi]
	jne	short .notboost
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
	jle	short .bits

	sub	eax, ebx
	js	short .noupdate
	
	;; ebx = correct bit
	;; eax = reverse bit
	dec	edi
	inc	byte [edi + eax]
	
	;half if > 1
	shr	byte [edi + ebx], 1
	jnz	short .noupdate
	rcl	byte [edi + ebx], 1
.noupdate:
	popa
	inc	esi		; Next model
	jmp	short ModelLoop
_ModelSkipPtr	equ	ModelSkipPtrP1-1
_BaseProbPtr	equ	BaseProbPtrP1-1