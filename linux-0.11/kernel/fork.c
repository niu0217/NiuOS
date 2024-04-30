/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

// 写页面验证。若页面不可写，则复制页面
extern void write_verify(unsigned long address);
extern long first_return_from_kernel(void);

long last_pid = 0; // 最新进程号，其值由find_empty_process()生成

// 进程空间区域写前验证函数
// 该函数对当前进程逻辑地址从addr到addr+size这一段范围以页为单位执行写操作前的检测操作。
// 由于检测判断是以页面为单位进行操作，因此程序首先需要找出addr所载页面开始地址start，
// 然后start加上进程数据段基址，使这个start变换成CPU 4G线性地址空间中的地址。
// 最后循环调用write_verify对指定大小的内存空间进行写前验证。若页面是只读的，则
// 执行共享检验和复制页面操作（写时复制）
void verify_area(void * addr,int size)
{
	unsigned long start;

	// 首先将起始地址start调整为其所在页的左边界开始位置，同时相应地调整验证区域大小
	// start & 0xfff用于获得指定起始位置addr（也就是start）在所在页面中的偏移值。
	// 原验证范围size加上这个偏移值即扩展成addr所在页面起始位置开始的范围值。
	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;	// 此时start是当前进程空间中的逻辑地址
	// 把start加上进程数据段在线性地址空间中的起始基址，变成系统整个线性空间中的
	// 地址位置
	start += get_base(current->ldt[2]);
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

// 复制内存页表
// 参数nr是新任务号；p是新任务数据结构指针。
// 该函数为新任务在线性地址空间中设置代码段和数据段基址、限长，并复制页表。由于Linux
// 系统采用了写时复制技术，因此这里仅为新进程设置自己的页目录项和页表项，而没有实际为
// 新进程分配物理内存页面。此时新进程与其父进程共享所有内存页面。
// 操作成功返回0，失败返回出错号
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	// 首先取当前进程局部描述符中代码段和数据段描述符项中的段限长（字节数）
	// 0x0f是代码段选择符；0x17是数据段选择符；
	// 然后取当前进程代码段和数据段在线性地址空间中的基地址。由于Linux0.11内核还不支持
	// 代码和数据段分立的情况，因此这里需要检查代码段和数据段基址和限长是否都分别相同
	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	// 然后设置创建中的新进程在线性地址空间中的基地址等于（64MB*其任务号），并用该值
	// 设置新进程局部描述符表中段描述符中的基地址。接着设置新进程的页目录项和页表项，即
	// 复制当前进程（父进程）的页目录项和页表项。此时子进程共享父进程的内存页面。
	// 正常情况下copy_page_tables返回0，否则表示出错，则释放刚申请的页表项
	new_data_base = new_code_base = nr * 0x4000000;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		printk("free_page_tables: from copy_mem\n");
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
// 复制进程
// 参数nr是调用find_empty_process()分配的任务数组项号
// 执行copy_process前的内核栈的样子
/*
+----------------------------------------------------------+
| # push by hardware                                       |
| SS                                                       |
| ESP                                                      |
| EFLAGS                                                   |
| CS                                                       |
| EIP                                                      |
+----------------------------------------------------------+
| # push in `system_call`                                  |
| DS                                                       |
| ES                                                       |
| FS                                                       |
| EDX                                                      |
| ECX                                                      |
| EBX # EDX, ECX and EBX as parameters to `system_call`    |
| &(pushl %eax) # push by `call sys_call_table(,%eax,4)`   |
+----------------------------------------------------------+
| # push in `sys_fork`                                     |
| GS                                                       |
| ESI                                                      |
| EDI                                                      |
| EBP                                                      |
| EAX # return value of `find_empty_process`                |
| &(addl $20,%esp) # push by `copy_process`                |
+----------------------------------------------------------+
*/
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	// 首先为新任务数据结构分配内存。如果分配内存错误，则返回出错码并退出。然后将新任务结构
	// 指针放入任务数组的nr项中。其中nr为任务号，由前面的find_empty_process()返回。
	// 接着把当前进程任务结构内容复制到刚申请的内存页面p开始处
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	task[nr] = p;
	// 这样做不会复制超级用户堆栈（只复制进程结构）
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid; // 新进程号，其值由ind_empty_process()返回
	p->father = current->pid;	// 设置父进程号
	p->counter = p->priority;	// 运行时间片值
	p->signal = 0;	// 信号位图置0
	p->alarm = 0;	// 报警定时值（滴答数）
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;	// 用户态时间和核心态运行时间
	p->cutime = p->cstime = 0;	// 子进程用户态和核心态运行时间
	p->start_time = jiffies;	// 进程开始运行时间（当前时间滴答数）

	fprintk(3, "%ld\t%c\t%ld\t%s\n", p->pid, 'N', jiffies, "copy_process");

	// 如果当前任务使用了协处理器，就保存其上下文
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	// 复制进程页表
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}

	// 初始化内核栈
	// 内核栈（初始值）放在与PCB相同物理页的页面顶端
	// 进程刚从用户态进入内核态时，内核栈的值总是空的，内核栈的大小为4096-sizeof(task_struct)
	// 用户栈的大小通常为几MB，堆则可以到几GB
	long *krnstack;
	krnstack = (long)(PAGE_SIZE + (long)p); // krnstack保存的是子进程的内核栈位置

	// iret指令的出栈数据
	// ss:esp指向用户栈 ss表示栈基地址 esp表示栈顶地址
	// cs:eip指向将要执行的用户态指令位置 cs指向代码段地址 eip是相对代码段地址的偏移地址
	// 如果是系统调用的话 int 0x80; __res=eax;  cs:eip指向的就是__res=eax这条指令
	// 也就是从系统调用返回后（内核态返回到用户态）执行的第一条指令
	*(--krnstack) = ss & 0xffff;
	*(--krnstack) = esp;
	*(--krnstack) = eflags;
	*(--krnstack) = cs & 0xffff;
	*(--krnstack) = eip;

	// “内核级线程切换五段论”中的最后一段切换，即完成用户栈和用户代码的切换
	// 依靠的核心指令就是 iret，回到用户态程序，当然在切换之前应该恢复一下执行现场，
	// 主要就是 eax,ebx,ecx,edx,esi,edi,gs,fs,es,ds 等寄存器的恢复.
	*(--krnstack) = ds & 0xffff;
	*(--krnstack) = es & 0xffff;
	*(--krnstack) = fs & 0xffff;
	*(--krnstack) = gs & 0xffff;
	*(--krnstack) = esi;
	*(--krnstack) = edi;
	*(--krnstack) = edx;

	// 处理 switch_to 返回，即结束后 ret 指令要用到的，ret 指令默认弹出一个 EIP 操作
	*(--krnstack) = (long)first_return_from_kernel;

	// swtich_to 函数中的 “切换内核栈” 后的弹栈操作
	*(--krnstack) = ebp;
	*(--krnstack) = ecx;
	*(--krnstack) = ebx;
	*(--krnstack) = 0;	// 子进程的返回值eax=0

	p->kernelstack = krnstack; // 将存放在 PCB 中的内核栈指针修改到初始化完成时内核栈的栈顶


	// 如果父进程中有文件是打开的，则将对应文件的打开次数加1。因为这里创建的子进程会与
	// 父进程共享这些打开的文件。将当前进程（父进程）的pwd、root和executable引用次数
	// 也加1，一样的道理。
	for (i=0; i<NR_OPEN;i++)
		if ((f=p->filp[i]))
			f->f_count++;
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	// 在GDT表中设置新任务TSS段和LDT段描述符项
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */

	fprintk(3, "%ld\t%c\t%ld\t%s\n", p->pid, 'J', jiffies, "copy_process");

	return last_pid;
}

// 为新进程取得不重复的进程号last_pid。函数返回在任务数组中的任务号（数组项）
int find_empty_process(void)
{
	int i;

	// 首先获取新的进程号，如果last_pid增加1后超出进程号的正数表示范围，则重新
	// 从1开始使用pid号。然后在任务数组中搜索刚设置的pid号是否已经被任何任务使用，
	// 如果是则跳转到函数开始处重新获得一个pid号。接着在任务数组中为新任务寻找一个
	// 空闲项，并返回项号
	// last_pid是一个全局变量，不再返回，如果此时任务数组中的64项已经被全部占用，
	// 则返回出错码
	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)	// 任务0被排除在外
		if (!task[i])
			return i;
	return -EAGAIN;
}
