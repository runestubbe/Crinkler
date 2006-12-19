bits		32
extern	_PackedData
extern	_Models
extern	_HashTable
extern	_UnpackedData
extern	_HashTableSize
extern	_VirtualSize
extern	_ImageBase
extern  _EndBitIndex

global	_header
global	_DepackEntry
global  _LinkerVersionPtr
global	_SubsystemTypePtr
global	_ModelSkipPtr
global	_BaseProbPtr1
global	_BaseProbPtr2
global  _LastModelMaskPtr

BaseProbDummy	equ	13
ModelSkipDummy	equ	23

%define HeaderOffset(l) (l-_header)
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
	mov	eax, _UnpackedData
	xor	ebx, ebx
	inc	ebx
	jmp	short DepackInit2
	
;coff header
db 'P', 'E', 0, 0		;PE signature
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
dd HeaderOffset(DepackInit)	;Address of entry point
db "HASH"			;Base of code
dd 0000000Ch			;Base of data (and PE header offset)
dd _ImageBase			;Image base
dd 00010000h			;Section alignment (in memory)
dd 00000200h			;File alignment (on disk)
db "HASH"			;Major/minor OS version
db "HASH"			;Major/minor image version

DummyImport:
dw 4,8000h			;Major/minor subsystem version
dd 0h				;Reserved
dd _VirtualSize+20000h		;Size of image (= Section size + Section alignment)
dd 00010000h			;Size of headers (= Section alignment)
;Checksum
DepackInit2:
	xor	edi, edi
	xor	ebp, ebp
_SubsystemTypePtr:
dw 0002h			;Subsystem
	;DLL characteristics
	;Size of stack reserve
	;Size of stack commit
	;Size of heap reserve
	;Size of heap commit
	xchg	eax, ebx
	mov	esi, _Models
	xor	ecx, ecx
	add	al, 0
	push	ebx
	jmp	_DepackEntry
	dw	0
	
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
dd _VirtualSize+10000h		;Virtual size (= Section size)
dd 00010000h			;Virtual address (= Section alignment)
dd 200h				;Size of raw data
dd 1h				;Pointer to raw data
db "HASH"			;Pointer to relocations
dd 0h				;Pointer to line numbers
db "HASH"			;Number of relocations/line numbers
dd 0F00000E0h			;Characteristics (contains everything, is executable, readable, writable and sharable)

times	1000	db "HASH"

zero_offset	equ	20
one_offset	equ	16
section depacker align=1

	;; ebp = source bit index
	;; edi = dest bit index
	;; ecx = data
	;; eax = interval size
	;; edx = zero prob
	;; ebx = one prob
	;; esi = _Models

AritDecodeLoop:
	bt	[esi+4], ebp	;test bit		TODO: (insert correct displacement)
	adc	ecx, ecx		;shift bit in
	inc	ebp			;next bit
	add	eax, eax		;shift interval
AritDecode:
	test	eax, eax		;msb of interval != 0
	jns	AritDecodeLoop		;loop while msb of interval == 0
	
	pop ebx			;;pop c1
	pop edx			;;pop c0
	
	add	ebx, edx		;ebx = p0 + p1
	push	eax			;push interval_size
	mul	edx			;edx:eax = p0 * interval_size
	div	ebx			;eax = (p0 * interval_size) / (p0 + p1)
	;; eax = threshold value between 0 and 1

	pop	ebx			;ebx = interval_size
	pop edx			;edx = unpacked data
	push edx
	cmp	ecx, eax		;data < threshold?
	jb	.zero
	
	;one
	sub	ecx, eax		;data -= threshold
	xchg eax, ebx		;eax = interval_size, ebx = threshold
	sub	eax, ebx		;eax = interval_size - threshold
	
	;put bit
	bts [edx], edi
	inc edi
	cmp edi, _EndBitIndex
	jne .zero
	ret
.zero:
	;; ecx = new data
	;; eax = new interval size
	;; edx = unpacked data

;;eax: 00000001  ebx: --------  ecx: 00000000  edx: --------
;;ebp: 00000000  esi: _Models	edi: 00000000

_DepackEntry:
	;; Init probs
	push byte BaseProbDummy
BaseProbPtrP1:
	push byte BaseProbDummy
BaseProbPtrP2:
	
	pusha
	;; edi: byte position
	mov ecx, edi
	shr edi, byte 3
	and ecx, byte 7	;; ecx: bit position
	xor eax, eax
	
	;; parse fences
	push esi
	add esi, byte 123	;TODO: skip models
.fenceloop:
	xchg ebx, eax
	lodsd
	xchg ebp, eax	;; ebp: modelweights
	lodsw
	cmp di, ax
	jge .fenceloop
	pop esi
	
	sub ebx, edi	;; ebx: negative fence displacement
	;TODO: clamp ebx?
	
	;; add base
	add edi, edx
	
Modelloop:
	pusha
	;;(c_0, c_1) = (0, 0)
	xor eax, eax
	push eax
	push eax
	
	;;assume dl = 0 (unpacked data is section aligned)
	mov dh, 0xFF
	shr edx, cl		;;dl: bitmask
	lodsb			;;al: model mask
	
.contextloop:	;; search context for matches
	pusha
	;;check current byte + context	
	.checkbit:
	and dl, byte [edi+ebx]
	cmp dl, byte [edi]
	jne .skipbyte
	.nextbit:
	dec edi
	mov dl, 0xFF
	add al, al
	jc .checkbit
	jnz .nextbit

	;;eax = 0
	;;check next bit
	lea esi, [esp+(16+1)*4]	;esi: ptr to c0
	mov edi, esi			;edi: ptr to c0
	
	mov al, byte [edi+ebx]
	shr al, cl	;cf: next bit
	jnc .zero
	lea edi, [edi-4]
	lodsd
	.zero:
	scasd
	
	;;esi: ptr to right counter
	;;edi: ptr to wrong counter	
	add dword [esi], byte 1
	shr dword [edi], byte 1
	jnz .skipbyte
	rcl dword [edi], byte 1
	.skipbyte:
	popa
	
	inc ebx
	jl .contextloop
	
	;; get model weight
	mov ecx, ebp
	and ecx, byte 3
	
	;; boost?
	xor eax, eax
	pop edx
	cmp edx, eax
	je .boost
	cmp dword [esp], eax
	jne .notboost
.boost:
	inc ecx
	inc ecx
.notboost:

	inc eax	;eax = 1
.addloop:
	shl edx, cl
	add [esp + 8*4], edx
	pop edx
	dec eax
	jz .addloop
	
	popa
	shr ebp, 2
	
	lodsb
	cmp al, byte 0x00	;TODO: last model
LastModelMaskPtrP1:
	jne Modelloop
	
	popa
	jmp AritDecode

_BaseProbPtr1	equ	BaseProbPtrP1-1
_BaseProbPtr2	equ	BaseProbPtrP2-1

_LastModelMaskPtr equ LastModelMaskPtrP1-1