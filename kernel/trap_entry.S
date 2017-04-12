#include <inc/mmu.h>
#include <inc/trap.h>

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps


.text

TRAPHANDLER_NOEC(kbd_trap_handler, IRQ_OFFSET + IRQ_KBD)
TRAPHANDLER_NOEC(timer_trap_handler, IRQ_OFFSET + IRQ_TIMER)
TRAPHANDLER_NOEC(page_fault_trap_handler, T_PGFLT)

.globl default_trap_handler;
_alltraps:
	push %ds
	push %es
	pushal

	pushl %esp # Pass a pointer which points to the Trapframe as an argument to default_trap_handler()
	call default_trap_handler
	add $4, %esp

	popal
	pop %es
	pop %ds

	add $8, %esp # Cleans up the pushed error code and pushed ISR number
	iret # pops 5 things at once: CS, EIP, EFLAGS, SS, and ESP!

