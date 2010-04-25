#include <sys/types.h>
#include <stdint.h>
#include <perfmon/perfmon_pebs_core_smpl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
      void* memory_address_;
      void* pip_;
      void* sip_;
      int   is_write_;
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
  void* stack_trace_[10];
  int trace_depth_;
};

void print_record(struct Record* record) {
  if(record->type_ == MEMORY_OP) {
    printf("record no: %d, thread_id = %d ",
           record->record_no_, record->thread_id_);
    printf("MEMORY_OP ");
    printf("ip:0x%08llx ", (unsigned long long)record->op_.memory_access_.pip_);
    printf("sip:0x%08llx ", (unsigned long long)record->op_.memory_access_.sip_);
    printf("m_addr:0x%08llx ", (unsigned long long)record->op_.memory_access_.memory_address_);
    if(record->op_.memory_access_.is_write_) {
    printf("WRITE ");
    } else {
    printf("READ ");
    }
    if(record->op_.memory_access_.mem_op_.ip != 0) {
    printf("ip:0x%08llx, eax:0x%08llx, ebx:0x%08llx, ecx:0x%08llx, edx:0x%08llx, esi:0x%08llx, edi:0x%08llx, ebp:0x%08llx,  esp:0x%08llx, r8:0x%08llx, r9:0x%08llx, r10:0x%08llx, r11:0x%08llx, r12:0x%08llx, r13:0x%08llx, r14:0x%08llx, r15:0x%08llx ",
           (unsigned long long)record->op_.memory_access_.mem_op_.ip,
           (unsigned long long)record->op_.memory_access_.mem_op_.eax,
           (unsigned long long)record->op_.memory_access_.mem_op_.ebx,
           (unsigned long long)record->op_.memory_access_.mem_op_.ecx,
           (unsigned long long)record->op_.memory_access_.mem_op_.edx,
           (unsigned long long)record->op_.memory_access_.mem_op_.esi,
           (unsigned long long)record->op_.memory_access_.mem_op_.edi,
           (unsigned long long)record->op_.memory_access_.mem_op_.ebp,
           (unsigned long long)record->op_.memory_access_.mem_op_.esp,
           (unsigned long long)record->op_.memory_access_.mem_op_.r8,
           (unsigned long long)record->op_.memory_access_.mem_op_.r9,
           (unsigned long long)record->op_.memory_access_.mem_op_.r10,
           (unsigned long long)record->op_.memory_access_.mem_op_.r11,
           (unsigned long long)record->op_.memory_access_.mem_op_.r12,
           (unsigned long long)record->op_.memory_access_.mem_op_.r13,
           (unsigned long long)record->op_.memory_access_.mem_op_.r14,
           (unsigned long long)record->op_.memory_access_.mem_op_.r15
           );
    }
    printf("{ ");
    for (int i = 0; i < record->op_.memory_access_.lock_count_; i++) {
      printf("0x%08llx ", record->op_.memory_access_.lockset_[i]);
    }
    printf("} ");
    // print the stack trace here
    for (int i = 0; i < record->trace_depth_; i++) {
      printf("0x%08llx->", record->stack_trace_[i]);
    }
    printf("\n");	
  } else if (record->type_ == MEMORY_ALLOC){
    printf("record no: %d, thread_id = %d ",
           record->record_no_, record->thread_id_);
    if(record->op_.mem_alloc_op_.is_alloc_) {
      printf("MEMORY_ALLOC ");
    } else {
      printf("MEMORY_FREE ");
    }
    printf("start_address:0x%08llx ", (unsigned long long)record->op_.mem_alloc_op_.start_address_);
    printf("size:%d\n", (unsigned long long)record->op_.mem_alloc_op_.size_);
    printf("\n");
  } else {
    printf("we did not support other type yet\n");
  }

}


int main(int argc, char**argv) {
  printf("argc = %d\n", argc);
  if (argc < 2) {
    printf("you must specify a input file\n");
    exit(0);
  }
  FILE* fp = fopen(argv[1],"r");
  if(fp == NULL) {
    printf("open error\n");
    exit(0);
  }
  int result;
  int count = 0;
  struct Record* record = (struct Record*)malloc(sizeof(struct Record));
  memset(record, 0, sizeof(struct Record));
  printf("size is %d\n",sizeof(struct Record));
  while(!feof(fp)) {
    count++;
    result = fread(record, sizeof(struct Record), 1, fp);
    printf("count = %d\n", count);
    if(result != 1) {
      printf("read error\n");
      fclose(fp);
      exit(0);
    }
    print_record(record);
  }
  fclose(fp);
  return 0;
}
