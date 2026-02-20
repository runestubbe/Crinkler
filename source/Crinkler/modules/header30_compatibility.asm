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
global	_NumberOfDataDirectoriesPtr
global	_ExportTableRVAPtr

BaseProbDummy	equ	10
ModelSkipDummy	equ	23

zero_offset	equ	20
one_offset	equ	16

section header align=1
_header:
; DOS header
	db		'M', 'Z'
DepackInit:
	mov 	edi, _UnpackedData											;5
	push	edi															;1
	xor		eax, eax													;2
	jmp		short DepackInit2											;2

; COFF header
	db		'P', 'E', 0, 0		; PE signature
	dw		0x014C				; Machine, 386+
	dw		0					; Number of sections
	db		"HASH"				; Timestamp
	db		"HASH"				; Symbol table pointer
	db		"HASH"				; Number of symbols
	dw		8					; Size of optional header
_CharacteristicsPtr:
	dw		2					; Characteristics
	; (bit 1 must be set, bit 13 must be clear)

; Optional header (PE-header)
	dw		0x010B				; Magic (Image file)
_LinkerVersionPtr:
	dw		0					; Major/Minor linker version
	db		"HASH"				; Size of code
	db		"HASH"				; Size of initialized data
	db		"HASH"				; Size of uninitialized data
	dd		EntryPoint-_header	; Entry point
	db		"HASH"				; Base of code
	dd		12					; Base of data (and PE header offset)
	dd		_ImageBase			; Image base
	dd		4					; Section alignment (in memory)
	dd		4					; File alignment (on disk)
	db		"HASH"				; Major/minor OS version
	db		"HASH"				; Major/minor image version
	dw		4					; Major subsystem version

EntryPoint:
	jmp		short DepackInit	; Minor subsystem version
	dd		0					; Reserved
	dd		_VirtualSize+0x20000; Size of image (= Section size + Section alignment)
	dd		64					; Size of headers (= Section alignment)
	; (must be <= entrypoint on Win8+, and at least 44 on WinXP)

; 4 bytes:
; Checksum
DepackInit2:
_SpareNopPtr:
	nop							; push edi when using call transform	;1
	push	eax															;1
	inc		eax															;1
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
	pop		ebp															;1
	mov		esi, _Models												;5
	push	byte 0														;2
	pop		ecx															;1
	; Initialized state:
	; edi = _UnpackedData
	; esi = _Models
	; ebp = 0
	; ecx = 0
	; eax = 1
	; ebx = subsystem version
	; Stack: _UnpackedData (twice when using call transform)
	jmp		_DepackEntry												;5
	dw		0															;2

LoaderFlags:
	db		"HASH"				; Loader flags

_NumberOfDataDirectoriesPtr:
	dd		0					; Number of RVAs and Sizes

; Data directories
_ExportTableRVAPtr:
	dd		0					; Export Table RVA

; 8 bytes:
; Export Table Size
; Import Table RVA (must point to valid, zeroed memory)
AritDecode2:
	xchg	eax, edx			; eax = interval_size, edx = threshold	;1
	sub		eax, edx			; eax = interval_size - threshold		;2
.zero:
	sbb		ebx, ebx			; ebx = -cf = -bit						;2
	ret																	;1
	dw		4															;2

	db		"HASH"				; Import Table Size

; 32 bytes:
; Resource Table RVA
; Resource Table Size
; Exception Table RVA
; Exception Table Size
; Certificate Table RVA
; Certificate Table Size
; Base Relocation Table RVA
; Base Relocation Table Size
AritDecodeLoop:
	bt		[_PackedData], ebp	; test bit								;7
	adc		ecx, ecx			; shift bit in							;2
	inc		ebp					; next bit								;1
	add		eax, eax			; shift interval						;2
AritDecode:
	test	eax, eax			; msb of interval != 0					;2
	jns		AritDecodeLoop		; loop while msb of interval == 0		;2

	add		ebx, edx			; ebx = p0 + p1							;2
	push	eax					; push interval_size					;1
	mul		edx					; edx:eax = p0 * interval_size			;2
	div		ebx					; eax = (p0 * interval_size) / (p0 + p1);2
	;; eax = threshold value between 0 and 1
	pop		edx					; edx = interval_size					;1
	cmp		ecx, eax			; data < threshold?						;2
	jb		AritDecode2.zero											;2
	
	;one
	sub		ecx, eax			; data -= threshold						;2
	jmp		short AritDecode2											;2

	db		"HASH"				; Debug RVA

	dd		0					; Debug Size (must be 0)

times 1000 db "HASH"

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

	lodsw
	add		ax, di
	je		InitHash            ; block_end == unpacked_byte_offset
	; carry = 1

Model:
	; Find probs from model
	; If ebx is 0 or 1,
	; update model with bit value in ebx

	push	byte BaseProbDummy
BaseProbPtrP1:
	pop		edx
	mov		[esp+zero_offset], edx
	mov		[esp+one_offset], edx

	; Init weight
	lodsd						; Model weight shift mask
	xor		ebp, ebp			; weight = 0

ModelLoop:
	dec		ebp
IncreaseWeight:
	inc		ebp					; weight++
	add		eax, eax			; Check next bit in model weight mask
	jc		IncreaseWeight
	jnz		NotModelEnd

	add		ebx, ebx
	popa
	jg		AritDecodeCallPad
  WriteBit:
	rcl		byte[edi], 1		; Shift the decoded bit in
	jnc		_DepackEntry		; Finished the byte?
	inc		edi
	jmp		short WriteBit		; New byte = 1

NotModelEnd:
	pusha
	lodsb						; Model mask
	mov		dl, al				; edx = mask

.hashloop:
	crc32	eax, byte [edi]
.next:
	dec		edi					; Next byte
	add		dl, dl				; Hash byte?
	jc		.hashloop
	jnz		.next
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
	jpo	EndCheck
	ret

UpdateHash:
	div		ecx
	; edx = hash
	lea		edi, [edi + edx*2]	; edi = hashTableEntry

	; Calculate weight
	mov		ecx, ebp			; ecx = weight
	xor		eax, eax			; eax = 0
	scasb
	je		.boost
	or		ch, [edi]
	jne		.notboost
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
	jp		.bits

	test	ebx, ebx
	jg		SkipUpdate
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
	inc		esi					; Next model
	jmp		short ModelLoop
SaturateAdjust2PtrP1:

_ModelSkipPtr		equ	ModelSkipPtrP1-1
_BaseProbPtr		equ	BaseProbPtrP1-1
_SaturateAdjust1Ptr	equ SaturateAdjust1PtrP1-1
_SaturateAdjust2Ptr	equ SaturateAdjust2PtrP1-1
