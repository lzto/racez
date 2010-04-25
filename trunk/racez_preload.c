/*
 TODO(tianwei): Copyright info
*/
#include "racez_mutex.h"
#include "racez_bufferdata.h"
#include "racez_lockset.h"
#include "racez_pmuprofiler.h"

#include <dlfcn.h>
#include <pthread.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static pthread_once_t initializer = PTHREAD_ONCE_INIT;
static pthread_key_t pkey;
typedef struct start_func {
   void *(*start_func_)(void*);
   void *arg_;  
}START_FUNC;

// TODO(tianweis): How to protect this racez_enable variable.
int racez_enable = 0;
int racez_lockset = 1;
int racez_signal_wait = 0;
int racez_memory_allocator = 0;
int racez_stacktrace = 0;
int racez_sampling_adjust = 0;
int racez_sample_skid = 0;
int racez_signal_sample = 0;
int racez_write_record = 0;

// pthread func pointers
int (*real_pthread_mutex_lock) (pthread_mutex_t *__mutex) = NULL;
int (*real_pthread_mutex_trylock) (pthread_mutex_t *__mutex) = NULL;
int (*real_pthread_mutex_unlock) (pthread_mutex_t *__mutex) = NULL;
int (*real_pthread_create)(pthread_t *__restrict __newthread,
                           __const pthread_attr_t *__restrict __attr,
                           void *(*__start_routine) (void *),
                           void *__restrict __arg) = NULL;
void* (*real_malloc) (size_t size) = NULL;
void  (*real_free) (void* p) = NULL;
int (*real_pthread_cond_signal) (pthread_cond_t*) = NULL;
int  (*real_pthread_cond_wait) (pthread_cond_t*, pthread_mutex_t*) = NULL;

// called when the library exits
//int racez_stop() __attribute__((destructor));
void preload_start() __attribute__((constructor));

static pid_t get_current_tid()
{
  return syscall(SYS_gettid);
}

/* pthread_once function */
void preload_init() {

  // By default, racez_lockset is turned on, you can turn off it by set the
  // corresponding environment variable .
  racez_lockset =
    getenv("RACEZ_LOCKSET") ? atoi(getenv("RACEZ_LOCKSET")) : 1;
  // By default, signal/wait and memory allocator are turnned off.
  racez_signal_wait =
    getenv("RACEZ_SIGNAL_WAIT") ? atoi(getenv("RACEZ_SIGNAL_WAIT")) : 0;
  racez_memory_allocator =
    getenv("RACEZ_MEMORY_ALLOCATOR") ? atoi(getenv("RACEZ_MEMORY_ALLOCATOR")) : 0;

  racez_stacktrace =
    getenv("RACEZ_STACKTRACE") ? atoi(getenv("RACEZ_STACKTRACE")) : 0;
  // By default, signal/wait and memory allocator are turnned off.
  racez_sampling_adjust =
    getenv("RACEZ_SAMPLING_ADJUST") ? atoi(getenv("RACEZ_SAMPLING_ADJUST")) : 0;
  racez_sample_skid =
    getenv("RACEZ_SAMPLE_SKID") ? atoi(getenv("RACEZ_SAMPLE_SKID")) : 0;
  racez_signal_sample =
    getenv("RACEZ_SIGNAL_SAMPLE") ? atoi(getenv("RACEZ_SIGNAL_SAMPLE")) : 0;
  racez_write_record =
    getenv("RACEZ_WRITE_RECORD") ? atoi(getenv("RACEZ_WRITE_RECORD")) : 0;
  
  printf("the setting is: racez_lockset=%d, racez_signal_wait=%d, racez_memory_allocator=%d, racez_stacktrace=%d,racez_sampling_adjust=%d, racez_sample_skid=%d,racez_signal_sample=%d, racez_write_record=%d\n",
         racez_lockset, racez_signal_wait,
         racez_memory_allocator, racez_stacktrace, racez_sampling_adjust,
         racez_sample_skid, racez_signal_sample, racez_write_record);
  real_pthread_create = dlsym(RTLD_NEXT, "pthread_create");

  if(racez_lockset) {
     real_pthread_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
     real_pthread_mutex_trylock = dlsym(RTLD_NEXT, "pthread_mutex_trylock");
     real_pthread_mutex_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
   }

   if(racez_signal_wait) {
     real_pthread_cond_signal = dlsym(RTLD_NEXT, "pthread_cond_signal");
     real_pthread_cond_wait = dlsym(RTLD_NEXT, "pthread_cond_wait");
   }
   if(racez_memory_allocator) {
     real_malloc = dlsym(RTLD_NEXT, "malloc");
     real_free = dlsym(RTLD_NEXT, "free");
   }
    pthread_key_create(&pkey, NULL);
}

/* destructor function */
void preload_done() {
	pthread_key_delete(pkey);
}


/* ########################## PTHREAD API ################################ */

