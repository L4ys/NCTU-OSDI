#include <inc/mmu.h>
#include <inc/types.h>
#include <inc/string.h>
#include <inc/x86.h>
#include <inc/memlayout.h>
#include <kernel/task.h>
#include <kernel/mem.h>
#include <kernel/cpu.h>
#include <kernel/spinlock.h>

// Global descriptor table.
//
// Set up global descriptor table (GDT) with separate segments for
// kernel mode and user mode.  Segments serve many purposes on the x86.
// We don't use any of their memory-mapping capabilities, but we need
// them to switch privilege levels. 
//
// The kernel and user segments are identical except for the DPL.
// To load the SS register, the CPL must equal the DPL.  Thus,
// we must duplicate the segments for the user and the kernel.
//
// In particular, the last argument to the SEG macro used in the
// definition of gdt specifies the Descriptor Privilege Level (DPL)
// of that descriptor: 0 for kernel and 3 for user.
//
struct Segdesc gdt[NCPU + 5] =
{
	// 0x0 - unused (always faults -- for trapping NULL far pointers)
	SEG_NULL,

	// 0x8 - kernel code segment
	[GD_KT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 0),

	// 0x10 - kernel data segment
	[GD_KD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 0),

	// 0x18 - user code segment
	[GD_UT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 3),

	// 0x20 - user data segment
	[GD_UD >> 3] = SEG(STA_W , 0x0, 0xffffffff, 3),

	// First TSS descriptors (starting from GD_TSS0) are initialized
	// in task_init()
	[GD_TSS0 >> 3] = SEG_NULL
	
};

struct Pseudodesc gdt_pd = {
	sizeof(gdt) - 1, (unsigned long) gdt
};



Task tasks[NR_TASKS];

extern char bootstack[];

extern char UTEXT_start[], UTEXT_end[];
extern char UDATA_start[], UDATA_end[];
extern char UBSS_start[], UBSS_end[];
extern char URODATA_start[], URODATA_end[];
/* Initialized by task_init */
uint32_t UTEXT_SZ;
uint32_t UDATA_SZ;
uint32_t UBSS_SZ;
uint32_t URODATA_SZ;

struct spinlock task_lock;

extern void sched_yield(void);


/* 
 * 1. Find a free task structure for the new task,
 *    the global task list is in the array "tasks".
 *    You should find task that is in the state "TASK_FREE"
 *    If cannot find one, return -1.
 *
 * 2. Setup the page directory for the new task
 *
 * 3. Setup the user stack for the new task, you can use
 *    page_alloc() and page_insert(), noted that the va
 *    of user stack is started at USTACKTOP and grows down
 *    to USR_STACK_SIZE, remember that the permission of
 *    those pages should include PTE_U
 *
 * 4. Setup the Trapframe for the new task
 *    We've done this for you, please make sure you
 *    understand the code.
 *
 * 5. Setup the task related data structure
 *    You should fill in task_id, state, parent_id,
 *    and its schedule time quantum (remind_ticks).
 *
 * 6. Return the pid of the newly created task.
 
 */
int task_create()
{
    spin_lock(&task_lock);

	Task *ts = NULL;

	/* Find a free task structure */
    int pid;
    for ( pid = 0; pid < NR_TASKS; ++pid )
        if ( tasks[pid].state == TASK_FREE ) {
            ts = &tasks[pid];
            break;
        }

    if ( !ts ) {
        spin_unlock(&task_lock);
        return -1;
    }

  /* Setup Page Directory and pages for kernel*/
  if (!(ts->pgdir = setupkvm()))
    panic("Not enough memory for per process page directory!\n");

  /* Setup User Stack */
    int va;
    for ( va = USTACKTOP - USR_STACK_SIZE; va < USTACKTOP; va += PGSIZE ) {
        struct PageInfo* pp = page_alloc(ALLOC_ZERO);
        if ( !pp )
            return -1;
        if ( page_insert(ts->pgdir, pp, (void*)va, PTE_W | PTE_U) )
            return -1;
    }

	/* Setup Trapframe */
	memset( &(ts->tf), 0, sizeof(ts->tf));

	ts->tf.tf_cs = GD_UT | 0x03;
	ts->tf.tf_ds = GD_UD | 0x03;
	ts->tf.tf_es = GD_UD | 0x03;
	ts->tf.tf_ss = GD_UD | 0x03;
	ts->tf.tf_esp = USTACKTOP-PGSIZE;

	/* Setup task structure (task_id and parent_id) */
    ts->task_id = pid;
    ts->parent_id = thiscpu->cpu_task ? thiscpu->cpu_task->task_id:0;
    ts->remind_ticks = TIME_QUANT;
    ts->state = TASK_RUNNABLE;
    spin_unlock(&task_lock);
    return pid;
}


