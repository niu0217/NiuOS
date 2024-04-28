#define __LIBRARY__
#include <unistd.h>
#include <linux/sem.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/sched.h>
#include <linux/kernel.h>
 
_syscall2(sem_t *,sem_open,const char *,name,unsigned int,value)
_syscall1(int,sem_wait,sem_t *,sem)
_syscall1(int,sem_post,sem_t *,sem)
_syscall1(int,sem_unlink,const char *,name)
 
_syscall1(int, shmat, int, shmid)
_syscall2(int, shmget, unsigned int, key, size_t, size)
 
#define PRODUCE_NUM 200 /* 打出数字总数*/
#define BUFFER_SIZE 10  /* 缓冲区大小 */
#define SHM_KEY 2018
 
sem_t *empty;
sem_t *full;
sem_t *mutex;

int main(int argc, char* argv[])
{
    int i, shm_id, location=0;
    int *p;

    empty = sem_open("EMPTY", BUFFER_SIZE);
    full = sem_open("FULL", 0);
    mutex = sem_open("MUTEX", 1);

    if((shm_id = shmget(SHM_KEY, BUFFER_SIZE*sizeof(int))) < 0)
        printf("shmget failed!");    
 
    if((p = (int * )shmat(shm_id)) < 0)
        printf("shmat error!");
 
	printf("producer start.\n");
	fflush(stdout);
 
    for(i=0; i<PRODUCE_NUM; i++)
    {
        sem_wait(empty);
        sem_wait(mutex);

        p[location] = i;
 
        printf("pid %d:\tproducer produces item %d\n", getpid(), p[location]);
        fflush(stdout);

        sem_post(mutex);
        sem_post(full);
        location  = (location+1) % BUFFER_SIZE;
    }
 
	printf("producer end.\n");
	fflush(stdout);
 
    /* 释放信号量 */
    sem_unlink("FULL");
    sem_unlink("EMPTY");
    sem_unlink("MUTEX");
 
    return 0;    
}