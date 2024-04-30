#include <shm.h>
#include <linux/mm.h>
#include <unistd.h>
#include <errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>

struct struct_shmem shm_list[SHM_NUM] = {{0, 0, 0}};

// 获得一个空闲的物理页面
int sys_shmget(key_t key, size_t size)
{
    int i;
    unsigned long page;

    if (size > PAGE_SIZE)
    {
        errno = EINVAL;
        printk("shmget:The size connot be greater than the PAGE_SIZE!\r\n");
        return -1;
    }

    if (key == 0)
    {
        printk("shmget:key connot be 0!\r\n");
        return -1;
    }

    // 判断是否已经创建
    for (i = 0; i < SHM_NUM; i++)
    {
        if (shm_list[i].key == key)
            return i;
    }

    page = get_free_page(); // 申请内存页 get_free_page 中会将 mem_map 相应位置置为 1
    decrease_mem_map(page); // 需要将 mem_map 相应位置清零，因为在 sys_shmat 才会将申请的物理页和虚拟地址关联增加引用次数
    if (!page)
    {
        errno = ENOMEM;
        printk("shmget:connot get free page!\r\n");
        return -1;
    }

    for (i = 0; i < SHM_NUM; i++)
    {
        if (shm_list[i].key == 0)
        {
            shm_list[i].size = size;
            shm_list[i].key = key;
            shm_list[i].page = page;
            break;
        }
    }
    return i;
}

// 将这个页面和进程的虚拟地址以及逻辑地址关联起来，让进程对某个逻辑地址的读写就是在读写该内存页
void *sys_shmat(int shmid)
{
    unsigned long tmp; // 虚拟地址
    unsigned long logicalAddr;
    if (shmid < 0 || shmid >= SHM_NUM || shm_list[shmid].page == 0 || shm_list[shmid].key <= 0)
    {
        errno = EINVAL;
        printk("shmat:The shmid id invalid!\r\n");
        return NULL;
    }
    tmp = get_base(current->ldt[1]) + current->brk; // 计算虚拟地址
    // 需要增加一次共享物理页的引用次数，否则会在 free_page 中 panic 死机
    // 特别注意：这里我们增加了物理页面的计数，如果是2，则在put_page中会打印
    // 一个“mem_map disagrees with %p at %p”这条信息，不知道为什么它要
    // 判断是否等于1来打印这个消息 这里我自己修改这个逻辑
    increase_mem_map(shm_list[shmid].page);         
    put_page(shm_list[shmid].page, tmp);
    logicalAddr = current->brk; // 记录逻辑地址
    current->brk += PAGE_SIZE;  // 更新brk指针
    return (void *)logicalAddr;
}