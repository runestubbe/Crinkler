;; -*- tab-width: 4 -*-
bits	32

	global	_Import

	extern __imp__LoadLibraryA@4


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
	xor		eax, eax
	mov		esi, _DLLNames
	mov		eax, [fs:eax+30h]		; PEB  base
	mov		eax, [eax+0ch]			; goto PEB_LDR_DATA
	; first entry in InInitializationOrderModuleList
	mov		eax, [eax+1ch]
	mov		eax, [byte eax+00h]		; forward to next LIST_ENTRY
	mov		ebp, [eax+08h]			; Kernel32 base memory
	
	cld
DLLLoop:
	pusha

GetProcAddress:
	mov		eax, [ebp + 3ch]		; eax = PE header offset
	add		eax, ebp
	mov		edi, [eax + 78h]		; edi = exports directory table offset
	add		edi, ebp				; edi = exports directory table address
	mov		ecx, [edi + 18h]		; ecx = number of names

	; Check all names of procedures for the right hash

ScanProcedureNamesLoop:
	mov		eax, [edi + 20h]		; edi = name pointers table offset
	add		eax, ebp				; edi = name pointers table address
	mov		esi, [eax + ecx*4]		; esi = name pointer offset
	add		esi, ebp				; esi = name pointer address
	
	xor		edx, edx
CalculateHashLoop:
	rol		edx, 6
	xor		eax, eax
	lodsb
	add		edx, eax
	dec		eax
	jge		CalculateHashLoop
	
	
	; Found, get the address from the table
	mov		eax, [edi + 24h]		; eax = ordinals table RNA offset
	add		eax, ebp				; eax = ordinals table RNA address
	xor		ebx, ebx
	mov		bx, [eax + ecx*2]		; ecx = function ordinal
	mov		eax, [edi + 1ch]		; eax = address table RVA offset
	add		eax, ebp				; eax = address table RVA address
	mov		eax, [eax + ebx*4]		; ebp = address of function RVA address
	add		eax, ebp
	
	and		edx, 0x3FFFFF
	mov		dword [_ImportList+edx*4], eax
	loop	ScanProcedureNamesLoop
	popa
	
	push	esi
	call	[__imp__LoadLibraryA@4]
	test eax, eax
	jz .done
	xchg	ebp, eax

	add esi, byte 9
	jmp	short DLLLoop
	.done