;; -*- tab-width: 4 -*-
bits	32

	global	_Import

	extern __imp__LoadLibraryA@4
	extern __imp__MessageBoxA@16

	extern	_HeaderHashes
	extern	_DLLNames
	extern	_ImportList

; Format of DLL names:
; For each DLL
;   Zero-terminated DLL name (omitted for kernel32)
;   byte: Number of hash-entries for this DLL (including dummies)
;   For each entry
;     byte: Number of ordinals imported from this hash, minus one
; byte: -1
section .text	align=1

_Import:
	mov		ebx, _HeaderHashes
	mov		esi, _DLLNames
	mov		edi, _ImportList

	pop		eax						; eax = PEB
	mov		eax, [eax+0ch]			; goto PEB_LDR_DATA
	mov		eax, [eax+0ch]			; InLoadOrderModuleList
	mov		eax, [eax]				; forward to next LIST_ENTRY
	mov		eax, [eax]				; forward to next LIST_ENTRY
	mov		ebp, [eax+18h]			; Kernel32 base memory

;	pop		ebp
;	push	ebp
;FindKernel32:
;	dec		ebp
;	cmp		[ebp], dword 00905A4Dh	; DOS header 00905a4dh
;	jne		FindKernel32

DLLLoop:
	test	ebp, ebp
	jne		.dontEnd
	
	push	byte 0
	push	byte 0
	push	edx
	push	byte 0
	call	[__imp__MessageBoxA@16]
	ret
.dontEnd:
	xor		eax, eax
	lodsb
	xchg	ecx, eax
HashLoop:
	pusha

GetProcAddress:
	mov		eax, [ebp + 3ch]		; eax = PE header offset
	add		eax, ebp
	mov		edx, [eax + 78h]		; edx = exports directory table offset
	add		edx, ebp				; edx = exports directory table address
	mov		ecx, [edx + 18h]		; ecx = number of names

	; Check all names of procedures for the right hash

ScanProcedureNamesLoop:
	mov		eax, [edx + 20h]		; edx = name pointers table offset
	add		eax, ebp				; edx = name pointers table address
	mov		esi, [eax + ecx*4 - 4]	; esi = name pointer offset
	add		esi, ebp				; esi = name pointer address
	xor		edi, edi

CalculateHashLoop:
	rol		edi, 6
	xor		eax, eax
	lodsb
	xor		edi, eax
	dec		eax
	jge		CalculateHashLoop

	cmp		edi, [ebx]				; check computed hash
	loopne	ScanProcedureNamesLoop

;	; For debugging
;	je		.found
;	pop		edx
;	popa
;	mov		eax, ebx
;.notfoundloop:
;	stosd
;	dec		byte	[esi]
;	jge		.notfoundloop
;	jmp		NextHash
;.found:

	; Found, get the address from the table
	mov		eax, [edx + 24h]		; ebx = ordinals table RNA offset
	add		eax, ebp				; ebx = ordinals table RNA address
	mov		cx, [eax + ecx*2]		; ecx = function ordinal
	mov		eax, [edx + 1ch]		; ebx = address table RVA offset
	add		eax, ebp				; ebx = address table RVA address
	mov		eax, [eax + ecx*4]		; ebp = function RVA address

	mov		[esp + 28], eax			; stack position of eax
	popa

	add		eax,	ebp
	stosd
	
NextHash:
	add		ebx,	byte 4
	loop	HashLoop

	push	esi
	call	[__imp__LoadLibraryA@4]
	xchg	ebp, eax
	mov		edx, esi

NextDLL:
	lodsb
	dec		al
	jns		NextDLL
	
	inc		al
	jz		DLLLoop