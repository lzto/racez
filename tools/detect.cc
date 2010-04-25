#include <sys/types.h>
#include <stdint.h>
#include <perfmon/perfmon_pebs_core_smpl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>

#include <map>
#include <tr1/unordered_set>
#include <set>
#include <list>
#include <algorithm>
#include <string>

using namespace std::tr1;
using std::insert_iterator;
using std::pair;
using std::string;
using std::set;

typedef set<struct Record>INDEX_IP_SET;
typedef set<struct Record>::iterator INDEX_IP_SET_ITER;
typedef std::set<void *> LockSet;

// Here we only use three maps to implement the eraser style of 
// lockset algorithm. 
// For the detail of the algorithm, please refer to the paper below:
// Eraser: a dynamic data race detector for multithreaded programs
// Stefan Savage Univ. of Washington, Seattle, et.al
// http://portal.acm.org/citation.cfm?id=265927

// The first map records the memory access for the same memory address.
// For example, for addr1, for one thread, we only record the latest 
// access, i.e, a following W will kill the previous R in the same threads.
typedef std::map<void *, INDEX_IP_SET*> MemoryRecordSetMap;
// A map which records the memory status which is the key for lockset 
// statemachine algorith.
typedef std::map<void *, int> MemoryRWMap;
// Map from a memory addrss into its lockset
typedef std::map<void *, LockSet *> MemoryLockSetMap;

// Heap malloc/free map
typedef std::map<void*, int> HeapMap;


MemoryRecordSetMap global_record_set_map;
MemoryRWMap rw_map;
MemoryLockSetMap lockset_map;
HeapMap heap_map;

static string binary;
static bool enable_memory_reuse = false;
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
  // This operator is used for ordering the record
  bool operator<(const struct Record& entry) const {
    if(record_no_ < entry.record_no_)
      return true;
    if (record_no_ == entry.record_no_ && 
        op_.memory_access_.pip_ < entry.op_.memory_access_.pip_)
      return true;
    return false;
  }
};

static int count = 0;
static int invalid_count = 0;
static int warning_count = 0;

void get_filename(unsigned long long addr, char* buffer) {
  string addr_binary = "/usr/bin/addr2line";
  string addr_option = " -e ";
  string direct = " >  ";
  string temp_file = "tmp.txt";
  char sbuffer[200];
  snprintf(sbuffer, 200, "0x%08llx", addr);
  string address = sbuffer;
  string final_addr_binary = addr_binary + addr_option + binary  + " " + address + direct + temp_file;
  printf("addr2line command is %s\n", final_addr_binary.c_str());
  if(system(final_addr_binary.c_str()) == -1) {
    perror("can not execute addr2line\n");
  }
  FILE* temp_fp = fopen(temp_file.c_str(),"r");
  if (temp_fp == NULL) {
    printf("can not open temp file!\n");
    exit(0);
  }
  fgets(buffer, 2000, temp_fp);
  int len = strlen(buffer);
  assert(buffer[len - 1] == '\n');
  buffer[len - 1] = '\0';
  fclose(temp_fp);
  return;
}

void report_error(INDEX_IP_SET* record_set, void* memory_address, FILE* output) {
  warning_count++;
  fprintf(output, "Warning %d :\n", warning_count);
  fprintf(output, "Memory Location: %p\n",memory_address);
  INDEX_IP_SET_ITER iter;
  int i = 1;
  char filename[2000];
  for(iter = record_set->begin(); iter != record_set->end(); iter++, i++) {
    unsigned long long ip_addr = (unsigned long long)(*iter).op_.memory_access_.pip_;
    fprintf(output, "The %dth access: INDEX=%d TID=%d IP=0x%08llx ", i, 
            (*iter).record_no_, (*iter).thread_id_,
            ip_addr);
    if ((*iter).op_.memory_access_.is_write_)
      fprintf(output, "WRITE ");
    else
      fprintf(output, "READ ");
    get_filename(ip_addr, filename);
    fprintf(output, "%s\n", filename);
    fprintf(output, "the lockset is : ( ");
    for(int j = 0; j < (*iter).op_.memory_access_.lock_count_; j++)
      fprintf(output, "%p\t", (*iter).op_.memory_access_.lockset_[j]);
    fprintf(output, ")\n");
    fprintf(output,"call stack is:\n");
    for (int j = 0; j < (*iter).trace_depth_; j++) {
      unsigned long long site = (unsigned long long)(*iter).stack_trace_[j];
      get_filename(site, filename);
      fprintf(output, "%s\n", filename);
    }
  }
  fprintf(output, "\n");
  fprintf(output, "\n");
  return;
}

