// This file defines the LOCKSET structure and the interface 
// to manipulate thread local storage.
// Create_Tls_Key and SetPerThreadLockset is not signal-safe while
// GetPerThreadLockset is signal-safe. 
#ifndef RACEZ_LOCKSET_H
#define RACEZ_LOCKSET_H

typedef struct {
  int n;
  int overflow;
  void* lockset_[10];
}LOCKSET;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern void Create_Tls_Key();
extern LOCKSET* GetPerThreadLockset();
extern void SetPerThreadLockset();

#ifdef __cplusplus
}
#endif

#endif  /* ! RACEZ_LOCKSET_H */
