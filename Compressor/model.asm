
	global	_ModelHash@12
;	global	_GetBit@8

	section	.text
%if 0	;bah, this is sloooow
	;; data, bitpos
_GetBit@8:
	mov	edx, [esp + 1*4]
	mov	ecx, [esp + 2*4]
	xor ecx, byte 7
	xor	eax, eax
	bt	[edx], ecx
	adc	eax, eax
	ret 8
%endif

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
	and	ecx, byte 7	; get bit pos
	shr	edi, 3		; byte pos in output
	add	edi, [esp + (2+1)*4]
	
	;mov edx, 0xFF
	;shl edx, cl
	mov edx, 0xFF0000
	shr edx, cl
	
	mov dl, al
	and	dh, [edi]
	xor	al, dh
	rol	eax, 9
	add	al, dh
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
	
	ror	eax, cl
	add	al, cl
	;; eax = hash

	pop ebx
	pop edi
	ret 12
