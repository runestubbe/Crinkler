	global	_ModelHash@16
	global	_ModelHashStart@8

	section	model text
	;; data, bitpos, model mask, hashmul
_ModelHash@16:
	push edi
	push ebx
	
	mov	edi, [esp + (2+2)*4]
	mov	eax, [esp + (2+3)*4]
	mov	edx, eax
	;; ebp = data
	;; edi = bitpos
	;; eax = model mask

	mov	ecx, edi
	not ecx
	and	ecx, byte 7	; get bit pos
	inc ecx
	
	shr	edi, 3		; byte pos in output
	add	edi, [esp + (2+1)*4]
	
	mov ebx, 0x100
	mov bl, [edi]
	shr ebx, cl

	xor	al, bl
	imul eax, dword [esp + (2+4)*4]
	add	al, bl
	sub eax, byte 1
	jmp	.next
.hashloop:
	movzx ebx, byte [edi]
	xor eax, ebx
	imul eax, dword [esp + (2+4)*4]
	add al, bl
	sub eax, byte 1
.next:
	sub edi, byte 1
	add	dl,dl
	jc	.hashloop
	jnz	.next
	
	;; eax = hash

	pop ebx
	pop edi
	ret 16


	;; model mask, hashmul
_ModelHashStart@8:	
	mov	eax, [esp + (1)*4]
	mov	edx, eax
	;; eax = model mask

	imul eax, dword [esp + (2)*4]
	sub eax, byte 1
	jmp	.next
.hashloop:
	imul eax, dword [esp + (2)*4]
	sub eax, byte 1
.next:
	add	dl,dl
	jc	.hashloop
	jnz	.next
	
	;; eax = hash

	ret 8

