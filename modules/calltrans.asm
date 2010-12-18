;; -*- tab-width: 4 -*-
bits 32

	global	_CallTrans

	section	ct text align=1

_CallTrans:
	mov		edi, [esp-4]
	mov		ecx, dword 0
tloop:
	mov		al, 0e8h
	scasb
	jne		tloop
	mov		eax, [edi]
	cwde
	cmp		eax, [edi]
	jne		tloop
	sub		eax, edi
	cwde
	stosd
	loop	tloop
