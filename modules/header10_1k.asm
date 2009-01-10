;; -*- tab-width: 4 -*-
bits		32
extern	_PackedData
extern	_UnpackedData
extern	_VirtualSize
extern	_ImageBase
extern _DepackEndPosition
extern	_ModelMask

global	_header
global	_DepackEntry
global	_SubsystemTypePtr
global	_BaseProbPtr0
global	_BaseProbPtr1

global  _BoostFactorPtr

BaseProbDummy	equ	13

%define SECTION_SIZE 10000h

%define HeaderOffset(l) (l-_header)
%define HeaderRVA(l) SECTION_SIZE+(l-_header)
%define NUM_MODELS	15
%define BOOST_FACTOR 8

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
mov		esi, _UnpackedData
push	esi
xor		edi, edi
jmp	short DepackInit2

;coff header
db 'P', 'E', 0, 0	;PE signature
dw 014Ch			;Machine, 386+
dw 01h				;Number of sections
;db "HASH"			;Timestamp
;db "HASH"			;Symbol table pointer
;db "HASH"			;Number of symbols
AritDecode:
	test eax,eax		;msb of interval != 0
	jns	short AritDecodeLoop	;loop while msb of interval == 0
	add	ebx, edx		;ebx = p0 + p1
	nop ;std
	push eax			;push interval_size
	mul	edx				;edx:eax = p0 * interval_size
	jmp short AritDecode2

dw 0040h			;Size of optional header
dw 030Fh			;Characteristics (32bit, relocs stripped,
					;					executable image + everything stripped)

;optional header (PE-header)
dw 010Bh			;Magic (Image file)
;dw 0h				;Major/Minor linker version
;db "HASH"			;Size of code
;db "HASH"			;Size of initialized data
;db "HASH"			;Size of uninitialized data
	;; ebp = source bit index
	;; ecx = dest bit index
	;; edi = data
	;; eax = interval size
	;; edx = zero prob
	;; ebx = one prob
AritDecodeLoop:
	bt	[_PackedData], ebp	;test bit
	adc	edi, edi			;shift bit in
	inc	ebp					;next bit
	add	eax, eax			;shift interval
	jmp AritDecode


dd HeaderOffset(DepackInit)		;Address of entry point
;db "HASH"			;Base of code
AritDecode2:
	div	ebx				;eax = (p0 * interval_size) / (p0 + p1)
	;; eax = threshold value between 0 and 1
	jmp short AritDecode3
	
dd 0000000Ch		;Base of data (and PE header offset)
dd _ImageBase		;Image base
dd SECTION_SIZE		;Section alignment (in memory)
dd 00000200h		;File alignment (on disk)
;db "HASH"			;Major/minor OS version
;db "HASH"			;Major/minor image version
AritDecode3:
	pop	edx			;edx = interval_size
	cmp	edi, eax		;data < threshold?
	jb	short zero
	xchg eax, edx		;eax = interval_size, edx = threshold
	jmp short AritDecode4

dw 4,8000h			;Major/minor subsystem version
dd 0h				;Reserved
dd _VirtualSize+SECTION_SIZE*2;Size of image (= Section size + Section alignment)
dd SECTION_SIZE		;Size of headers (= Section alignment)

	;Section header
	;db "HASH"			;Checksum / Name1
AritDecode4:
	sub	edi, edx		;data -= threshold
	jmp short one

_SubsystemTypePtr:
	dw		0002h	;Subsystem				/ Name2
	dw		0h		;DLL characteristics	/ Name2
	;Size of stack reserve
	;Size of stack commit
	;Size of heap reserve
	;Size of heap commit
	dd _VirtualSize+SECTION_SIZE;Virtual size (= Section size)
	dd SECTION_SIZE		;Virtual address (= Section alignment)
	dd 200h				;Size of raw data
	dd 1h				;Pointer to raw data

;db "HASH"			;Loader flags	/ Pointer to relocs
DepackInit2:
	push byte 8
	pop	ecx
	mov	eax, 1
;dd 0				;Number of RVAs and Sizes	/ Pointer to linenumbers (must be 0)
;Data directories
;db "HASH"			;Number of relocations/line numbers
	xor		ebp, ebp
	jmp		short _DepackEntry
dd 0E00000E0h		;Characteristics (contains everything, is executable, readable and writable)

;decode bit
AritDecodeJumpPad:
	jmp short AritDecode
one:
	sub	eax, edx		;eax = interval_size - threshold, cf=0
	bts [esi], ecx		;write bit
zero:
	
;init:
;; ebp = source bit index
;; ecx = 0
;; edi = 0
;; eax = 1
;; edx = ?
;; ebx = ?
;; esi = _UnpackedData	
zero_offset	equ	20
one_offset	equ	16
_DepackEntry:
	dec ecx
	jns .dontInc
	and ecx, byte 7
	inc esi
	.dontInc:

	push byte BaseProbDummy
BaseProbPtrP0:
	push byte BaseProbDummy
BaseProbPtrP1:
	
	mov edx, _ModelMask
	mov bl, 31
model_loop:	
	pusha
	;clear eax, edx
	xor eax, eax
	cdq 		;edx := 0
	
	mov edi, dword [esp+10*4]
	;edi: start
	;esi: current ptr

.context_loop:	
	;try to match
	pusha
	inc ecx			;bitpos \in {8..1}
.matchloop:
	mov al, byte [esi]
	xor al, byte [edi]
	shr al, cl
	jnz short .no_match
.skip:
	mov ecx, 0		;b8-bb: must be zero
	dec esi
	dec edi
	shr bl, byte 1
	jc short .matchloop
	jnz short .skip
	
	.no_match:
	popa

	;update
	jnz .end
	inc eax
	inc edx
	bt [edi], ecx
	jc .one
		shr edx, byte 1 
		jmp short .end
	.one:
		shr eax, byte 1
	.end:
	
	inc edi
	cmp esi, edi
	jg short .context_loop

	mov cl, byte BOOST_FACTOR
	BoostFactorPtrP:
	.add_loop:
		add dword [esp+8*4], eax
		add dword [esp+9*4], edx
		pusha
		imul edx
		test eax, eax		; :/
		popa
		loope .add_loop		;loop BOOST_FACTOR times, if c0*c1 = 0
	popa
.skip_model:
	dec ebx
	add edx, edx
	jc model_loop
	jnz .skip_model
	
	pop edx
	pop ebx
	
	cmp esi, _DepackEndPosition
_UnpackedDataLength:
	jle short AritDecodeJumpPad
	ret
	
_BaseProbPtr0	equ	BaseProbPtrP0-1
_BaseProbPtr1	equ	BaseProbPtrP1-1
_BoostFactorPtr	equ BoostFactorPtrP-1 