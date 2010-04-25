//TODO(tianwei): Copyright info
#include "racez_lockset.h"
#include <pthread.h>
#include <stdlib.h>

pthread_key_t   Tls_Lockset_Key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static void make_key()
{
  (void) pthread_key_create(&Tls_Lockset_Key, NULL);
}

// This function is not signal-safe, it only can be called inside of non-signal-handler
// code.
void Create_Tls_Key() {
  // TODO(tianweis): set the destructor.
  pthread_once(&key_once, make_key);
  return;
}

// This function is signal-safe and can be called inside of signal-handler.
LOCKSET* GetPerThreadLockset() {
  return (LOCKSET*)pthread_getspecific(Tls_Lockset_Key);
}

// This function is not signal-safe, it only can be called in non-signal handler code
// Note that the client must ensure that the key is already initialized.
void SetPerThreadLockset() {
  LOCKSET* lockset = (LOCKSET*)malloc(sizeof(LOCKSET));
  lockset->n = 0;
  lockset->overflow = false;
  pthread_setspecific(Tls_Lockset_Key, lockset);
  return;
}
