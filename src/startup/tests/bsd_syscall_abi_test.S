.text
.globl mldr_syscall_leaf_call
.type mldr_syscall_leaf_call,@function
mldr_syscall_leaf_call:
	# Match the generated SYS.h leaf: an extra call, no alignment padding.
	xorl %eax, %eax
	call __darling_bsd_syscall
	ret
.size mldr_syscall_leaf_call,.-mldr_syscall_leaf_call

.globl mldr_syscall_custom_call
.type mldr_syscall_custom_call,@function
mldr_syscall_custom_call:
	# Match custom __fork: align before the dispatcher call. Supply its
	# two possible stack arguments in the dispatcher argument convention.
	subq $24, %rsp
	movq 32(%rsp), %r11
	movq %r11, 8(%rsp)
	movq 40(%rsp), %r11
	movq %r11, 16(%rsp)
	xorl %eax, %eax
	call __darling_bsd_syscall
	addq $24, %rsp
	ret
.size mldr_syscall_custom_call,.-mldr_syscall_custom_call

.globl mldr_syscall_unknown_leaf
.type mldr_syscall_unknown_leaf,@function
mldr_syscall_unknown_leaf:
	movl $1, %eax
	call __darling_bsd_syscall
	ret
.size mldr_syscall_unknown_leaf,.-mldr_syscall_unknown_leaf

.globl mldr_syscall_unknown_custom
.type mldr_syscall_unknown_custom,@function
mldr_syscall_unknown_custom:
	subq $24, %rsp
	movl $1, %eax
	call __darling_bsd_syscall
	addq $24, %rsp
	ret
.size mldr_syscall_unknown_custom,.-mldr_syscall_unknown_custom

.globl mldr_syscall_abi_probe
.type mldr_syscall_abi_probe,@function
mldr_syscall_abi_probe:
	# A C callee must enter at rsp%16 == 8. This aligned store in its
	# red zone faults on the old dispatcher for the custom caller.
	movq %rsp, %rax
	andl $15, %eax
	movq %rax, mldr_syscall_entry_alignment(%rip)
	movaps %xmm0, -24(%rsp)
	jmp mldr_syscall_eight
.size mldr_syscall_abi_probe,.-mldr_syscall_abi_probe

.globl ___unknown_syscall
.type ___unknown_syscall,@function
___unknown_syscall:
	movq %rsp, %rax
	andl $15, %eax
	movq %rax, mldr_syscall_entry_alignment(%rip)
	movaps %xmm0, -24(%rsp)
	jmp mldr_syscall_unknown
.size ___unknown_syscall,.-___unknown_syscall

.section .note.GNU-stack,"",@progbits
