#include <kernel/trap.h>
#include <inc/mmu.h>
#include <inc/x86.h>

/* For debugging, so print_trapframe can distinguish between printing
 * a saved trapframe and printing the current trapframe and print some
 * additional information in the latter case.
 */
static struct Trapframe *last_tf;

struct Gatedesc idt[256];
struct Pseudodesc idt_pd = {
	(uint16_t)(sizeof(idt) - 1),	// pd_lim
	(uint32_t)idt					// pd_base
};

/* For debugging */
static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}

/* For debugging */
void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p \n");
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	// If this trap was a page fault that just happened
	// (so %cr2 is meaningful), print the faulting linear address.
	if (tf == last_tf && tf->tf_trapno == T_PGFLT)
		cprintf("  cr2  0x%08x\n", rcr2());
	cprintf("  err  0x%08x", tf->tf_err);
	// For page faults, print decoded fault error code:
	// U/K=fault occurred in user/kernel mode
	// W/R=a write/read caused the fault
	// PR=a protection violation caused the fault (NP=page not present).
	if (tf->tf_trapno == T_PGFLT)
		cprintf(" [%s, %s, %s]\n",
			tf->tf_err & 4 ? "user" : "kernel",
			tf->tf_err & 2 ? "write" : "read",
			tf->tf_err & 1 ? "protection" : "not-present");
	else
		cprintf("\n");
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	if ((tf->tf_cs & 3) != 0) {
		cprintf("  esp  0x%08x\n", tf->tf_esp);
		cprintf("  ss   0x----%04x\n", tf->tf_ss);
	}
}

/* For debugging */
void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
  /* TODO: Handle specific interrupts.
   *       You need to check the interrupt number in order to tell
   *       which interrupt is currently happening since every interrupt
   *       comes to this function called by default_trap_handler.
   *
   * NOTE: Checkout the Trapframe data structure for interrupt number,
   *       which we had pushed into the stack when going through the
   *       declared interface in trap_entry.S
   *
   *       The interrupt number is defined in inc/trap.h
   *
   *       We prepared the keyboard handler and timer handler for you
   *       already. Please reference in kernel/kbd.c and kernel/timer.c
   */
	switch ( tf->tf_trapno ) {
	case IRQ_OFFSET + IRQ_KBD:
		kbd_intr();
		break;
	case IRQ_OFFSET + IRQ_TIMER:
		timer_handler();
		break;
    default:
        // Unexpected trap: The user process or the kernel has a bug.
        print_trapframe(tf);
	}
}

/* 
 * Note: This is the called for every interrupt.
 */
void default_trap_handler(struct Trapframe *tf)
{
	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);
}


void trap_init()
{
	/* Keyboard interrupt setup */
	SETGATE(idt[IRQ_OFFSET + IRQ_KBD], 0, GD_KT, kbd_trap_handler, 0);
	/* Timer Trap setup */
	SETGATE(idt[IRQ_OFFSET + IRQ_TIMER], 0, GD_KT, timer_trap_handler, 0);
	/* Load IDT */
	lidt(&idt_pd);
}
