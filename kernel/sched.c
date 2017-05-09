#include <kernel/task.h>
#include <kernel/cpu.h>
#include <inc/x86.h>

#define ctx_switch(ts) \
  do { env_pop_tf(&((ts)->tf)); } while(0)

//
// Lab6
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
void sched_yield(void) {
    extern Task tasks[];

    int index = thiscpu->cpu_task ? thiscpu->cpu_rq.index: 0;
    int i;
    for ( i = 0 ; i < thiscpu->cpu_rq.ntasks; ++i ) {
        index = (index + 1) % thiscpu->cpu_rq.ntasks;

        int pid = thiscpu->cpu_rq.tasks[index];
        if ( tasks[pid].state == TASK_RUNNABLE ) {
            thiscpu->cpu_task = &(tasks[pid]);
            thiscpu->cpu_task->state = TASK_RUNNING;
            thiscpu->cpu_task->remind_ticks = TIME_QUANT;
            thiscpu->cpu_rq.index = index;
            lcr3(PADDR(thiscpu->cpu_task->pgdir));
            ctx_switch(thiscpu->cpu_task);
        }
    }
}
