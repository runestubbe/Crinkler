;; -*- tab-width: 4 -*-
bits 32

	global	_CallTrans
	global	_NumCallTransPtr

	section	ct	align=1

_CallTrans:
	mov		edi, edx
	sub		ecx, ecx
	mov		cl, 0
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

_NumCallTransPtr equ _CallTrans+5
