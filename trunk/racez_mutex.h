#ifndef RACEZ_MUTEX_H
#define RACEZ_MUTEX_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
  * An experimental function to use C++ function in this library
*/
extern void add_one_mutex(pthread_mutex_t*);
extern void remove_one_mutex(pthread_mutex_t*);

#ifdef __cplusplus
}
#endif

#endif  /* ! RACEZ_MUTEX_H */
