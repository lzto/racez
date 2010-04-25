// TODO(tianwei): Copyright information
#include "racez_mutex.h"
#include "racez_lockset.h"
#include <stdio.h>
#include <assert.h>

extern int racez_sample_skid;

void add_one_mutex(pthread_mutex_t *mutex) 
{
  int sum = 0;
  if(racez_sample_skid) {                                                                                         
    for(int i = 0; i < 2500; i++)                                                                     
      sum += i;                                                                                       
  }

  LOCKSET* lockset = GetPerThreadLockset();
  // The only reason lockset is NULl because the key is not created yet
  if(lockset == NULL) {
    Create_Tls_Key();
    SetPerThreadLockset();
    lockset = GetPerThreadLockset();
  }
  if(lockset->overflow == false) {
    //printf("add a lock %p\n", (void*)mutex);
    lockset->n++;
    // add the mutex at the end of lockset
    // NOTE that, the order is very important since the signal-handler
    // use libunwind library where it also will call pthread_mutex_lock/unlock
    // look at the following call seq:
    // a. user code: lockset_[0] = mutex
    // b. user code interrupted before n is increment
    // c. signal-handler code add_one_mutex, lockset_[0] = signal_mutex
    // d. signal-handler code remove_one_mutex, lockset->n=0
    // e. user code get the context, update the lockset->n=1
    // but now lockset->lockset_[0] is the signal_mutex, not the user code's mutex.
    // If we update the lockset->n++, we can workaround this problem. 
    // since the signal-handler is atomic, which means it will execute the lock/unlock together.
    lockset->lockset_[lockset->n-1] = (void*)mutex;
    // 0 is the first element and 9 is last element.
    if (lockset->n == 10) {
      lockset->overflow = true;
    }
  }
}

void remove_one_mutex(pthread_mutex_t *mutex)
{
  int sum = 0;
  if(racez_sample_skid) {                                                                                         
    for(int i = 0; i < 5000; i++)                                                                     
      sum += i;                                                                                       
  }
  LOCKSET* lockset = GetPerThreadLockset();
  // The only reason lockset is NULl because the key is not created yet
  if(lockset == NULL) {
    Create_Tls_Key();
    SetPerThreadLockset();
    lockset = GetPerThreadLockset();
  }
  int lock_count = lockset->n;
  for (int i = 0; i < lock_count; i++) {
    if(lockset->lockset_[i] == (void*)mutex) {
      //printf("remove a lock %p\n", (void*)mutex);
      lockset->n--;
      // Same as add_one_mutex, we need to update lockset->n-- first.
      lockset->lockset_[i] = lockset->lockset_[lock_count - 1];
      if (lockset->overflow == true)
        lockset->overflow = false;
      // We are unlikely to get the two same mutexes, so exit here.
      break;
    }
  }
}


