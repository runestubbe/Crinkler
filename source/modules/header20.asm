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
global	_LinkerVersionPtr
global	_SubsystemTypePtr
global	_ModelSkipPtr
global	_BaseProbPtr
global	_SpareNopPtr
global	_CharacteristicsPtr
global	_SaturatePtr
global	_SaturateAdjust1Ptr
global	_SaturateAdjust2Ptr
global _NumberOfDataDirectoriesPtr
global _ExportTableRVAPtr

HASH_MULTIPLIER	equ 111

BaseProbDummy	equ	10
ModelSkipDummy	equ	23

zero_offset	equ	20
one_offset	equ	16

section header	align=1
_header:
; DOS header
	db		'M','Z'
_LinkerVersionPtr:
	dw		0

; COFF header
	db		'P', 'E', 0, 0		; PE signature
	dw		0x014C				; Machine, 386+
	dw		0					; Number of sections

; 12 bytes:
; Timestamp
; Symbol table pointer
; Number of symbols
ModelEnd:
	add		ebx, ebx			; Put decoded bit in carry				;2
	popa																;1
	jg		short AritDecode											;2
WriteBit:
	rcl		byte[edi], 1		; Shift the decoded bit in				;2
	jnc		short _DepackEntry	; Finished the byte?					;2
	inc		edi															;1
	jmp		short WriteBit		; New byte = 1							;2

	dw		8					; Size of optional header
_CharacteristicsPtr:
	dw		2					; Characteristics
	; (bit 1 must be set, bit 13 must be clear)

; Optional header (PE-header)
	dw		0x010B				; Magic (Image file)

; 14 bytes:
; Major/Minor linker version
; Size of code
; Size of initialized data
; Size of uninitialized data
AritDecodeLoop2:
	adc		ecx, ecx			; Shift bit in							;2
	inc		ebp					; Next bit								;1
AritDecode:
	test	eax, eax			; Test sign								;2
	jns		short AritDecodeLoop; Loop while msb of interval == 0		;2
	add		ebx, edx			; ebx = p0 + p1							;2
	push	eax					; Push interval_size					;1
	mul		edx					; edx:eax = p0 * interval_size			;2
	nop																	;1
	db		0x3D				; cmp eax, DepackInit-_header			;1

	dd		DepackInit-_header	; Entry point

; 8 bytes:
; Base of code
; Base of data
	div		ebx					; eax = (p0 * interval_size) / (p0 + p1);2
	; eax = threshold value between 0 and 1
	cmp		ecx, eax			; data < threshold?						;2
	sbb		ebx, ebx			; ebx = -cf = -bit						;2
	jmp		short AritDecode3											;2

	dd		_ImageBase			; Image base
	dd		4					; Section alignment (and PE header offset)
	dd		4					; File alignment (on disk)

; 12 bytes:
; Major/minor OS version
; Major/minor image version
; Major/minor subsystem version
AritDecodeLoop:
	bt		[_PackedData], ebp	; Test bit								;7
	db		0x8D				; Shift interval: lea eax, [eax*2]		;1
	dw		4					; Major subsystem version				;2
	jmp		short AritDecodeLoop2										;2

	dd		0					; Reserved (Must be 0)

ModelEndJumpPad:
	dd		_VirtualSize + 0x2B6EB; Size of image (and jmp short ModelEnd)
	dd		64					; Size of headers
	; (must be <= entrypoint on Win8+, and at least 44 on WinXP)

; 4 bytes:
; Checksum
DepackInit:
	push	ebx					; ebx = PEB								;1
	xor		ebp, ebp			; ebp = 0								;2
	db		0xBB				; mov ebx, const						;1

_SubsystemTypePtr:
	dw		2					; Subsystem
	dw		0					; DLL characteristics

; 16 bytes:
; Size of stack reserve
; Size of stack commit
; Size of heap reserve
; Size of heap commit
; (must all have reasonable sizes, since these are allocated)
	nop							; 90									;1
	mov		esi, _Models		; BE 7C 01 40 00						;5
	push	byte 1				; 6A 01									;2
	pop		eax					; 58									;1
	mov		edi, _UnpackedData	; BF 00 00 42 00						;5
	mov		cl, 0				; B1 00									;2

; 4 bytes:
; Loader flags
_SpareNopPtr:
	nop							; push edi when using call transform	;1
	push	edi															;1
	; Initialized state:
	; edi = _UnpackedData
	; esi = _Models
	; ebp = 0
	; ecx = 0
	; eax = 1
	; ebx = subsystem version
	; Stack: _UnpackedData (twice if call transform), PEB
	jmp		short _DepackEntry											;2