INDEX_IP_SET_ITER  set_includes_thread_id(INDEX_IP_SET* record_set, 
                                          int thread_id){
  INDEX_IP_SET_ITER iter;
  for (iter = record_set->begin(); 
       iter != record_set->end(); iter++){
    if ((*iter).thread_id_ == thread_id)
      return iter;
  } 
  return iter;
}

int core_detection(struct Record* cur_record, INDEX_IP_SET* record_set,
                   int* status, LockSet* lockset) {
  // Model the state machines
  // state 0: T==1, R exclusive read
  // state 1: T==1, W exclusive write
  // state 2: R, T>1; shared read
  // state 3: shared modified
  // state 4: error shared modify with inconsistent lockset.
  int may_report = 0;
  int will_report = 0;
  switch (*status) {
    // at state 0: where only R happened, T==1;
  case 0: {
    // First we check if thread_id of cur_record is same as
    // the thread_id in record_set;
    assert(record_set->size() == 1);
    INDEX_IP_SET_ITER iter = 
      set_includes_thread_id(record_set, cur_record->thread_id_);
    // If access from different thread.
    if (iter == record_set->end()) {
      // Whatever, we insert this record into
      // the record set.
      record_set->insert(*cur_record);
      // if current access is a write, directly
      // enter state 3.
      // otherwise, enter state 2.
      if (cur_record->op_.memory_access_.is_write_ == 1) {
        may_report = 1;
        *status = 3;
      } else {
        *status = 2;
      }
    } else {
      // otherwise, we replace the current entry
      record_set->erase(iter);
      record_set->insert(*cur_record);
      if (cur_record->op_.memory_access_.is_write_ == 1) {
        *status = 1;
      }
    }
    break;
  }
  case 1: {
    assert(record_set->size() == 1);
    INDEX_IP_SET_ITER iter = 
      set_includes_thread_id(record_set, cur_record->thread_id_);
    // If access from different thread.
    if (iter == record_set->end()) {
      // Whatever, we insert the current record.
      record_set->insert(*cur_record);
      // if current entry is write, then any incoming access(read or write) from
      // other thread will cause us enter into status 3. 
      may_report = 1;
      *status = 3;
    } else {
      // If it's a write operation, replace the current write.
      // both read or write from the same thread will not
      // update the status.
      if (cur_record->op_.memory_access_.is_write_ == 1) {
        record_set->erase(iter);
        record_set->insert(*cur_record);
      }
    }
    break;
  }
  case 2: {
    // Shared read state
    assert(record_set->size() > 1);
    INDEX_IP_SET_ITER iter = 
      set_includes_thread_id(record_set, cur_record->thread_id_);
    if (iter == record_set->end()) {
      record_set->insert(*cur_record);
      if (cur_record->op_.memory_access_.is_write_ == 1) {
        may_report = 1;
        *status = 3;
      }
    } else {
      record_set->erase(iter);
      record_set->insert(*cur_record);
      if (cur_record->op_.memory_access_.is_write_ == 1) {
        may_report = 1;
        *status = 3;
      }
    }
    break;
  }
  case 3: {
    assert(record_set->size () > 1);
    INDEX_IP_SET_ITER iter = 
      set_includes_thread_id(record_set, cur_record->thread_id_);
    if (iter == record_set->end()) {
      record_set->insert(*cur_record);
    } else {
      if (cur_record->op_.memory_access_.is_write_ == 1 ||
          ((*iter).op_.memory_access_.is_write_ == 0 &&
           cur_record->op_.memory_access_.is_write_ == 1)) {
        record_set->erase(iter);
        record_set->insert(*cur_record);
      }
      may_report = 1;
    }
    break;
  }
  case 4: {
    assert(record_set->size() > 1);
    // Currently we disable this.
    if(0) {
      INDEX_IP_SET_ITER iter = 
        set_includes_thread_id(record_set, cur_record->thread_id_);
      // If we are already in error shared modify state,
      // we need to check if the incoming thread is new.
      // we only need to work if we met a new thread or
      // it's from exising threads but access at different IP.
      if (iter == record_set->end()) {
        record_set->insert(*cur_record);
        will_report = 1;
      } else {
        if(cur_record->op_.memory_access_.pip_ != 
           (*iter).op_.memory_access_.pip_) {
          record_set->erase(iter);
          record_set->insert(*cur_record);
          will_report = 1;
        }
      }
    } 
    break;
  }
  default:
    assert(0);
  }
  // Whatever, we intersection the lockset.
  LockSet temp_set;
  LockSet result_set;
  for(int i = 0; i < cur_record->op_.memory_access_.lock_count_; i++)
    temp_set.insert(cur_record->op_.memory_access_.lockset_[i]);
  
  set_intersection(lockset->begin(), lockset->end(),
                   temp_set.begin(), temp_set.end(),
                   inserter(result_set, result_set.begin()));
  lockset->swap(result_set);
  // If the intersection is not empty, report the warning.
  // If the two set are same, simply drop the later one and
  if(may_report == 1) {
    if(*status == 3) {
      if(lockset->empty()) {
        will_report = 1;
        *status = 4;
      }
    } 
  }
  return will_report;
}

