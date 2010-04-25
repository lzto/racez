#ifndef RACEZ_BUFFERDATA_H
#define RACEZ_BUFFERDATA_H

#include <sys/types.h>
#include <stdint.h>
#include <perfmon/perfmon_pebs_smpl.h>
// This file defines a buffer data structure to store the different events. 
typedef enum  {
  MEMORY_OP = 1,
  SYNC_LOCK = 2,
  SYNC_UNLOCK = 3,
  SYNC_SIGNAL = 4,
  SYNC_WAIT = 5,
  MEMORY_ALLOC = 6
} RecordType;

// A record is used to store one line in log file
struct Record {
  int record_no_;
  int thread_id_;
  RecordType type_;
  union {
    struct {
      // Extracted these two  here since IBS can get this directly.
      // For IBS, after racez, memory_address_ is set to zero, while
      // pip_ is equal to mem_op_.ip
      void* memory_address_;
      void* pip_;
      // temporal experiment, the user level ip in signal-handler
      void* sip_;
      int is_write_;
      // Note that we use the core_smpl_entry_t for both core and NHM
      pfm_pebs_core_smpl_entry_t mem_op_; 
      int lock_count_;
      void *lockset_[10];
    }memory_access_;
    struct {
      void *lock_address_;
    }lock_op_;
    struct {
      void *signal_address_;
    }signal_op_;
    struct {
      void *wait_address1_;
      void *wait_address2_;
    }wait_op_;
    struct {
      void*start_address_;
      int size_;
      int is_alloc_;
    }mem_alloc_op_;
  }op_;
  // store the stack trace of this record
  // We only store the IP addresses and get the 
  // final call chain through offline analysis using addr2line
  void* stack_trace_[10];
  // trace depth at most is 10 now.
  int trace_depth_;
};

#define BUFFERSIZE 5000000

struct BufferData {
  struct Record* global_;
  struct Record* start_;
  int fd_;
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
extern int InitBuffer(const char* file_name);
extern void StopBuffer();
extern void WriteRecord(struct Record* record);
extern void FlushBuffer();


#ifdef __cplusplus
}
#endif

#endif  /* ! APR_RACEZ_BUFFERDATA_H */