/*
 * This function free the memory allocated by kernel.
 *
 * 1. Be sure to change the page directory to kernel's page
 *    directory to avoid page fault when removing the page
 *    table entry.
 *    You can change the current directory with lcr3 provided
 *    in inc/x86.h
 *
 * 2. You have to remove pages of USER STACK
 *
 * 3. You have to remove pages of page table
 *
 * 4. You have to remove pages of page directory
 *
 * HINT: You can refer to page_remove, ptable_remove, and pgdir_remove
 */


static void task_free(int pid)
{
    lcr3(PADDR(kern_pgdir));
    // remove user stack
    int va;
    for ( va = USTACKTOP - USR_STACK_SIZE; va < USTACKTOP; va += PGSIZE )
        page_remove(tasks[pid].pgdir, (void*)va);

    ptable_remove(tasks[pid].pgdir);
    pgdir_remove(tasks[pid].pgdir);
}

// Lab6
//
// Modify it so that the task will be removed form cpu runqueue
// ( we not implement signal yet so do not try to kill process
// running on other cpu )
//
void sys_kill(int pid)
{
    if ( pid > 0 && pid < NR_TASKS && thiscpu->cpu_rq.ntasks ) {
        int i;
        for ( i = 0 ; i < thiscpu->cpu_rq.ntasks; i++ ) {
            if ( thiscpu->cpu_rq.tasks[i] == pid ) {
                memmove(&thiscpu->cpu_rq.tasks[i], &thiscpu->cpu_rq.tasks[i+1], (thiscpu->cpu_rq.ntasks - i - 1) * sizeof(int));
                --thiscpu->cpu_rq.ntasks;
                break;
            }
        }
        tasks[pid].state = TASK_FREE;
        task_free(pid);
        if ( thiscpu->cpu_task->task_id == pid ) {
            thiscpu->cpu_task = NULL;
            sched_yield();
        }
    }
}

/* 
 * In this function, you have several things todo
 *
 * 1. Use task_create() to create an empty task, return -1
 *    if cannot create a new one.
 *
 * 2. Copy the trap frame of the parent to the child
 *
 * 3. Copy the content of the old stack to the new one,
 *    you can use memcpy to do the job. Remember all the
 *    address you use should be virtual address.
 *
 * 4. Setup virtual memory mapping of the user prgram 
 *    in the new task's page table.
 *    According to linker script, you can determine where
 *    is the user program. We've done this part for you,
 *    but you should understand how it works.
 *
 * 5. The very important step is to let child and 
 *    parent be distinguishable!
 *
 * HINT: You should understand how system call return
 * it's return value.
 */

//
// Lab6
//
// Modify it so that the task will disptach to different cpu runqueue
// (please try to load balance, don't put all task into one cpu)
//
int sys_fork()
{
    /* pid for newly created process */
    int pid = task_create();
    if ( pid == -1 )
        return -1;

    if ((uint32_t)thiscpu->cpu_task)
    {
        Task* child = &tasks[pid];
        child->tf = thiscpu->cpu_task->tf;

        // Copy stack
		int va;
		for ( va = USTACKTOP - USR_STACK_SIZE; va < USTACKTOP; va += PGSIZE ) {
            pte_t* pte_src = pgdir_walk(thiscpu->cpu_task->pgdir, (void*)va, 0);
            pte_t* pte_dst = pgdir_walk(child->pgdir, (void*)va, 0);
            memcpy(KADDR(PTE_ADDR(*pte_dst)), KADDR(PTE_ADDR(*pte_src)), PGSIZE);
		}

        /* Step 4: All user program use the same code for now */
        setupvm(tasks[pid].pgdir, (uint32_t)UTEXT_start, UTEXT_SZ);
        setupvm(tasks[pid].pgdir, (uint32_t)UDATA_start, UDATA_SZ);
        setupvm(tasks[pid].pgdir, (uint32_t)UBSS_start, UBSS_SZ);
        setupvm(tasks[pid].pgdir, (uint32_t)URODATA_start, URODATA_SZ);

        child->tf.tf_regs.reg_eax = 0;
    }

    // Find cpu with least tasks
    int min = NR_TASKS * NCPU + 1;
    int cid = 0;
    int i;
    for ( i = 0; i < ncpu; ++i ) {
        if ( cpus[i].cpu_rq.ntasks < min ) {
            min = cpus[i].cpu_rq.ntasks;
            cid = i;
        }
    }

    cpus[cid].cpu_rq.tasks[cpus[cid].cpu_rq.ntasks++] = pid;

    return pid;
}