void delete_map_entries(void* start_address, void* end_address) {
  MemoryLockSetMap::iterator lockset_start_iter;
  MemoryLockSetMap::iterator lockset_end_iter;
  // lower_bound return the iter which is the first not less than start_address
  lockset_start_iter = lockset_map.lower_bound(start_address);
  // TODO(tianweis): check how about if it return the equal one(==), we
  // need to include end_iter.
  lockset_end_iter = lockset_map.lower_bound(end_address);
  for(MemoryLockSetMap::iterator iter = lockset_start_iter; iter != lockset_end_iter;) {
    delete iter->second;
    lockset_map.erase(iter++);
  }

  MemoryRecordSetMap::iterator record_set_start_iter;
  MemoryRecordSetMap::iterator record_set_end_iter;
  // lower_bound return the iter which is the first not less than start_address
  record_set_start_iter = global_record_set_map.lower_bound(start_address);
  // TODO(tianweis): check how about if it return the equal one(==), we
  // need to include end_iter.
  record_set_end_iter = global_record_set_map.lower_bound(end_address);
  for(MemoryRecordSetMap::iterator iter = record_set_start_iter; iter != record_set_end_iter;) {
    delete iter->second;
    global_record_set_map.erase(iter++);
  }

  MemoryRWMap::iterator rw_start_iter;
  MemoryRWMap::iterator rw_end_iter;
  // lower_bound return the iter which is the first not less than start_address
  rw_start_iter = rw_map.lower_bound(start_address);
  // TODO(tianweis): check how about if it return the equal one(==), we
  // need to include end_iter.
  rw_end_iter = rw_map.lower_bound(end_address);
  for(MemoryRWMap::iterator iter = rw_start_iter; iter != rw_end_iter;) {
    //printf("delete the address %p among %p:%p\n", iter->first, start_address, end_address);
    rw_map.erase(iter++);
  }
}

