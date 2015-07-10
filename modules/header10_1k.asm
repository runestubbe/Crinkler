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

section header	align=1

_header:
	db	'MZ'	;exe magic

	;Two bytes of funnies
	;db '1K'
	;db 'DO'
	;db 'DU'
	;db 'HO'
	;db 'NO'
	;db 'PO'
	db 'RA'

	;coff header
	db 'PE', 0, 0		;PE signature
	dw 014Ch			;Machine, 386+
	dw 0h				;Number of sections

	;12 bytes
	;db "HASH"			;Timestamp
	;db "HASH"			;Symbol table pointer
	;db "HASH"			;Number of symbols
	;dw 0878h			;Size of optional header
AritDecodeLoop:
	;12
	bt	[_PackedData], ebp	;test bit								;7
	adc	esi, esi			;shift bit in							;2
	inc	ebp					;next bit								;1
	jmp short AritDecodeLoop2
	dw 0008h			;Size of optional header
	dw 0002h			;Characteristics (32bit)

;optional header (PE-header)
	dw 010Bh			;Magic (Image file)
	
	;14 bytes + entry point + 8 bytes
	;dw 0h				;Major/Minor linker version
	;db "HASH"			;Size of code
	;db "HASH"			;Size of initialized data
	;db "HASH"			;Size of uninitialized data
	
	;db "HASH"			;Base of code
	;db "HASH"			;Base of data (and PE header offset)
_decoder0:
	xor ecx, ecx
	shr bl, 1		;shr bl, byte 1					;2
	jc short _matchloop								;2
	jnz short _skip									;2
	
	_no_match:
	popa											;1
	
	;update
	jnz short _no_update							;2
	inc eax											;1
	inc edx											;1
	mov	ebp, DepackInit-_header						;5 ;Address of entry point
	ror byte [esi], cl								;2
	jc short _one									;2
	shr edx, 1				;shr edx, byte 1		;2
	jmp short _end			;end					;2

	dd _ImageBase		;Image base
	dd 4h				;Section alignment (in memory)
	dd 4h				;File alignment (on disk)	(must be=SectionAlignment when <0x1000)

	; 8 bytes + 0x0003 + 6 bytes + virtualsize low bytes

	;db "HASH"		;Major/Minor OS version
	;db "HASH"		;Major/minor image version
AritDecodeLoop2:
	add	eax, eax			;shift interval							;2
AritDecode:
	;23
	test eax,eax		;msb of interval != 0						;2
	jns	short AritDecodeLoop	;loop while msb of interval == 0	;2
	jmp short _AritDecode2

	dw 0003h		;Major subsystem version
	dw 8000h		;can be something else

	dd 0				;Reserved	(needs to be null to work for pylle. d3d fails)
_AritDecode2:
	jmp short _AritDecode3
	db 0x00, 
_VirtualSizeHighBytePtr:
	db 0x01	;TODO: crinkler should set this to something reasonable
	;dd _VirtualSize+SECTION_SIZE*2;Size of image (= Section size + Section alignment)
	dd 30h				;Size of headers (can be almost anything, must be >= 0x30 in Win7 due to bug)

	;Section header
	;4 bytes
	;db "HASH"			;Checksum / Name1
_AritDecodeJumpPad:
	jle short AritDecode		;2
	pop eax						;1
	ret							;1
_SubsystemTypePtr:
	dw		0003h	;Subsystem				/ Name2
	dw		0h		;DLL characteristics	/ Name2
	; 4 dwords with small msb + 4 bytes + 0x00000000 + 52bytes + 0x00000000
	;dd		0h		;Size of stack reserve
	;dd		0h		;Size of stack commit
	;dd		0h		;Size of heap reserve
	;dd		0h		;Size of heap commit
	;db "HASH"			;Loader flags	/ Pointer to relocs
DepackInit:
	;20
	nop										;90
	mov		edi, _UnpackedData				;BF 00 00 41 00
	push	byte 1							;6A 01
	pop		eax								;58
	push	edi								;57
	push	byte 0							;6A 00
	pop		esi								;5E
	push	ebx		; ebx=[fs:30h]			;53
	push	byte 0							;6A 00
	pop		ebp								;5D
	;; edi = dst ptr
	;; esi = data
	;; ebp = source bit index
	;; eax = interval size
	;; ebx = one prob
	;; ecx = dest bit index
	;; edx = zero prob

_DepackEntry:
	push	byte 8
	mov		ecx, 0
	;dd 0				;Number of RVAs and Sizes	/ Pointer to linenumbers (must be 0)
;Data directories
	pop		ecx			;1		59
_DontInc:
	push byte 0			;2		6a ??
BaseProbPtrP0:
	push byte 0			;2		6a ??
BaseProbPtrP1:
	
	mov edx, _ModelMask	;5		ba ?? ?? ?? ??
	add al, 0			;2		04 00

	mov bl, 31			;2
model_loop:
	pusha				;1
	;clear eax, edx
	xor eax, eax		;2
	cdq 		;edx := 0	;1
	
	mov esi, dword [esp+11*4]		;esi := UnpackedData		;4
	;esi: start
	;edi: current ptr

_context_loop:
	;try to match
	pusha														;1
_matchloop:
	mov al, byte [esi]											;2
	shr al, cl													;2
	xor al, byte [edi]											;2
	jnz short _no_match											;2
_skip:
	dec esi														;1
	dec edi														;1
	jmp _decoder0	;TODO: fix me
	_one:
		shr eax, 1		;shr edx, byte 1
	_end:
	rol byte [esi], cl
_no_update:
	inc esi
	cmp edi, esi
	jg short _context_loop

	mov cl, byte 0	;boost factor
	BoostFactorPtrP:
	.add_loop:
		add dword [esp+9*4], edx;4
		test eax, eax
		nop
		mov ebp, 0						;b0-b3 must be 0 (DebugTable)
		jz .loop
		add dword [esp+8*4], eax;4
		test edx, edx
	.loop:
		loope .add_loop			;2		;loop BOOST_FACTOR times, if c0*c1 = 0
	popa						;1
.skip_model:
	dec ebx						;1
	add edx, edx				;2
	jc short model_loop			;2
	jnz short .skip_model		;2
	
	pop ebx		;zero prob		;1
	pop edx		;one prob		;1

	;cmp di, 0x0000				;5
	db 0x66, 0x81, 0xFF
	db 0x00, 0x00, ; length
DepackEndPositionP:
	jmp short _AritDecodeJumpPad
_AritDecode3:
	add	ebx, edx		;ebx = p0 + p1								;2		;Minor subsystem version	(this can be a lot of stuff)
	push eax			;push interval_size							;1
	mul	edx				;edx:eax = p0 * interval_size				;2
	div	ebx				;eax = (p0 * interval_size) / (p0 + p1)		;2
	
	pop	edx				;edx = interval_size						;1
	cmp	esi, eax		;data < threshold?							;2
	jb	short one													;2
	xchg eax, edx		;eax = interval_size, edx = threshold		;1
	sub	esi, edx		;data -= threshold							;2
	sub	eax, edx		;eax = interval_size - threshold, cf=0		;2
one:
	rcl byte [edi], 1		;rcl byte [edi], byte 1					;2

	dec ecx					;1
	jne short _DontInc		;2
	inc edi					;1
	jmp short _DepackEntry	;2
	
;-----------------------------------------------------------------------------
_BaseProbPtr0	equ	BaseProbPtrP0-1
_BaseProbPtr1	equ	BaseProbPtrP1-1
_BoostFactorPtr	equ BoostFactorPtrP-1
_DepackEndPositionPtr equ DepackEndPositionP-2