int pthread_mutex_lock(pthread_mutex_t *m) {
	pthread_once(&initializer, preload_init);
   if(racez_lockset) {
      //printf("we are in user-define pthread_mutex_lock\n");
      add_one_mutex(m);
   } else if(real_pthread_mutex_lock == NULL) 
     real_pthread_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");

   assert(real_pthread_mutex_lock != NULL);

   int r = real_pthread_mutex_lock(m);
   return r;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
  pthread_once(&initializer, preload_init);
  if(real_pthread_mutex_unlock == NULL)
    real_pthread_mutex_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
  int r = real_pthread_mutex_unlock(m);
  if (racez_lockset && racez_enable) {
    //printf("we are in user-define pthread_mutex_unlock\n");  
    remove_one_mutex(m);
  }
  return r;
}
/*
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  pthread_once(&initializer, preload_init);

  if(real_pthread_cond_wait == NULL) {
      real_pthread_cond_wait = dlsym(RTLD_NEXT, "pthread_cond_wait");
  }
  int result = real_pthread_cond_wait(cond, mutex);
  assert(result == 0);
  if(racez_signal_wait && racez_enable) {
    struct Record record;
    memset(&record, 0, sizeof(record));	
    record.thread_id_ = get_current_tid();
    record.type_ = SYNC_WAIT;
    record.op_.wait_op_.wait_address1_ = cond;
    record.op_.wait_op_.wait_address2_ = mutex;
    WriteRecord(&record);
  } 
  return result;
}

int pthread_cond_signal(pthread_cond_t *cond) {
  pthread_once(&initializer, preload_init);
  if(real_pthread_cond_signal == NULL) {
      real_pthread_cond_signal = dlsym(RTLD_NEXT, "pthread_cond_signal");
  }
  int result = real_pthread_cond_signal(cond);
  assert(result == 0);
  if(racez_signal_wait && racez_enable) {
    struct Record record;
    memset(&record, 0, sizeof(record));	
    record.thread_id_ = get_current_tid();
    record.type_ = SYNC_SIGNAL;
    record.op_.signal_op_.signal_address_ = cond;
    WriteRecord(&record); 
  }
  return result;
}
*/

void* malloc(size_t size) {
  pthread_once(&initializer, preload_init);
  //TODO(tianweis): Is there a race?  for example, how about
  // multithreads enter enter this malloc simultaneously, i.e,
  // more than 1 threads will be in dlsym.
  if(real_malloc == NULL) {
      real_malloc = dlsym(RTLD_NEXT, "malloc");
  }

  void* result = real_malloc(size);

  // TODO(tianweis): Is there a race? 
  // check if racez_enable is synched correctly.
  // For example, if racez_stop is called, the buffer is
  // no longer valide, we should not write.
  if(racez_memory_allocator && racez_enable) {
    struct Record record;
    memset(&record, 0, sizeof(record));	
    record.thread_id_ = get_current_tid();
    record.type_ = MEMORY_ALLOC;
    record.op_.mem_alloc_op_.start_address_ = result;
    record.op_.mem_alloc_op_.size_ = size;
    record.op_.mem_alloc_op_.is_alloc_ = 1;
    WriteRecord(&record); 
  }
  return result;
}

void free(void* p) {
  pthread_once(&initializer, preload_init);
  if(real_free == NULL)
    real_free = dlsym(RTLD_NEXT, "free");
  real_free(p);
  if(racez_memory_allocator && racez_enable) {
    struct Record record;
    memset(&record, 0, sizeof(record));
    record.thread_id_ = get_current_tid();
    record.type_ = MEMORY_ALLOC;
    record.op_.mem_alloc_op_.start_address_ = p;
    record.op_.mem_alloc_op_.size_ = 0;
    record.op_.mem_alloc_op_.is_alloc_ = 0;
    WriteRecord(&record);
  }
}

static void* racez_thread_start_func(void* arg) {
  START_FUNC *orig_start = (START_FUNC*)arg;
  if(racez_enable) {
    pmu_profiler_perthread_attach();
  }
  orig_start->start_func_(orig_start->arg_);
  return ;
}

int
pthread_create(pthread_t *newthread, const pthread_attr_t *attr, 
               void *(*start_func)(void*), void* arg) {
  pthread_once(&initializer, preload_init);
  //printf("we are in user-define pthread_create function\n");
  // TODO(tianweis:) have a better design to free the memory,
  // Note that you can not use local variables here.
  START_FUNC *orig_start = (START_FUNC*)malloc(sizeof(START_FUNC));
  orig_start->start_func_ = start_func;
  orig_start->arg_ = arg; 
  int r = real_pthread_create(newthread, attr, 
                         racez_thread_start_func, (void*)orig_start);
  return r;
}

void racez_log_memory_allocation(void* start_address, int size, int is_alloc) {
  if(racez_memory_allocator && racez_enable) {
    struct Record record;
    memset(&record, 0, sizeof(record));
    record.thread_id_ = get_current_tid();
    record.type_ = MEMORY_ALLOC;
    record.op_.mem_alloc_op_.start_address_ = start_address;
    record.op_.mem_alloc_op_.size_ = size;
    record.op_.mem_alloc_op_.is_alloc_ = is_alloc;
    WriteRecord(&record);
  }
}

void debug_mutex() {
  LOCKSET *lockset = GetPerThreadLockset();
  if (lockset != NULL) {
    if(lockset->n >=1)
      assert(0);
  }
}
// These two function need to be registered at the begging since malloc 
// may be used by pthread_once.
void preload_start() {
}

int racez_start() {
  pthread_once(&initializer, preload_init);
  //printf("we are in pmu_profile_start function in a separated library\n");
  if(pmu_profiler_start()) {
    assert(racez_enable == 0);
    racez_enable = 1;
  }
  return 0;
}

int racez_stop() {
  //printf("we are in pmu_profile_stop function in a separated library\n");
  //We should set the racez_enable = 0 first, then stop.
  assert(racez_enable == 1);
  racez_enable = 0;
  pmu_profiler_stop();
  return 0;
}
