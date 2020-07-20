;; -*- tab-width: 4 -*-
bits	32

extern __imp____getmainargs
extern __imp__ExitProcess@4

extern _main
global _mainCRTStartup


section console text align=1
_mainCRTStartup:
    push    eax
    mov     dword [esp], esp
    push    eax
    mov     dword [esp], esp
    push    eax
    mov     dword [esp], esp
    call    [__imp____getmainargs]
    call    _main
    push    eax
    call    [__imp__ExitProcess@4]


section fltused bss align=4
__fltused:
	resd	1
