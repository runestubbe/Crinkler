;; -*- tab-width: 4 -*-
bits	32

	global	_Import

	extern __imp__LoadLibraryA@4

	extern	_HeaderHashes
	extern	_DLLNames

section .text	align=1

_Import:
	mov		edi, _HeaderHashes
	mov		esi, _DLLNames

	pop		eax						; eax = PEB
	mov		eax, [eax+0ch]			; goto PEB_LDR_DATA
	mov		eax, [eax+0ch]			; InLoadOrderModuleList
	mov		eax, [eax]				; forward to next LIST_ENTRY
	mov		eax, [eax]				; forward to next LIST_ENTRY
	mov		ebp, [eax+18h]			; Kernel32 base memory

DLLLoop:

HashLoop:
	mov		eax, [ebp + 3ch]		; eax = PE header RVA
	add		eax, ebp				; eax = PE header address
	mov		ebx, [eax + 78h]		; ebx = exports directory table RVA
	add		ebx, ebp				; ebx = exports directory table address
	mov		ecx, [ebx + 18h]		; ecx = number of names

	; Check all names of procedures for the right hash

ScanProcedureNamesLoop:
	mov		eax, [ebx + 20h]		; eax = name pointers table RVA
	add		eax, ebp				; eax = name pointers table address
	mov		eax, [eax + ecx*4 - 4]	; eax = name pointer RVA
	add		eax, ebp				; eax = name pointer address
	xor		edx, edx

CalculateHashLoop:
	rol		edx, 6
	xor		dl, [eax]
	cmp		byte [eax], 1
	inc		eax
	jnc		CalculateHashLoop

	cmp		edx, [edi]				; check computed hash
	loopne	ScanProcedureNamesLoop
	jne		short LoadDLL

	; Found, get the address from the table
	mov		eax, [ebx + 24h]		; eax = ordinals table RVA
	add		eax, ebp				; eax = ordinals table address
	mov		cx, [eax + ecx*2]		; ecx = function ordinal
	mov		eax, [ebx + 1ch]		; eax = address table RVA
	add		eax, ebp				; eax = address table address
	mov		eax, [eax + ecx*4]		; eax = function RVA
	add		eax, ebp				; eax = function address
	stosd
	
	jmp		short HashLoop

LoadDLL:
	push	esi
	call	[__imp__LoadLibraryA@4]
	xchg	ebp, eax

NextDLL:
	lodsb
	dec		al
	jns		NextDLL
	
	inc		al
	jz		DLLLoop
