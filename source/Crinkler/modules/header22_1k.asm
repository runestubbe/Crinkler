bits		32
extern	_PackedData
extern	_UnpackedData
extern	_ImageBase
extern	_ModelMask

global	_header
global	_DepackEntry
global	_SubsystemTypePtr
global	_BaseProbPtr0
global	_BaseProbPtr1
global  _BoostFactorPtr
global	_DepackEndPositionPtr
global	_VirtualSizeHighBytePtr	
global	_CharacteristicsPtr
global	_LinkerVersionPtr

section header	align=1

_header:
; DOS header
	db	'M', 'Z'
_LinkerVersionPtr:
	dw 0

; COFF header
	db 'P', 'E', 0, 0			; PE signature
	dw 014Ch					; Machine, 386+
	dw 0h						; Number of sections

; 12 bytes:
; Timestamp
; Symbol table pointer
; Number of symbols
AritDecodeLoop:
	bt		[_PackedData], ebp	; Test bit								;7
	adc		esi, esi			; Shift bit in							;2
	inc		ebp					; Next bit								;1
	jmp		short AritDecodeLoop2										;2
	dw		8					; Size of optional header
_CharacteristicsPtr:
	dw		2					; Characteristics
	; (bit 1 must be set, bit 13 must be clear)

; Optional header (PE-header)
	dw		0x010B				; Magic (Image file)

; 18 bytes:
; Major/Minor linker version
; Size of code
; Size of initialized data
; Size of uninitialized data
; Entry point
_match_lala:
	; Try to match
	pusha																;1
_matchloop:
	mov		al, byte [esi]												;2
	shr		al, cl														;2
	xor		al, byte [edi]												;2
	jnz		short _no_match												;2
_skip:
	dec		esi															;1
	dec		edi															;1
	xor		ecx, ecx													;2
	mov		ebp, DepackInit-_header										;5

; 8 bytes:
; Base of code
; Base of data
	shr		bl, 1														;2
	jc		short _matchloop											;2
	jnz		short _skip													;2
	jmp		short _no_match												;2

	dd		_ImageBase			; Image base
	dd		4					; Section alignment (and PE header offset)
	dd		4					; File alignment (on disk)

; 8 bytes:
; Major/minor OS version
; Major/minor image version
AritDecodeLoop2:
	add		eax, eax			; Shift interval						;2
AritDecode:
	test	eax, eax			; MSB of interval != 0					;2
	jns		short AritDecodeLoop; Loop while msb of interval == 0		;2
	jmp		short _AritDecode2											;2

	dw		3					; Major subsystem version
	dw		0x8000				; Minor subsystem version

	dd		0					; Reserved (Must be 0)

; 4 bytes:
; Size of imnage
_AritDecode2:
	jmp		short _AritDecode3											;2
	db		0x00														;1
_VirtualSizeHighBytePtr:
	db		0x01														;1

	dd		48					; Size of headers
	; (must be <= entrypoint on Win8+, and at least 44 on WinXP)

; 4 bytes:
; Checksum
_AritDecodeJumpPad:
	pop		edx					; One prob								;1
	jl		short AritDecode											;2
	ret																	;1

_SubsystemTypePtr:
	dw		2					; Subsystem
	dw		0					; DLL characteristics

; 16 bytes:
; Size of stack reserve
; Size of stack commit
; Size of heap reserve
; Size of heap commit
; (must all have reasonable sizes, since these are allocated)
DepackInit:
	; ebx = PEB
	push	ebx					; 53									;1
	mov		edi, _UnpackedData	; BF 00 00 42 00						;5
	push 	byte 1				; 6A 01									;2
	pop 	eax					; 58									;1
	xor		ebp, ebp			; 31 ED									;2
	add		esi, esi			; 01 F6									;2
	push 	edi					; 57									;1

	; edi = dst ptr
	; esi = data
	; ebp = source bit index
	; eax = interval size
	; ebx = one prob
	; ecx = dest bit index
	; edx = zero prob

_DepackEntry:
	push	byte 8														;2

; 8 bytes:
; Loader flags
; Number of RVAs and Sizes
	pop		ecx					; 59									;1
_DontInc:
	push 	byte 0				; 6A ??									;2
BaseProbPtrP0:
	cmp		eax, strict dword 0											;5

; Data directories

; 12 bytes:
; Export Table RVA
; Export Table Size
; Import Table RVA (must point to valid, zeroed memory)
	push	byte 0				; 6A ??									;2
BaseProbPtrP1:
	mov		edx, _ModelMask		; BA ?? ?? ?? ??						;5
	mov		bl, 31														;2
model_loop:
	pusha																;1
	add		al, 0				; 04 00									;2

; 8 bytes:
; Import Table Size
; Resource Table RVA
	xor		eax, eax			; eax = 0								;2
	cdq 						; edx = 0								;1
	
	mov		esi, dword [esp+10*4]; esi = UnpackedData					;4
	dec		esi															;1
	; esi = start
	; edi = current ptr

; 36 bytes:
; Resource Table Size
; Exception Table RVA
; Exception Table Size
; Certificate Table RVA
; Certificate Table Size
; Base Relocation Table RVA
; Base Relocation Table Size
; Debug RVA
; Debug Size (must be 0)
_context_loop:
	jmp		short _match_lala											;2

_no_match:
	popa																;1

	; Update
	jnz		short _no_update											;2
	inc		eax															;1
	inc		edx															;1
	ror		byte [esi], cl												;2
	jc		short _one													;2
	shr		edx, 1														;2
	jmp		short _end													;2
_one:
	shr		eax, 1														;2
_end:
	rol		byte [esi], cl												;2
_no_update:
	inc		esi															;1
	cmp		edi, esi													;2
	jg		short _context_loop											;2

	mov		cl, byte 0			; Boost factor							;2
BoostFactorPtrP:
	mov		esi, esp													;2
.add_loop:
	add		dword [esi+9*4], edx										;3
	cmp		eax, strict dword 0											;5

	jz		.loop
	add		dword [esi+8*4], eax
	test	edx, edx
.loop:
	loope	.add_loop			; loop BOOST_FACTOR times, if c0*c1 = 0
	popa
.skip_model:
	dec		ebx
	add		edx, edx
	jc		short model_loop
	jnz		short .skip_model
	
	pop		ebx					; Zero prob
	cmp		di, strict word 0
DepackEndPositionP:
	jmp		short _AritDecodeJumpPad
_AritDecode3:
	add		ebx, edx			; ebx = p0 + p1
	push	eax					; Push interval_size
	mul		edx					; edx:eax = p0 * interval_size
	div		ebx					; eax = (p0 * interval_size) / (p0 + p1)
	pop		edx					; edx = interval_size
	cmp		esi, eax			; data < threshold?
	jb		short one
	xchg	eax, edx			; eax = interval_size, edx = threshold
	sub		esi, edx			; data -= threshold
	sub		eax, edx			; eax = interval_size - threshold, cf=0
one:
	rcl		byte [edi], 1

	loop	_DontInc
	inc		edi
	jmp		short _DepackEntry
	
;-----------------------------------------------------------------------------
_BaseProbPtr0			equ	BaseProbPtrP0-1
_BaseProbPtr1			equ	BaseProbPtrP1-1
_BoostFactorPtr			equ BoostFactorPtrP-1
_DepackEndPositionPtr	equ DepackEndPositionP-2
