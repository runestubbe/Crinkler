/* Stubs for the MSVC compiler-support routines referenced by prebuilt .lib
 * files (external/distorm) that MinGW's runtime does not provide.
 *
 * These come from /GS (buffer security check) code generation. distorm neither
 * throws nor relies on the cookie for anything Crinkler depends on, so a
 * permissive implementation is enough to let the MSVC objects link.
 */

#include <stddef.h>

#if defined(__x86_64__)

/* x64: the cookie is referenced as "__security_cookie" and the check is a
 * plain (register-argument) call. */
unsigned long long __security_cookie = 0x00002B992DDFA232ULL;

void __security_check_cookie(unsigned long long cookie)
{
	(void)cookie;
}

/* Language-specific SEH handler emitted into .xdata for /GS functions.
 * 1 == ExceptionContinueSearch. */
int __GSHandlerCheck(void *exception_record, void *establisher_frame,
                     void *context_record, void *dispatcher_context)
{
	(void)exception_record;
	(void)establisher_frame;
	(void)context_record;
	(void)dispatcher_context;
	return 1;
}

#elif defined(__i386__)

/* x86: MSVC emits "___security_cookie" (C decoration) and calls the check
 * routine through the __fastcall-decorated name "@__security_check_cookie@4". */
unsigned int __security_cookie = 0xBB40E64EU;

void __attribute__((__fastcall__)) __security_check_cookie(unsigned int cookie)
{
	(void)cookie;
}

/* 64-bit integer helpers from the MSVC CRT. They use a register-based private
 * calling convention (value in EDX:EAX, shift count in CL, result in EDX:EAX),
 * so they have to be written in assembly rather than plain C. */
__asm__(
	".text\n"

	/* unsigned __int64 >> count */
	".globl __aullshr\n"
	"__aullshr:\n"
	"	cmpb $64, %cl\n"
	"	jae 3f\n"
	"	cmpb $32, %cl\n"
	"	jae 2f\n"
	"	shrdl %cl, %edx, %eax\n"
	"	shrl %cl, %edx\n"
	"	ret\n"
	"2:	movl %edx, %eax\n"
	"	xorl %edx, %edx\n"
	"	andb $31, %cl\n"
	"	shrl %cl, %eax\n"
	"	ret\n"
	"3:	xorl %eax, %eax\n"
	"	xorl %edx, %edx\n"
	"	ret\n"

	/* signed __int64 >> count (arithmetic) */
	".globl __allshr\n"
	"__allshr:\n"
	"	cmpb $64, %cl\n"
	"	jae 3f\n"
	"	cmpb $32, %cl\n"
	"	jae 2f\n"
	"	shrdl %cl, %edx, %eax\n"
	"	sarl %cl, %edx\n"
	"	ret\n"
	"2:	movl %edx, %eax\n"
	"	sarl $31, %edx\n"
	"	andb $31, %cl\n"
	"	sarl %cl, %eax\n"
	"	ret\n"
	"3:	sarl $31, %edx\n"
	"	movl %edx, %eax\n"
	"	ret\n"

	/* __int64 << count */
	".globl __allshl\n"
	"__allshl:\n"
	"	cmpb $64, %cl\n"
	"	jae 3f\n"
	"	cmpb $32, %cl\n"
	"	jae 2f\n"
	"	shldl %cl, %eax, %edx\n"
	"	shll %cl, %eax\n"
	"	ret\n"
	"2:	movl %eax, %edx\n"
	"	xorl %eax, %eax\n"
	"	andb $31, %cl\n"
	"	shll %cl, %edx\n"
	"	ret\n"
	"3:	xorl %eax, %eax\n"
	"	xorl %edx, %edx\n"
	"	ret\n"
);

#endif
