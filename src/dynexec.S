@ vim: set ft=armv5:

.cpu cortex-m0
.thumb

.extern dynexec_regs
.extern dynexec_code

.section .text.dynexec_exec, "ax", %progbits
.type dynexec_exec, %function
.thumb_func
.global dynexec_exec
dynexec_exec:
	push {r0-r7,lr}

	@ load regs from saved ones
	ldr r7, =dynexec_regs
	ldmia r7!, {r0-r6}
	ldr r7, [r7]
	push {r7}

	ldr r7, =dynexec_code
	ldr r7, [r7]
	add r7, #1 @ bluh
	blx r7

	@ save regs in saved ones

	push {r7}
	ldr r7, =dynexec_regs
	stmia r7!, {r0-r6}
	pop {r0}
	str r0, [r7]

	pop {r0-r7,pc}

	.pool

/*to_exec_code:
	pop {r7}
	push {lr}
	@ TODO
	pop {pc}*/
