	global	_ModelHash@12

	section	model text
	;; data, bitpos, model mask
_ModelHash@12:
	;pusha
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
	rol	eax, 9
	add	al, bl
	sub eax, byte 1
	jmp	.next
.hashloop:
	movzx ebx, byte [edi]
	xor eax, ebx
	rol eax, 9
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
	ret 12
