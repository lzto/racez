// This file provides a stub for start and stop racez.
// We need to add these two functions into user program.
// At runtime, these callsite will be redirected into 
// other same name functions in libracez.so with the help of
// the preload machanism.
// For example, if we want to get a binary, named app,
// to enable racez, user only need to follow the steps below:
// step 0: compile this file into libutil.so
// step 1: g++ -l/path/to/libutil.so -o binary other options
// step 2: LD_PRELOAD=/path/to/libracez.so ./binary options

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>

int racez_start() {
  // do nothing
  return 0;
}
int racez_stop() {
  // do nothing
  return 0;
}

void racez_log_memory_allocation(void* start_address, int size, int is_alloc) {
  return;
}

void debug_mutex() {
  return;
}

#ifdef __cplusplus
}
#endif