/*
 * We've done the initialization for you,
 * please make sure you understand the code.
 */
void task_init()
{
  extern int user_entry();
  spin_initlock(&task_lock);

  UTEXT_SZ = (uint32_t)(UTEXT_end - UTEXT_start);
  UDATA_SZ = (uint32_t)(UDATA_end - UDATA_start);
  UBSS_SZ = (uint32_t)(UBSS_end - UBSS_start);
  URODATA_SZ = (uint32_t)(URODATA_end - URODATA_start);

	/* Initial task sturcture */
	int i;
	for (i = 0; i < NR_TASKS; i++)
	{
		memset(&(tasks[i]), 0, sizeof(Task));
		tasks[i].state = TASK_FREE;

	}
	task_init_percpu();
}

// Lab6
//
// Please modify this function to:
//
// 1. init idle task for non-booting AP 
//    (remember to put the task in cpu runqueue) 
//
// 2. init per-CPU Runqueue
//
// 3. init per-CPU system registers
//
// 4. init per-CPU TSS
//

void task_init_percpu()
{
	int i;
	extern int user_entry();
	extern int idle_entry();
	
	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	memset(&(thiscpu->cpu_tss), 0, sizeof(thiscpu->cpu_tss));
    thiscpu->cpu_tss.ts_esp0 = (uint32_t)percpu_kstacks[thiscpu->cpu_id] + KSTKSIZE;
	thiscpu->cpu_tss.ts_ss0 = GD_KD;

	// fs and gs stay in user data segment
	thiscpu->cpu_tss.ts_fs = GD_UD | 0x03;
	thiscpu->cpu_tss.ts_gs = GD_UD | 0x03;

	/* Setup TSS in GDT */
	gdt[(GD_TSS0 >> 3) + thiscpu->cpu_id] = SEG16(STS_T32A, (uint32_t)(&thiscpu->cpu_tss), sizeof(struct tss_struct), 0);
	gdt[(GD_TSS0 >> 3) + thiscpu->cpu_id].sd_s = 0;

	/* Setup first task */
	i = task_create();
	thiscpu->cpu_task = &(tasks[i]);

	/* For user program */
	setupvm(thiscpu->cpu_task->pgdir, (uint32_t)UTEXT_start, UTEXT_SZ);
	setupvm(thiscpu->cpu_task->pgdir, (uint32_t)UDATA_start, UDATA_SZ);
	setupvm(thiscpu->cpu_task->pgdir, (uint32_t)UBSS_start, UBSS_SZ);
	setupvm(thiscpu->cpu_task->pgdir, (uint32_t)URODATA_start, URODATA_SZ);
	thiscpu->cpu_task->tf.tf_eip = (uint32_t)(thiscpu->cpu_id == bootcpu->cpu_id ? user_entry:idle_entry);

    thiscpu->cpu_rq.tasks[0] = i;
    thiscpu->cpu_rq.index = 0;
    thiscpu->cpu_rq.ntasks = 1;

	/* Load GDT&LDT */
	lgdt(&gdt_pd);


	lldt(0);

	// Load the TSS selector 
	ltr(GD_TSS0 + (thiscpu->cpu_id << 3));

	thiscpu->cpu_task->state = TASK_RUNNING;
}
