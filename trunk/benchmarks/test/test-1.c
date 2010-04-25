#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <inttypes.h>
#include <sys/syscall.h>

#define N 2 
#define M 10000
pthread_t thd[N];
pthread_mutex_t mutex;
int global;

// Return the current tid for the calling thread.
static pid_t get_current_tid()
{
  return syscall(SYS_gettid);
}

void foo(int*p) {
    int j = 0;
    int i = 0;
    int sum = 0;
    int tid = get_current_tid();
    int *q = (int*)malloc(1000);
    for (j = 0; j < 6000; j++) {
       if(j%100 == 0)
         pthread_mutex_lock(&mutex);
       sum = sum + j;
       sum = sum + j;
       sum = sum + j;
       sum = sum + j;
       sum = sum + j;
       sum = sum + j; 
       sum = sum + j;
       if (j%100== 0) 
         pthread_mutex_unlock(&mutex);
    }
   free(q);
}

void foo_2(int *p) {
  int i = 0;
  for(i = 0; i< M; i++) {
    foo(p);
    *p = 1;
  }
}


void foo_1(int *p) {
  foo_2(p);
}
 

void foo_0(int *p) {
  foo_1(p);
  pthread_exit(0);
}

void bar(int *p) {
    int j = 0;
    int i = 0;
    int sum = 0;
    int tid = get_current_tid();
    int* q = (int*)malloc(1000);
    for (j = 0; j < 20000; j++) {
       sum = sum + j;
       sum = sum + j;
       sum = sum + j;
       sum = sum + j;
       sum = sum + j;
       sum = sum + j; 
       sum = sum + j; 
    }
   free(q);
}

void bar_2(int *p) {
  int i = 0;
  for(i = 0; i< M; i++) {
    bar(p);
    *p = 1;
  }
}

void bar_1(int *p) {
  bar_2(p);
}

void bar_0(int *p) {
 bar_1(p);
 pthread_exit(0);
}

int main() {
  int i;
  int rv;
  racez_start();
  printf("the address of global is %p\n", &global);
  for(i = 0; i < N; i++) {
    if(i%N == 0) {
      rv = pthread_create(&thd[i], NULL, foo_0, (void*)&global);
    } else {
      rv = pthread_create(&thd[i], NULL, bar_0, (void*)&global);
    }
    if(rv != 0) {
      printf("pthread_create failed!\n");
      exit(0);
    }
  }
  for(i = 0; i < N; i++) {
    rv = pthread_join(thd[i], NULL);
    if( rv != 0) {
      printf("pthread_join failed\n");
      exit(0);
    }
  }
  racez_stop();
  return 0;
}