void offline_detection(struct Record* record, FILE* output) {
  if(record->type_ == MEMORY_OP) {
    int thread_id = record->thread_id_;
    void *memory_address = record->op_.memory_access_.memory_address_;
    void *code_address = (void*)record->op_.memory_access_.pip_;
    if(memory_address == 0)
      return;
    else {
      pair<MemoryRecordSetMap::iterator, bool> memory_record_set_map_status =
        global_record_set_map.insert(MemoryRecordSetMap::value_type(memory_address, NULL));
      pair<MemoryRWMap::iterator, bool> rw_map_status =
        rw_map.insert(MemoryRWMap::value_type(memory_address, NULL));
      pair<MemoryLockSetMap::iterator, bool> lockset_map_status =
          lockset_map.insert(MemoryLockSetMap::value_type(memory_address, NULL));
      // If first time is entered, we created new a Lockset.
      // If the map entries already exists, do the intersection to
      // if we can find the inconsistent lockset
      if (rw_map_status.second) {
        // Insert the current record into the record set.
        assert(memory_record_set_map_status.second);
        assert(lockset_map_status.second);
        memory_record_set_map_status.first->second = new INDEX_IP_SET();
        memory_record_set_map_status.first->second->insert(*record);
        lockset_map_status.first->second = new LockSet();
        for(int i = 0; i < record->op_.memory_access_.lock_count_; i++)
          lockset_map_status.first->second->insert(record->op_.memory_access_.lockset_[i]);
        rw_map_status.first->second = record->op_.memory_access_.is_write_;
        // If we met it first time, just return;
        return;
      } else {
        // First we use a function to update the rec_set for memory address.
        INDEX_IP_SET* record_set = memory_record_set_map_status.first->second;
        LockSet* lockset = lockset_map_status.first->second;
        if (core_detection(record, record_set, &(rw_map_status.first->second), lockset)){
          report_error(record_set, memory_address, output);
        }
      }
    }
  } else if (record->type_ == MEMORY_ALLOC){
    // if enable memory_reuse, we need to perform the following additional
    // steps.
    // a: if it's allocate operations, register the start and size in a map
    // b. if it's free operations,
    // b.1 if the size is 0, query the map to get the size information which is
    // registered at step a.
    // b.2 if the size is not zero, get the size information directly
    // b.3 give the start_address and size, get the memory range[start_address,
    // start_address+size] to get the lower_bound and upbound of the
    // MemoryLockSet map and delete any entries among this range. 
    if(enable_memory_reuse) {
      int size = 0;
      if(record->op_.mem_alloc_op_.is_alloc_) {
        heap_map[record->op_.mem_alloc_op_.start_address_] = 
          record->op_.mem_alloc_op_.size_;
      } else {
        size = record->op_.mem_alloc_op_.size_;
        void* start_address = record->op_.mem_alloc_op_.start_address_;
        // TODO(tianweis): Check why we will get start_address == 0 events.
        if(start_address == 0)
          return;
        if (size == 0) {
          //TODO(tianweis): There are some cases that the FREE is logged before
          // MALLOC
          // for example:
          // record no: 11, thread_id = 1022 MEMORY_FREE
          // start_address:0x029d3e20 size:0
          // record no: 12, thread_id = 1022 MEMORY_ALLOC
          // start_address:0x029d3e20 size:61
          size = heap_map[start_address];
          if(size == 0)
            return;
        }
        void* end_address = (char*)start_address + size;
        delete_map_entries(start_address, end_address);
      }
    }
    return;
  } else {
    printf("we did not support other type yet\n");
  } 
  return;
}

void print_simple_record(struct Record* record) {
  if(record->type_ == MEMORY_OP) {
    printf("record no: %d, thread_id = %d ",
           record->record_no_, record->thread_id_);
    printf("MEMORY_OP ");
    printf("m_addr:0x%08llx",(unsigned long long)record->op_.memory_access_.memory_address_);
    printf("{ ");
    for (int i = 0; i < record->op_.memory_access_.lock_count_; i++) {
      printf("0x%08llx ", (unsigned long long)record->op_.memory_access_.lockset_[i]);
    }
    printf("}\n");
  } else {
    printf("we did not support other type yet\n");
  }

}

