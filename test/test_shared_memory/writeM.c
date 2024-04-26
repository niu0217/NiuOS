/* ************************************************************************
> File Name:     writeM.c
> Author:        niu0217
> Created Time:  Fri 26 Apr 2024 10:30:15 AM CST
> Description:   
 ************************************************************************/

#define __LIBRARY__
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/sched.h>
#include <linux/kernel.h>

_syscall1(int, shmat, int, shmid);
_syscall2(int, shmget, unsigned int, key, size_t, size);

#define BUFFER_SIZE 10  /* 缓冲区大小 */
#define SHM_KEY 2018

int main()
{
    int i;
    int shm_id;
    int *p = NULL;
    if((shm_id = shmget(SHM_KEY, BUFFER_SIZE*sizeof(int))) < 0)
        printf("shmget failed!");

    if((p = (int * )shmat(shm_id)) < 0)
        printf("shmat error!");

    for (i = 0; i < BUFFER_SIZE; i++)
    {
        p[i] = i + 1;
    }
    while(1)
    {
        
    }
}

