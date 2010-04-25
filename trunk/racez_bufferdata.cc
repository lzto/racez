// TODO(tianwei): Copyright info

#include "racez_bufferdata.h"

#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>

static struct BufferData *global_buffer = NULL; 
extern int racez_write_record;
struct Record* atomic_increment_no_barrier(struct Record** ptr, int increment) {
  long temp = increment;
  __asm__ __volatile__("lock; xadd %0,%1"
                       : "+r" (temp), "+m" (*ptr)
                       : : "memory");
  // temp now holds the old value of *ptr
  return (struct Record*)(temp + increment);
}
// This file implement the interface in bufferdata.h
// TODO(tianweis): add a evict mechnasim to handle the case
// when the buffer is full.
void WriteRecord(struct Record* record) {
  if(racez_write_record == 0)
    return;
  struct Record* current_entry = 
    atomic_increment_no_barrier(&global_buffer->global_, sizeof(struct Record));

  int record_no = current_entry - global_buffer->start_;
  // TODO(tianweis): Revisit this statement.
  if (record_no > BUFFERSIZE)
    return;

  current_entry->record_no_ = record_no;
  current_entry->thread_id_ = record->thread_id_;
  current_entry->type_ = record->type_;
  if(current_entry->type_ == MEMORY_OP) {
    current_entry->op_.memory_access_.memory_address_ = 
      record->op_.memory_access_.memory_address_;
    // Now we only can process memory access
    current_entry->op_.memory_access_.mem_op_ = 
      record->op_.memory_access_.mem_op_;
    current_entry->op_.memory_access_.pip_ =
      record->op_.memory_access_.pip_;
    current_entry->op_.memory_access_.sip_ = 
      record->op_.memory_access_.sip_;
    current_entry->op_.memory_access_.is_write_ =
      record->op_.memory_access_.is_write_;
    current_entry->op_.memory_access_.lock_count_ = 
      record->op_.memory_access_.lock_count_;

    for(int i = 0; i < record->op_.memory_access_.lock_count_; i++) {
      current_entry->op_.memory_access_.lockset_[i] = 
        record->op_.memory_access_.lockset_[i];
    }
  } else if (current_entry->type_ == MEMORY_ALLOC) {
    current_entry->op_.mem_alloc_op_.start_address_ = 
      record->op_.mem_alloc_op_.start_address_;
    current_entry->op_.mem_alloc_op_.size_ = 
      record->op_.mem_alloc_op_.size_;
    current_entry->op_.mem_alloc_op_.is_alloc_ = 
      record->op_.mem_alloc_op_.is_alloc_;
  }

  // Copy the stack trace
  current_entry->trace_depth_ = record->trace_depth_;
  for(int i = 0; i < current_entry->trace_depth_; i++) {
    current_entry->stack_trace_[i] = 
      record->stack_trace_[i];
  }
  return;
}

void StopBuffer() {
  struct Record* entry = global_buffer->start_;
  int fd = global_buffer->fd_;
  int count = 0;
  int nbytes = sizeof(struct Record);
  while(entry <= global_buffer->global_) {
    count++;
    if(write(fd, entry, nbytes) == -1) {
      perror("stop buffer");
      printf("*********fd:%d, write error:%d***********\n", fd, errno);
    }
    entry++;
  }
  printf("count = %d\n",count);
  close(fd);
  free(global_buffer->start_);
  free(global_buffer);
  global_buffer = NULL;
  return;
}

void FlushBuffer() {
  struct Record* entry = global_buffer->start_;
  int fd = global_buffer->fd_;
  while (entry <= global_buffer->global_) {
    write(fd, entry, sizeof(struct Record));
    entry++;
  }
  return;
}

// This function only will be called by the main thread
// TODO(tianweis): Do we need to protect the access to global_buffer_?
int InitBuffer(const char* file_name) {
  // the global_buffer should have been initialized in caller
  assert(global_buffer == NULL);
  global_buffer = (struct BufferData*) malloc(sizeof(BufferData));
  if(global_buffer == NULL) {
    printf("alloc memory failed\n");
    exit(1);
  } 
  memset(global_buffer, 0, sizeof(BufferData));

  global_buffer->start_  = (struct Record*) malloc(BUFFERSIZE*sizeof(struct Record));
  if(global_buffer->start_ == NULL) {
    printf("alloc memory failed\n");
    exit(1);
  }

  global_buffer->global_ = global_buffer->start_;
 
  int fd = open(file_name,O_RDWR|O_TRUNC|O_CREAT, 0777);
  
  if(fd < 0) {
    perror("error is:");
    printf("errno in init_buffer is %d\n", errno);
    printf("open file error!!!\n");
    return false;
  }

  global_buffer->fd_ = fd;
  return true;
}
