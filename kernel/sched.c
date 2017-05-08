#include <kernel/task.h>
#include <kernel/cpu.h>
#include <inc/x86.h>

#define ctx_switch(ts) \
  do { env_pop_tf(&((ts)->tf)); } while(0)

//
// TODO: Lab6
// Modify your Round-robin scheduler to fit the multi-core
// You should:
//
// 1. Design your Runqueue structure first (in kernel/task.c)
//
// 2. modify sys_fork() in kernel/task.c ( we dispatch a task
//    to cpu runqueue only when we call fork system call )
//
// 3. modify sys_kill() in kernel/task.c ( we remove a task
//    from cpu runqueue when we call kill_self system call
//
// 4. modify your scheduler so that each cpu will do scheduling
//    with its runqueue
//    
//    (cpu can only schedule tasks which in its runqueue!!) 
//    (do not schedule idle task if there are still another process can run)	
//
//
void sched_yield(void)
{
	extern Task tasks[];
	extern Task *cur_task;

    int pid;
    for ( pid = ((cur_task->task_id + 1) % NR_TASKS); // starts from next
          pid != cur_task->task_id;
          pid = (pid + 1) % NR_TASKS )
    {
        if ( tasks[pid].state == TASK_RUNNABLE )
            break;
    }

    cur_task = &tasks[pid];
    cur_task->state = TASK_RUNNING;
    cur_task->remind_ticks = TIME_QUANT;
    lcr3(PADDR(cur_task->pgdir));
    ctx_switch(cur_task);
}