_NumberOfDataDirectoriesPtr:
	dd		0					; Number of RVAs and Sizes

; Data directories
_ExportTableRVAPtr:
	dd		0					; Export Table RVA

; 4 bytes:
; Export Table Size
AritDecode3:
	pop		edx						; edx = interval_size				;1
	jb		short .zero													;2
	;one
	xchg	eax, edx				; interval_size <-> threshold		;1

; 4 bytes:
; Import Table RVA (must point to valid, zeroed memory)
	sub		ecx, edx				; data -= threshold					;2
	add		al, 0					; 04 00								;2

; 8 bytes:
; Import Table Size
; Resource Table RVA
	sub		eax, edx				; eax = interval_size - threshold	;2
.zero:
	; ebx = -bit
	; ecx = new data
	; eax = new interval size

_DepackEntry:

EndCheck:
	pusha																;1
	lodsd																;1
	add		eax,edi														;2
	je		short InitHash			; block_end == unpacked_byte_offset	;2
	; carry = 1

; 32 bytes:
; Resource Table Size
; Exception Table RVA
; Exception Table Size
; Certificate Table RVA
; Certificate Table Size
; Base Relocation Table RVA
; Base Relocation Table Size
; Debug RVA
Model:
	; Find probs from model
	; If ebx is 0 or 1,
	; update model with bit value in ebx

	push	byte BaseProbDummy											;2
BaseProbPtrP1:
	pop		edx															;1
; Take advantage of the fact that base prob is always 10
;	mov		[esp+zero_offset],edx
	mov		[esp+edx*2], edx											;3
	mov		[esp+one_offset], edx										;4

	; Init weight
	lodsd							; Model weight shift mask			;1
	xor		ebp, ebp				; weight = 0						;2

ModelLoop:
	dec		ebp						; weight--							;1
IncreaseWeight:
	inc		ebp						; weight++							;1
	add		eax, eax				; Check next bit in model weight mask;2
	jc		short IncreaseWeight										;2
	jz		short ModelEndJumpPad										;2

	pusha																;1
	lodsb							; Model mask						;1
	mov		dl, al					; dl = mask							;2

.hashloop:
	xor		al, [edi]													;2
	imul	eax, byte HASH_MULTIPLIER									;3
	db		0x02, 0x87				; add al, [dword edi + 0]			;2

	dd		0						; Debug Size (must be 0)

	dec		eax
.next:
	dec		edi						; Next byte
	add		dl, dl					; Hash byte?
	jc		short .hashloop
	jnz		short .next
	; cf = 0
	; zf = 1

InitHash:
	mov		edi, _HashTable
	mov		ecx, _HashTableSize
	jnc		short UpdateHash

_ClearHash:
	rep stosw
	or		al, [esi]
	popa
	lea		esi, [esi + ModelSkipDummy]
ModelSkipPtrP1:
	jpo		short EndCheck
	ret

UpdateHash:
	div		ecx
	; edx = hash
	lea		edi, [edi + edx*2]		; edi = hashTableEntry

	; Calculate weight
	mov		ecx, ebp				; ecx = weight
	xor		eax, eax				; eax = 0
	scasb
	je		short .boost
	add		[edi], al
	jne		short .notboost
.boost:
	inc		ecx
	inc		ecx
.notboost:

	; Add probs
.bits:
	movzx	edx, byte [edi + eax]
	shl		edx, cl
	add		[esp + 8*4 + zero_offset + eax*4], edx
	dec		eax
	jp		short .bits
	; eax = -1

	test	ebx, ebx
	jg		short SkipUpdate
SaturateAdjust1PtrP1:

	; Half if > 1
	shr		byte [edi + ebx], 1
	jnz		short .nz
	rcl		byte [edi + ebx], 1
.nz:

	; Inc correct bit
	not		ebx
	inc		byte [edi + ebx]
_SaturatePtr:
; Saturation code inserted here when the /SATURATE option is used:
;	jnz		.nowrap
;	dec		byte [edi + ebx]
;.nowrap:

SkipUpdate:
	popa
	inc		esi						; Next model
	jmp		short ModelLoop
SaturateAdjust2PtrP1:

_ModelSkipPtr		equ	ModelSkipPtrP1-1
_BaseProbPtr		equ	BaseProbPtrP1-1
_SaturateAdjust1Ptr	equ SaturateAdjust1PtrP1-1
_SaturateAdjust2Ptr	equ SaturateAdjust2PtrP1-1

