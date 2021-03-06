#include "type.h"
#include "proc.h"
#include "tty.h"
#include "fs.h"
#include "mm.h"
#include "exe.h"
#include "sysconst.h"
#include "protect.h"
#include "keyboard.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "global.h"
#include "irq.h"

#define	TASK_PRIORITY		15
#define USER_PROC_PRIORITY	5
#define N			10

int re_enter;

struct semaphore sem_mutex;
int array[N];

void proc_begin(); /* lib/klib.asm */


void kernel_main()
{
	print("\n------------kernel_main------------\n");
	
	re_enter = 0;
	
	ticks = 0;
	
	struct proc* p_proc = proc_table;
	struct task* p_task = task_table;
	
	uint8_t _DPL;	/* 描述符特权级 */
	uint16_t _RPL_MASK;	/* 请求特权级掩码 */
	uint32_t _eflags;
	
	/* initialize proc_table */
	
	init_prot();
	
	for (int i = 0; i < NR_PROCS; i++, p_proc++, p_task++)
	{
		if (i >= NR_NATIVE_PROCS + NR_TASKS)
		{
			p_proc->flag = FREE_SLOT;
			continue;
		}
		if (i < NR_NATIVE_PROCS)	/* 用户进程 ring3 */
		{
			_DPL = DPL_3;
			_RPL_MASK = 0xFFFF;
			_eflags = 0x202; /* IOPL = 0 (禁止用户进程的 I/O 操作), IF = 1, bit 2 is always 1 */
			p_proc->ticks = p_proc->priority = USER_PROC_PRIORITY;
		}
		else	/* 任务 ring1 */
		{
			_DPL = DPL_1;
			_RPL_MASK = 0xFFFD;
			_eflags = 0x1202; /* IOPL = 1, IF = 1, bit 2 is always 1 */
			p_proc->ticks = p_proc->priority = TASK_PRIORITY;
		}
		
		/* 初始化进程/任务的 LDT 描述符 */
		if (i == PID_INIT)
		{
			/* 0~1M */
			init_desc(&p_proc->LDT[INDEX_LDT_C * DESC_SIZE],  0, PROC_IMAGE_SIZE - 1, DA_C32 | _DPL);
			init_desc(&p_proc->LDT[INDEX_LDT_RW * DESC_SIZE], 0, PROC_IMAGE_SIZE - 1, DA_D32 | _DPL);
		}
		else
		{
			/* 0~4G */
			init_desc(&p_proc->LDT[INDEX_LDT_C * DESC_SIZE], 0, 0xFFFFF, DA_C32 | DA_G_4K | _DPL);
			init_desc(&p_proc->LDT[INDEX_LDT_RW * DESC_SIZE], 0, 0xFFFFF, DA_D32 | DA_G_4K | _DPL);
		}
		
		/* 仅初始化必要的寄存器 */
		p_proc->regs.gs = SELECTOR_VIDEO;
		p_proc->regs.fs = SELECTOR_LDT_FLATRW & _RPL_MASK;
		p_proc->regs.es = SELECTOR_LDT_FLATRW & _RPL_MASK;
		p_proc->regs.ds = SELECTOR_LDT_FLATRW & _RPL_MASK;
		p_proc->regs.eip = (uint32_t) p_task->task_entry;
		p_proc->regs.cs = SELECTOR_LDT_FLATC & _RPL_MASK;
		p_proc->regs.eflags = _eflags;
		p_proc->regs.esp = (uint32_t) p_task->task_stack + TASK_STACK_SIZE;
		p_proc->regs.ss = SELECTOR_LDT_FLATRW & _RPL_MASK;
		
		p_proc->pid = i;
		p_proc->pid_parent = NONE;
		p_proc->flag = 0;
		p_proc->p_msg = NULL;
		p_proc->pid_sendto = NONE;
		p_proc->pid_recvfrom = NONE;
		p_proc->has_int_msg = 0;
		
		init_send_queue(p_proc);
		
		memset(p_proc->filp, 0, sizeof(struct file_desc*) * NR_FILES);
	}
	
	init_clock();
	init_keyboard();
	
	p_current_proc = proc_table;
	proc_begin();
	
	while(1) {}
}

void sleep(int ms)
{
	int t = get_ticks();
	
	while (((get_ticks() - t) * 1000 / CLK_FREQ) < ms) {}
}


void Init()
{
	int pid;
	
	sleep(5000);

#if 0
	pid = fork();
	
	if (pid != 0) 
	{
		printf("\n{Init} parent %d running, child pid: %d", getpid(), pid);
		
		pid = fork();
		if (pid != 0)
		{
			printf("\n{Init} parent %d running, child pid: %d", getpid(), pid);
		}
		else
		{
			printf("\n{Init} child %d running, parent pid: %d", getpid(), getppid());
		}
	}
	else
	{
		printf("\n{Init} child %d running, parent pid: %d", getpid(), getppid());
	}
#endif

	for (;;) 
	{
	
	}
}

void TaskA()
{
	int i;
	
	printf("\n{Task-A}");
	
	sem_init(&sem_mutex, 1);

	sem_wait(&sem_mutex);
	printf("\nTask-A sem_wait succeeds\n");
	for (i = 0; i < N; i++)
	{
		array[i] = i + 1;
		sleep(1000);
	}
	sem_post(&sem_mutex);
	
	for (;;)
	{

	}
}