void print_record(struct Record* record) {
  if(record->type_ == MEMORY_OP) {
    printf("record no: %d, thread_id = %d ",
           record->record_no_, record->thread_id_);
    printf("MEMORY_OP ");
    printf("m_addr:0x%08llx,ip:0x%08llx, eax:0x%08llx, ebx:0x%08llx, ecx:0x%08llx, edx:0x%08llx, esi:0x%08llx, edi:0x%08llx, ebp:0x%08llx,  esp:0x%08llx, r8:0x%08llx, r9:0x%08llx, r10:0x%08llx\n, r11:0x%08llx, r12:0x%08llx, r13:0x%08llx, r14:0x%08llx, r15:0x%08llx ",
           (unsigned long long)record->op_.memory_access_.memory_address_,
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
    printf("{ ");
    for (int i = 0; i < record->op_.memory_access_.lock_count_; i++) {
      printf("0x%08llx ", (unsigned long long)record->op_.memory_access_.lockset_[i]);
    }
    printf("}\n");
  } else {
    printf("we did not support other type yet\n");
  }

}

static void
usage(void)
{
  printf("detect [-b binary] [-i input] [-o output ] [-m]\n"
                "-b binarys: the binary which the tool want to detect\n"
                "-i input: input files of offline data\n"
                "-n input: new input files of offline data\n"
                "-o output: output file to save the final detection result\n"
                "-m : enable the memory reuse filter. (default: off)\n");
}


int main(int argc, char**argv) {
  char* output_file;
  char* input_file = NULL;
  char* new_input_file = NULL;
  FILE* new_input_fp = NULL;
  INDEX_IP_SET index_ip_set;
  INDEX_IP_SET_ITER index_ip_set_iter;
  int i;
  while((i=getopt(argc, argv, "b:i:n:o:mh")) != EOF) {
    switch(i) {
    case 'h':
      usage();
      return 0;
    case 'b':
      binary = optarg;
      break;
    case 'i':
      input_file = optarg;
      break;
    case 'n':
      new_input_file = optarg;
      break;
    case 'o':
      output_file = optarg;
      break;
    case 'm':
      enable_memory_reuse = true;
      break;
    default: {
      printf("invalid option\n");
      return 0;
    }
    }
  }
  if(argc < 4) {
    printf("At least three options: ./detect -b binary -i input -o output\n");
    return 0;
  }
  FILE* input_fp = fopen(input_file,"r");
  if(input_fp == NULL) {
    printf("open error\n");
    exit(0);
  }
  if(new_input_file != NULL) {
    new_input_fp = fopen(new_input_file,"r");
    if(new_input_fp == NULL) {
      printf("open error\n");
      exit(0);
    }
  }
  FILE* output_fp = fopen(output_file, "w");
  if (output_fp == NULL) {
    printf("output file open error\n");
    exit(0);
  }
  int raw_result;
  int new_result;
  int count = 0;
  struct Record* raw_record = (struct Record*)malloc(sizeof(struct Record));
  memset(raw_record, 0, sizeof(struct Record));
  struct Record* new_record = (struct Record*)malloc(sizeof(struct Record));
  memset(new_record, 0, sizeof(struct Record));
  // First we read the first element
  raw_result = fread(raw_record, sizeof(struct Record), 1, input_fp);
  if(raw_result != 1) {
    printf("read raw input error\n");
    return 0;
  }
  if(new_input_fp != NULL) {
    // We first need to order the record in new_input_fp
    new_result = fread(new_record, sizeof(struct Record), 1, new_input_fp);
    // Sometime the new result file is zero even user specify
    // TODO(tianweis): consider this problem again.
    if(new_result != 1) {
      printf("read new input file error\n");
      new_input_fp = NULL;
    }
    while(new_result == 1) {
      index_ip_set.insert(*new_record);
      new_result = fread(new_record, sizeof(struct Record), 1, new_input_fp);
    }
  }

  // The raw record do not have same record_no_, while the
  // new records may have same record_no_.
  // It's a 1->{n} mapping
  if (new_input_fp != NULL) {
    index_ip_set_iter = index_ip_set.begin();
    //printf("new record size is %d\n", index_ip_set.size());
    //for(; index_ip_set_iter != index_ip_set.end(); index_ip_set_iter++)
    //  printf("record no is %d\n", (*index_ip_set_iter).record_no_);
    while(raw_result == 1 && index_ip_set_iter != index_ip_set.end()) {
      count++;
      // First we read all new records which have same record_no_ as
      // current raw record.
      if((*index_ip_set_iter).record_no_ == raw_record->record_no_) {
        struct Record temp_record;
        temp_record = *index_ip_set_iter;
        offline_detection(&temp_record, output_fp);
        index_ip_set_iter++;
        continue;
      }
      // Then we detect the current raw record and then advance to the next
      // raw record.
      offline_detection(raw_record, output_fp);
      raw_result = fread(raw_record, sizeof(struct Record), 1, input_fp);
    }
  } else {
    while (raw_result == 1) {
      offline_detection(raw_record, output_fp);
      raw_result = fread(raw_record, sizeof(struct Record), 1, input_fp);
    }
  }

  fclose(input_fp);
  fclose(output_fp);
  free(raw_record);
  free(new_record);
  
  if(new_input_fp != NULL) {
    fclose(new_input_fp);
    index_ip_set.clear();
  }
  return 0;
}
