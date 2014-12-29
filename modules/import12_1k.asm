;; -*- tab-width: 4 -*-
bits	32

	global	_Import
	global	_HashShiftPtr
	global	_MaxNameLengthPtr

	extern __imp__LoadLibraryA@4


	extern	_DLLNames
	extern	_ImportList
	extern  _HashFamily

; Format of DLL names:
; For each DLL
;   Zero-terminated DLL name (omitted for kernel32)
;   byte: Number of hash-entries for this DLL (including dummies)
;   For each entry
;     byte: Number of ordinals imported from this hash, minus one
; byte: -1
section .text	align=1

_Import:
	add	cl, al						; here to help compression. first byte is predicted as all 0
	mov		edi, _DLLNames
	;pop		eax					; moved to header
	mov		eax, [eax+0ch]			; goto PEB_LDR_DATA
	mov		eax, [eax+0ch]			; InLoadOrderModuleList
	mov		eax, [byte eax+0h]				; forward to next LIST_ENTRY
	mov		eax, [byte eax+0h]				; forward to next LIST_ENTRY
	mov		eax, [eax+18h]			; Kernel32 base memory

	;eax: misc
	;ebx: exports ptr
	;ecx: ordinal number
	;edx: dllnames
DLLLoop:
	xchg	ebp, eax
	
	mov		eax, [ebp + 3ch]		; eax = PE header offset
	add		eax, ebp
	mov		ebx, [eax + 78h]		; ebx = exports directory table offset
	add		ebx, ebp				; ebx = exports directory table address
	mov		ecx, [ebx + 18h]		; ecx = number of names
	dec		ecx
	; Check all names of procedures for the right hash

ScanProcedureNamesLoop:
	mov		eax, [ebx + 20h]		; ebx = name pointers table offset
	add		eax, ebp				; ebx = name pointers table address
	mov		esi, [eax + ecx*4]		; esi = name pointer offset
	add		esi, ebp				; esi = name pointer address

; Found, get the address from the table
	mov		eax, [ebx + 24h]		; eax = ordinals table RNA offset
	add		eax, ebp				; eax = ordinals table RNA address
	movzx	edx, word [eax + ecx*2]		; ecx = function ordinal
	mov		eax, [ebx + 1ch]		; eax = address table RVA offset
	add		eax, ebp				; eax = address table RVA address
	mov		edx, [eax + edx*4]		; ebp = address of function RVA address
	add		edx, ebp

	xor		eax, eax
CalculateHashLoop:
	lodsb
	imul	eax, _HashFamily
	add		al, al
	jne		CalculateHashLoop
	shr		eax, byte 0				; filled out by crinkler
_HashShiftPtrP:
	mov		dword [_ImportList+eax*4], edx
	
	dec		ecx
	jns		ScanProcedureNamesLoop
	
	push	edi
	call	[__imp__LoadLibraryA@4]
	
	add		edi, byte 0
_MaxNameLengthPtrP:

	test	eax, eax
	jnz		short DLLLoop
	
_HashShiftPtr		equ _HashShiftPtrP-1
_MaxNameLengthPtr	equ _MaxNameLengthPtrP-1