void TaskB()
{
	int i;
	
	printf("\n{Task-B}");
	
	sem_wait(&sem_mutex);
	printf("\nTask-B sem_wait succeeds\n");
	for (i = 0; i < N; i++)
	{
		printf("%d ", array[i]);
	}
	sem_post(&sem_mutex);
	printf("\n");

	int fd;
	char s[32];
	fd = open("/test1", O_CREAT | O_RDWR);
	write(fd, "testfile", 9);
	close(fd);
	
	fd = open("/test2", O_CREAT | O_RDWR);
	write(fd, "hello, world", 13);
	close(fd);

	fd = open("/test1", O_RDWR);
	read(fd, s, sizeof(s));
	close(fd);
	printf("\nTaskB file test1: %s",s);

	fd = open("/test2", O_RDWR);
	read(fd, s, sizeof(s));
	close(fd);
	printf("\nTaskB file test2: %s",s);

	for (;;)
	{
		
	}
}

void TaskC()
{
	printf("\n{Task-C}");
	
	void *la1, *la2, *la3, *la4, *la5;
	
	/* 11M ~ 13M */
	la1 = vm_alloc((void*)0xb00080, 0x200000, PAGE_READWRITE);
	if (la1)
	{
		printf("\n(1) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la1);
		memset(la1, 0xcd, 0x200000);
		memset(la1, 0x12, 8);
	}
	else
	{
		printf("\n(1) vm_alloc failed: laddr = 0x%.8x\n", 0xb00080);
	}

	/* 8M ~ 12M */
	la2 = vm_alloc((void*)0x800600, 0x400000, PAGE_READWRITE);
	if (la2)
	{
		printf("\n(2) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la2);
		memset(la2, 0xcc, 0x400000);
		memset(la2, 0x34, 8);
	}
	else
	{
		printf("\n(2) vm_alloc failed: laddr = 0x%.8x\n", 0x800600);
	}
	
	/* 28M ~ 28M + 7K */
	la3 = vm_alloc((void*)0x1c00000, 7 * 1024, PAGE_READWRITE);
	if (la3)
	{
		printf("\n(3) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la3);
		memset(la3, 0xdd, 4096 * 2);
		memset(la3, 0x56, 8);
	}
	else
	{
		printf("\n(3) vm_alloc failed: laddr = 0x%.8x\n", 0x1c00000);
	}
	
	/* 9M ~ 12M */
	la4 = vm_alloc((void*)0x900000, 0x300000, PAGE_READWRITE);
	if (la4)
	{
		printf("\n(4) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la4);
		memset(la4, 0xdd, 0x300000);
		memset(la4, 0x99, 8);
	}
	else
	{
		printf("\n(4) vm_alloc failed: laddr = 0x%.8x\n", 0x900000);
	}

	/* Free: 11M ~ 13M */
	vm_free(la1, 0x200000);

	/* Allocate: 8M ~ 12M */
	la2 = vm_alloc((void*)0x800600, 0x400000, PAGE_READWRITE);
	if (la2)
	{
		printf("\n(5) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la2);
		memset(la2, 0xcc, 0x400000);
		memset(la2, 0x34, 8);
	}
	else
	{
		printf("\n(5) vm_alloc failed: laddr = 0x%.8x\n", 0x800600);
	}

	/* Allocate: 9M ~ 12M */
	la4 = vm_alloc((void*)0x900000, 0x300000, PAGE_READWRITE);
	if (la4)
	{
		printf("\n(6) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la4);
		memset(la4, 0xdd, 0x300000);
	}
	else
	{
		printf("\n(6) vm_alloc failed: laddr = 0x%.8x\n", 0x900000);
	}

	/* Free: 8M ~ 12M */
	vm_free(la2, 0x400000);

	/* Allocate: 9M ~ 12M */
	la4 = vm_alloc((void*)0x900000, 0x300000, PAGE_READWRITE);
	if (la4)
	{
		printf("\n(7) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la4);
		memset(la4, 0xdd, 0x300000);
		memset(la4, 0x99, 8);
	}
	else
	{
		printf("\n(7) vm_alloc failed: laddr = 0x%.8x\n", 0x900000);
	}
	
	/* Allocate: 15M ~ 17M */
	la5 = vm_alloc((void*)0xF00000, 0x200000, PAGE_READWRITE);
	if (la5)
	{
		printf("\n(8) vm_alloc succeeded: laddr = 0x%.8x\n",(uint32_t)la5);
		memset(la5, 0xff, 0x200000);
		memset(la5, 0x00, 8);
	}
	else
	{
		printf("\n(8) vm_alloc failed: laddr = 0x%.8x\n", 0xF00000);
	}
	
	/* Test Page-Fault */
	*(int*)0x810C5F40 = 10;
	
	uint32_t *laddr = (uint32_t*) 0xF0017000;
	printf("\nTest Page-Fault, LA(0x%.8x) = 0x%.8x -> ",
		laddr, *laddr);
	
	*laddr = 5;
	printf("0x%.8x", *laddr);


	for (;;)
	{

	}
}

