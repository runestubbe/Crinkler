;; -*- tab-width: 4 -*-
bits		32
extern	_PackedData
extern	_UnpackedData
extern	_VirtualSize
extern	_ImageBase

extern _PackedDataOffset

global _UnpackedDataLengthPtr
global	_header
global	_DepackEntry
global  _LinkerVersionPtr
global	_SubsystemTypePtr
global	_BaseProbPtr1
global	_BaseProbPtr2

BaseProbDummy	equ	13

%define HeaderOffset(l) (l-_header)
%define HeaderRVA(l) 10000h+(l-_header)
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
	mov		ebp, _PackedDataOffset
	xor		eax, eax
	inc		eax
	jmp		short DepackInit2
	
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
dd HeaderOffset(DepackInit);Address of entry point
db "HASH"			;Base of code
dd 0000000Ch		;Base of data (and PE header offset)
dd _ImageBase		;Image base
dd 00010000h		;Section alignment (in memory)
dd 00000200h		;File alignment (on disk)
db "HASH"			;Major/minor OS version
db "HASH"			;Major/minor image version

DummyImport:
dw 4,8000h			;Major/minor subsystem version
dd 0h				;Reserved
dd _VirtualSize+20000h;Size of image (= Section size + Section alignment)
dd 00010000h		;Size of headers (= Section alignment)

	;Checksum
DepackInit2:
	xor		edi, edi
	std
	db		0xbb ; mov ebx, const
_SubsystemTypePtr:
	dw		0002h	;Subsystem
	dw		0h		;DLL characteristics
	;Size of stack reserve
	;Size of stack commit
	;Size of heap reserve
	;Size of heap commit
	push	esi
	mov		esi, _UnpackedData
	push	byte 0
	pop		ecx
	jmp		_DepackEntry
	dw		0

LoaderFlags:
DummyImportTable equ LoaderFlags-4
db "HASH"			;Loader flags
dd 3				;Number of RVAs and Sizes

;Data directories
;directory 0 (export table)
dd HeaderRVA(DummyDLL)		;RVA
dd HeaderRVA(DummyImport)	;Size
;directory 1 (import table)
dd HeaderRVA(DummyImportTable)	;RVA
db "HASH"			;Size
;directory 2
dd 0h				;RVA
dd 0h				;Size

;Section headers
;Name
DummyDLL:
db "lz32.dll"
dd _VirtualSize+10000h;Virtual size (= Section size)
dd 00010000h		;Virtual address (= Section alignment)
dd 200h				;Size of raw data
dd 1h				;Pointer to raw data
db "HASH"			;Pointer to relocations
dd 0h				;Pointer to line numbers
db "HASH"			;Number of relocations/line numbers
dd 0E00000E0h		;Characteristics (contains everything, is executable, readable and writable)

;times	100	db "HASH"

zero_offset	equ	20
one_offset	equ	16
section depacker align=1

;decode bit
	;; ebp = source bit index
	;; ecx = dest bit index
	;; edi = data
	;; eax = interval size
	;; edx = zero prob
	;; ebx = one prob
AritDecodeLoop:
	bt	[esi], ebp		;test bit
	adc	edi, edi		;shift bit in
	inc	ebp				;next bit
	add	eax, eax		;shift interval
AritDecode:
	test eax, eax		;msb of interval != 0
	jns	AritDecodeLoop	;loop while msb of interval == 0
	
	add	ebx, edx		;ebx = p0 + p1
	push eax			;push interval_size
	mul	edx				;edx:eax = p0 * interval_size
	div	ebx				;eax = (p0 * interval_size) / (p0 + p1)
	;; eax = threshold value between 0 and 1

	pop	edx				;edx = interval_size
	cmp	edi, eax		;data < threshold?
	jb	.zero			;cf = 1
	
	;one
	sub	edi, eax		;data -= threshold	
	xchg eax, edx		;eax = interval_size, edx = threshold
	sub	eax, edx		;eax = interval_size - threshold, cf=0

	bts [esi], ecx		;write bit
	.zero:
	
	pop ecx
	inc ecx

;init:
;; [esp] = _UnpackedData
;; ebp = source bit index, relative to _UnpackedData
;; ecx = 0
;; edi = 0
;; eax = 1
;; edx = ?
;; ebx = ?
;; esi = _UnpackedData	
_DepackEntry:
	push ecx
	xor ecx, byte 7
	
	push byte BaseProbDummy
BaseProbPtrP1:
	push byte BaseProbDummy
BaseProbPtrP2:

	mov bl, NUM_MODELS
model_loop:
	pusha
	
	;clear eax, edx
	xor eax, eax
	cdq ;clear edx
	
	mov edi, ecx
	and ecx, byte 7
	shr edi, byte 3	;edi: offset from start

.context_loop:
	bt [esi], ecx
	sbb ebp, ebp	;ebp: -bit
	
	;try to match
	pusha
	inc ecx			;bitpos \in {8..1}
	.matchloop:
	lodsb
	xor al, byte [esi+edi+1]
	shr al, cl
	jnz .no_match
	
	mov cl, byte 0
	shr bl, byte 1
	jc .matchloop
	mov cl, byte 8
	jnz .matchloop

	;update
	inc dword [esp+7*4+ebp*8]
	not ebp
	shr dword [esp+7*4+ebp*8], byte 1
	jnz .no_match
	rcl	byte [esp+7*4+ebp*8], 1
	
	.no_match:
	popa
	inc esi
	dec edi
	jg .context_loop

	mov cl, byte BOOST_FACTOR
	.add_loop:
		add dword [esp+8*4], eax
		add dword [esp+9*4], edx
		pusha
		imul edx	;TODO: imul doesn't set the zf flags, or does it?!
		test eax, eax
		popa
		loope .add_loop	;loop BOOST_FACTOR times, if c0*c1 = 0
		
.skip_model:
	popa
	dec bl
	jge model_loop
	
	pop edx
	pop ebx
	
	cmp cx, word 0
_UnpackedDataLengthPtrP1:
	jl AritDecode
	
	;return
	cld		;move to import code
	push esi
	ret
	
_BaseProbPtr1	equ	BaseProbPtrP1-1
_BaseProbPtr2	equ	BaseProbPtrP2-1
_UnpackedDataLengthPtr	equ	_UnpackedDataLengthPtrP1-2