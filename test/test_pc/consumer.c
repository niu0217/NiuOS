/* consumer.c */
 
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
 
_syscall1(int, shmat, int, shmid);
_syscall2(int, shmget, unsigned int, key, size_t, size);
 
#define PRODUCE_NUM 200
#define BUFFER_SIZE 10
#define SHM_KEY 2018
 
sem_t *Empty,*Full,*Mutex;
 
int main(int argc, char* argv[])
{
    int used = 0, shm_id,location = 0;
    int *p;
 
    Empty = sem_open("Empty", BUFFER_SIZE);
    Full = sem_open("Full", 0);
    Mutex = sem_open("Mutex", 1);
 
    if((shm_id = shmget(SHM_KEY, BUFFER_SIZE*sizeof(int))) < 0)
        printf("shmget failed!\n");    
 
    if((p = (int * )shmat(shm_id)) < 0)
        printf("link error!\n");
 
	printf("consumer start.\n");
	fflush(stdout);
 
    while(1)
    {
        sem_wait(Full);
        sem_wait(Mutex);
 
        printf("pid %d:\tconsumer consumes item %d\n", getpid(), p[location]);
        fflush(stdout);
 
        sem_post(Mutex);     
        sem_post(Empty);
        location  = (location+1) % BUFFER_SIZE;
 
        if(++used == PRODUCE_NUM)
            break;
    }
 
	printf("consumer end.\n");
	fflush(stdout);
 
    /* 释放信号量 */
    sem_unlink("Mutex");
    sem_unlink("Full");
    sem_unlink("Empty");
 
    return 0;    
}