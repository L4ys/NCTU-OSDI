#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/mem.h>
#include <kernel/cpu.h>
#include <kernel/syscall.h>
#include <kernel/trap.h>
#include <inc/stdio.h>

void do_puts(char *str, uint32_t len)
{
	uint32_t i;
	for (i = 0; i < len; i++)
	{
		k_putch(str[i]);
	}
}

int32_t do_getc()
{
	return k_getc();
}

int32_t do_syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t retVal = -1;
	extern Task *cur_task;

	switch (syscallno)
	{
	case SYS_fork:
        retVal = sys_fork();
		break;

	case SYS_getc:
		retVal = do_getc();
		break;

	case SYS_puts:
		do_puts((char*)a1, a2);
		retVal = 0;
		break;

	case SYS_getpid:
        retVal = cur_task->task_id;
		break;

	case SYS_getcid:
		/* Lab6: get current cpu's cid */
		retVal = thiscpu->cpu_id;
		break;

	case SYS_sleep:
        cur_task->state = TASK_SLEEP;
        cur_task->remind_ticks = a1;
        sched_yield();
		break;

    case SYS_kill:
        sys_kill(a1);
        break;

    case SYS_get_num_free_page:
        retVal = sys_get_num_free_page(); 
        break;

    case SYS_get_num_used_page:
        retVal = sys_get_num_used_page();
        break;

    case SYS_get_ticks:
        retVal = sys_get_ticks();
        break;

    case SYS_settextcolor:
        retVal = sys_settextcolor(a1, a2);
        break;

    case SYS_cls:
        retVal = sys_cls();
        break;

    }
    return retVal;
}

static void syscall_handler(struct Trapframe *tf)
{
    tf->tf_regs.reg_eax = do_syscall(
            tf->tf_regs.reg_eax,
            tf->tf_regs.reg_edx,
            tf->tf_regs.reg_ecx,
            tf->tf_regs.reg_ebx,
            tf->tf_regs.reg_edi,
            tf->tf_regs.reg_esi);
}

void syscall_init()
{
    extern void SYSCALL();
    register_handler(T_SYSCALL, syscall_handler, SYSCALL, 1, 3);
}

