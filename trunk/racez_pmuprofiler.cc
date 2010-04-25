// TODO(tianwei): Copyright info

#include "racez_bufferdata.h" // the buffer data
#include "racez_mutex.h"
#include "racez_lockset.h"
#include "racez_pmuprofiler.h"
#include <sys/time.h>
#include <sys/types.h>
#include <inttypes.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
//#include <google/stacktrace.h> // for stacktrace
// TODO(tianweis): Use the code as google to decide if we use glibc backtrace
// or libunwind. Now we found that glibc backtrace has some problem, we need to
// use latest libunwind library from its git repository.
// git clone git://git.sv.gnu.org/libunwind.git
#include <libunwind.h>
// NOTE that, now we only support libpfm3.9 and 2.6.30 kernel.
#include <perfmon/perfmon.h>
// Now libpfm 3.9 has a unified header file both for
// core2 and NHM. We do not need this smpl header for AMD IBS
// since we write and read counter directly and do not use buffer as
// INTEL.
#include <perfmon/perfmon_pebs_smpl.h> 

#include <perfmon/pfmlib.h>
#include <perfmon/pfmlib_core.h> // for Core2 
#include <perfmon/pfmlib_intel_nhm.h> // for NHM
#include <perfmon/pfmlib_amd64.h>  // for AMD

#include <map> // A simple hash function 
using namespace std;


// Now since we are on 2.6.30, we use 15 and 16
#ifndef F_SETOWN_EX
#define F_SETOWN_EX	15
#define F_GETOWN_EX	16

#define F_OWNER_TID	0
#define F_OWNER_PID	1
#define F_OWNER_GID	2

struct f_owner_ex {
	int	type;
	pid_t	pid;
};
#endif

int signum = SIGIO;
bool sig_enable = false;
extern int racez_enable;
extern int racez_lockset;
extern int racez_stacktrace;
extern int racez_sampling_adjust;
extern int racez_signal_sample;

int num_stat = 0;
#define SMPL_EVENT	"INST_RETIRED:ANY_P" /* not all events support PEBS */

pthread_key_t   Tls_Hdr_Key; // smpl_hdr_t key
static pthread_once_t hdr_key_once = PTHREAD_ONCE_INIT;

pthread_mutex_t sig_mutex;

// The hdr and arg can apply to both core and nhm
typedef pfm_pebs_smpl_hdr_t	smpl_hdr_t;
typedef pfm_pebs_smpl_arg_t	smpl_arg_t;

typedef pfm_pebs_core_smpl_entry_t	core_smpl_entry_t;
typedef pfm_pebs_nhm_smpl_entry_t       nhm_smpl_entry_t;

static   int type;
static const char* file_name = "/home/tianwei/b.txt";

#define NUM_PMCS	16
#define NUM_PMDS	16

#define N 10 
#define FMT_CORE_NAME                       PFM_PEBS_SMPL_NAME
#define FMT_NHM_NAME                        PFM_PEBS_SMPL_NAME
#define FMT_AMD_NAME                        "ibs_amd"

#define PMD_IBSOP_NUM		7

#define SMPL_PERIOD	20001ULL	 /* must not use more bits than actual HW
 counter width */

typedef map<int, int>TID_FD_MAP;
typedef TID_FD_MAP::iterator TID_FD_MAP_ITER;
static pthread_mutex_t tid_fd_map_mutex;
static TID_FD_MAP tid_fd_map;
// Return the current tid for the calling thread.
static pid_t get_current_tid()
{
  return syscall(SYS_gettid);
}

// The following two functions are only called by core and nhm microarchitecture.
// For AMD IBS, we do not use sampling buffer and read the IBS OP register directly.
static void make_hdr_key()
{
  (void) pthread_key_create(&Tls_Hdr_Key, NULL);
}
// This function is not signal-safe, it only can be called inside of non-signal-handler
// code.
static void Create_Hdr_Key() {
  // TODO(tianweis): set the destructor.
  pthread_once(&hdr_key_once, make_hdr_key);
  return; ;
}
// This function is signal-safe and can be called inside of signal-handler.
static void* GetPerThreadHdr() {
  return pthread_getspecific(Tls_Hdr_Key);
}

static void fatal_error(const char *fmt,...) __attribute__((noreturn));

static void
fatal_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	printf("fatal error here!\n");
	exit(1);
}

static void
warning(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

// Use the interface in bufferdata.h and write the current
// record into log file
static void write_pebs_info(Record* record,int tid, void* input_entry, unsigned
                            long long sip, void*context) {
  core_smpl_entry_t *entry = (core_smpl_entry_t *)input_entry;
  LOCKSET *lockset = NULL;
  record->thread_id_ = tid;
  record->type_ = MEMORY_OP;
  record->op_.memory_access_.memory_address_ = 0;
  // initilize it as read
  // the Mao phase will be reposible for setting the value
  // according to the type of the instruction.
  record->op_.memory_access_.is_write_ = 0;
  record->op_.memory_access_.pip_ = (void*)entry->ip;
  record->op_.memory_access_.sip_ = (void*)sip;
  printf("ip:0x%08llx,sip:0x%08llx\n", (unsigned long long)record->op_.memory_access_.pip_,(unsigned long long)record->op_.memory_access_.sip_);

  record->op_.memory_access_. mem_op_.eflags = entry->eflags;
  record->op_.memory_access_. mem_op_.ip = entry->ip;
  record->op_.memory_access_. mem_op_.eax = entry->eax;
  record->op_.memory_access_. mem_op_.ebx = entry->ebx;
  record->op_.memory_access_. mem_op_.ecx = entry->ecx;
  record->op_.memory_access_. mem_op_.edx = entry->edx;
  record->op_.memory_access_. mem_op_.esi = entry->esi;
  record->op_.memory_access_. mem_op_.edi = entry->edi;
  record->op_.memory_access_. mem_op_.ebp = entry->ebp;
  record->op_.memory_access_. mem_op_.esp = entry->esp;
  record->op_.memory_access_. mem_op_.r8 = entry->r8;
  record->op_.memory_access_. mem_op_.r9 = entry->r9;
  record->op_.memory_access_. mem_op_.r10 = entry->r10;
  record->op_.memory_access_. mem_op_.r11 = entry->r11;
  record->op_.memory_access_. mem_op_.r12 = entry->r12;
  record->op_.memory_access_. mem_op_.r13 = entry->r13;
  record->op_.memory_access_. mem_op_.r14 = entry->r14;
  record->op_.memory_access_. mem_op_.r15 = entry->r15;
  // TODO(tianweis): Check if lockset will be NULL.
  if(racez_lockset)
    lockset = GetPerThreadLockset();
  if (lockset != NULL) {
    record->op_.memory_access_.lock_count_ = lockset->n;
    for (int i = 0; i < lockset->n; i++) {
      record->op_.memory_access_.lockset_[i] = lockset->lockset_[i];
    }
  }
  WriteRecord(record);

  // Only turn on the signal sample on CORE 2, it seems that 
  // NHM fix this skid problem.
  if (racez_signal_sample && type == PFMLIB_INTEL_CORE_PMU){
      ucontext_t my_context = *(ucontext_t*)context;
      // Here, we extend the mechanism that we add another records from user-level context
      Record urecord;  	
      memset(&urecord, 0, sizeof(struct Record));
      urecord.thread_id_ = tid;
      urecord.type_ = MEMORY_OP;
      urecord.op_.memory_access_.memory_address_ = 0;
      // initilize it as read
      // the Mao phase will be reposible for setting the value
      // according to the type of the instruction.
      urecord.op_.memory_access_.is_write_ = 0;
      urecord.op_.memory_access_.pip_ = (void*)sip;
      urecord.op_.memory_access_.sip_ = (void*)sip;
      urecord.op_.memory_access_. mem_op_.eflags = my_context.uc_mcontext.gregs[17];
      urecord.op_.memory_access_. mem_op_.ip = sip;
      urecord.op_.memory_access_. mem_op_.eax = my_context.uc_mcontext.gregs[13];
      urecord.op_.memory_access_. mem_op_.ebx = my_context.uc_mcontext.gregs[11];
      urecord.op_.memory_access_. mem_op_.ecx = my_context.uc_mcontext.gregs[14];
      urecord.op_.memory_access_. mem_op_.edx = my_context.uc_mcontext.gregs[12];
      urecord.op_.memory_access_. mem_op_.esi = my_context.uc_mcontext.gregs[9];
      urecord.op_.memory_access_. mem_op_.edi = my_context.uc_mcontext.gregs[8];
      urecord.op_.memory_access_. mem_op_.ebp = my_context.uc_mcontext.gregs[10];
      urecord.op_.memory_access_. mem_op_.esp = my_context.uc_mcontext.gregs[15];
      urecord.op_.memory_access_. mem_op_.r8 = my_context.uc_mcontext.gregs[0];
      urecord.op_.memory_access_. mem_op_.r9 = my_context.uc_mcontext.gregs[1];
      urecord.op_.memory_access_. mem_op_.r10 = my_context.uc_mcontext.gregs[2];
      urecord.op_.memory_access_. mem_op_.r11 = my_context.uc_mcontext.gregs[3];
      urecord.op_.memory_access_. mem_op_.r12 = my_context.uc_mcontext.gregs[4];
      urecord.op_.memory_access_. mem_op_.r13 = my_context.uc_mcontext.gregs[5];
      urecord.op_.memory_access_. mem_op_.r14 = my_context.uc_mcontext.gregs[6];
      urecord.op_.memory_access_. mem_op_.r15 = my_context.uc_mcontext.gregs[7];
      // TODO(tianweis): Check if lockset will be NULL.
      if (lockset != NULL) {
        urecord.op_.memory_access_.lock_count_ = lockset->n;
        for (int i = 0; i < lockset->n; i++) {
          urecord.op_.memory_access_.lockset_[i] = lockset->lockset_[i];
        }
      }
      WriteRecord(&urecord);
  }
}

// Use the interface in bufferdata.h and write the current
// record into log file
static void write_ibs_info(Record* record, int tid, unsigned long long ip,
                           unsigned long long m_addr, bool is_read,
                           unsigned long long sip) {
  record->thread_id_ = tid;
  record->type_ = MEMORY_OP;
  record->op_.memory_access_.memory_address_ = (void*)m_addr;
  // initilize it as read
  // the Mao phase will be reposible for setting the value
  // according to the type of the instruction.
  if(is_read)
    record->op_.memory_access_.is_write_ = 0;
  else 
    record->op_.memory_access_.is_write_ = 1;
  record->op_.memory_access_.pip_ = (void*)ip;
  record->op_.memory_access_.sip_ = (void*)sip;
  // TODO(tianweis): Check if lockset will be NULL.
  LOCKSET *lockset = GetPerThreadLockset();
  if (lockset != NULL) {
    record->op_.memory_access_.lock_count_ = lockset->n;
    for (int i = 0; i < lockset->n; i++) {
      record->op_.memory_access_.lockset_[i] = lockset->lockset_[i];
    }
  }
  WriteRecord(record);

}

static void
process_smpl_buf_pebs(Record* record, smpl_hdr_t *hdr, unsigned long long sip,
                      void* context)
{
  void *ent;
  int entry;
  unsigned long count;
  int tid = get_current_tid();
  
  count = hdr->count;

  // we should have one-entry.
  //assert(count == 1);
  // TODO(tianweis): Why we will get count = 0 case?
  if(count == 0)
    return;
  /*
   * the beginning of the buffer does not necessarily follow the header
   * due to alignement.
   */
  ent  = hdr+1;
  while(count--) {
    if (count == 0) {
      write_pebs_info(record, tid, ent, sip, context);
    }
      ent = (char*)ent + hdr->entry_size;
    } 
}

static void signal_handler_pebs(Record* record, int sig, siginfo_t* into, void *context) {
  smpl_hdr_t *hdr;
  ucontext_t my_context = *(ucontext_t*)context;
  unsigned long long sip = (unsigned long long)my_context.uc_mcontext.gregs[16];
  hdr = (smpl_hdr_t*) GetPerThreadHdr();
  if(hdr != NULL)
    process_smpl_buf_pebs(record, hdr, sip, context);
}


// TODO(tianweis): Add the AMD ibs support.
static void signal_handler_amd_ibs(Record* record, int sig, siginfo_t* info, void *context) {
  int fd = info->si_fd;
  pfarg_pmd_t pd[PFMLIB_MAX_PMDS];
  int tid = get_current_tid();
  unsigned long long ip, m_addr;
  pfarg_msg_t msg;
  ibsopdata3_t opdata3;
  ucontext_t my_context = *(ucontext_t*)context;
  unsigned long long sip = (unsigned long long)my_context.uc_mcontext.gregs[16];

  memset(pd, 0, sizeof(pd));
  if (read(fd, &msg, sizeof(msg)) != sizeof(msg))
    errx(1, "read from sigio fd failed");
  
  pd[0].reg_num = 11;
  if (pfm_read_pmds(fd, pd, 1))
    err(1, "pfm_read_pmds");
  opdata3.val = pd[0].reg_value;
  if(opdata3.reg.ibsldop || opdata3.reg.ibsstop){
    // First read the ip address
    pd[0].reg_num = 8;
    if (pfm_read_pmds(fd, pd, 1))
      err(1, "pfm_read_pmds");
    ip = pd[0].reg_value;
    // Then read the memory address
    pd[0].reg_num = 12;
    if (pfm_read_pmds(fd, pd, 1))
      err(1, "pfm_read_pmds");
    m_addr = pd[0].reg_value;
    if(opdata3.reg.ibsldop) {
      write_ibs_info(record, tid, ip, m_addr, true, sip);
    }
    else {
      write_ibs_info(record, tid, ip, m_addr, false, sip);
    }
  }
}

// The return value is the number of 
int get_backtrace (void** buffer, int n) {
  unw_cursor_t cursor; unw_context_t uc;
  unw_word_t ip, sp;
  int i = 0;
  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);
  while (unw_step(&cursor) > 0 && i < n) {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    unw_get_reg(&cursor, UNW_REG_SP, &sp);
    buffer[i] = (void*)ip;
    i++;
    //printf ("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
  }
  return i;
}

// The signal_handler function to catch pmu interrupt signal.
static void signal_handler(int sig, siginfo_t *info, void *context) {
  int fd = info->si_fd;
  int tid = get_current_tid();
  if(fd != tid_fd_map[tid])
    printf("error, mismatch tid and fd");

  num_stat++;
  // TODO(tianweis): Investigate why we will get SIGSEG for backtrace
  // when N>10.
  void *stack_trace[N];
  int backtrace_count = 0;
  int i, size;
  struct Record record;
  memset(&record, 0, sizeof(struct Record));
  for(int i = 0; i < N; i++)
    stack_trace[i] = 0;

  // Get the stack trace now
  // Note that now we are in signal handler's frame, the top of the stack
  // frames:
  // signal_handler->(signal called in libpthread.so)->user-level code, so we must start from i = 3
  // we also will be ended by at least  three frames, which is clone(libc), thread_start(pthread) and
  // racez_start_func(libracez.so).
  if(racez_stacktrace) {
    size = get_backtrace(stack_trace, N);
    backtrace_count = size - 2;
    for (i = 0; i < backtrace_count; i++) 
      record.stack_trace_[i] = stack_trace[i+2];
    record.trace_depth_ = backtrace_count;
  } 
  // Dispatch different signal handler according to underlying PMU type .
  // We share the same functions for both core and nhm.
  if (type == PFMLIB_INTEL_CORE_PMU || type == PFMLIB_INTEL_NHM_PMU)  {
    signal_handler_pebs(&record, sig, info, context);
    // We add the randomization here for PEBS
    // The method is using rewrite the pmds with a random value.
    if(racez_sampling_adjust) {
      int random_value = rand() % 1024;
      pfarg_pmd_t pd[NUM_PMDS];
      memset(pd, 0, sizeof(pd));
      // TODO(tianweis): Now we hard code the reg_num as 0
      pd[0].reg_num = 0;
      pd[0].reg_flags |= PFM_REGFL_OVFL_NOTIFY;
      pd[0].reg_value = -(SMPL_PERIOD - random_value);
      pd[0].reg_long_reset = -(SMPL_PERIOD - random_value);
      pd[0].reg_short_reset = -(SMPL_PERIOD - random_value);
      // TODO(tianweis): Now we hard code the pmd count as 1
      if(pfm_write_pmds(fd, pd, 1)) {
        errx(1, "pfm_write_pmds in signal-handler failed\n");
      }
    }
  } else if (type == PFMLIB_AMD64_PMU) {
    signal_handler_amd_ibs(&record, sig, info, context);
  } else {
    errx(1,"we only support INTEL CORE, NHM and AMD64 now.\n");
  }

  // Restart the pmu context after reading the counter.
  if (pfm_restart(fd) != 0) {
    //fatal_error("pfm_restart error errno %d\n",errno);
    write(0,"restart failed",15);      
  }
}

static bool disable_signal_handler() {
  struct sigaction sa;
  sigset_t set;
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&set);
  sa.sa_handler = SIG_IGN;
  sa.sa_sigaction = NULL;
  sa.sa_mask = set;
  sa.sa_flags = 0;

  if (sigaction(signum, &sa, NULL) != 0) {
	printf("signal action failed");
    return false;
  }
}
// This function is protected by sig_mutex.
// only one thread can enter this at one time.
// Actually only the first thread will enter this 
// function.
static bool setup_signal_handler() {
  struct sigaction sa;
  sigset_t set;
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&set);
  sa.sa_sigaction = signal_handler;
  sa.sa_mask = set;
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(signum, &sa, NULL) != 0) {
	printf("signal action failed");
    return false;
  }
  sig_enable = true;
  return true;
}

static bool pmuprofiler_context_attach_experimental() {
  int i, fd, flags;
  pfmlib_input_param_t inp;
  pfmlib_output_param_t outp;
  pfarg_pmd_t pd[NUM_PMDS];
  pfarg_pmc_t pc[NUM_PMCS];
  pfarg_ctx_t ctx;
  pfarg_load_t load_args;
  struct f_owner_ex owner_ex;
  pid_t tid = get_current_tid();
  pid_t pid = getpid();
  
  memset(pd, 0, sizeof(pd));
  memset(pc, 0, sizeof(pc));
  memset(&inp, 0, sizeof(inp));
  memset(&outp, 0, sizeof(outp));
  
  memset(&ctx, 0, sizeof(ctx));
  memset(&load_args, 0, sizeof(load_args));
  
  inp.pfp_event_count = 1;
  inp.pfp_dfl_plm = PFM_PLM3;
  inp.pfp_flags = 0;
  /*
   * search for our sampling event
   */
  if (pfm_find_full_event(SMPL_EVENT, &inp.pfp_events[0]) != PFMLIB_SUCCESS)
    printf("cannot find sampling event %s\n", SMPL_EVENT);
  
  fd = pfm_create_context(&ctx, NULL, NULL, 0);
  if (fd < 0) {
    printf("pfm_create_context failed\n");
    errx(1,"pfm_create_context failed");
  }
 
  if (pfm_dispatch_events(&inp, NULL, &outp, NULL) != PFMLIB_SUCCESS) {
    printf("pfm_dispatch_events failed\n");
    errx(1,"pfm_dispatch_events failed");
  }
  for (i = 0; i < outp.pfp_pmc_count; i++) {
    pc[i].reg_num  =  outp.pfp_pmcs[i].reg_num;
    pc[i].reg_value = outp.pfp_pmcs[i].reg_value;
  }

  for (i = 0; i < outp.pfp_pmd_count; i++) {
    pd[i].reg_num = outp.pfp_pmds[i].reg_num;
  }

  /*
   * setup sampling period for first counter
   * we want notification on overflow, i.e., when buffer is full
   */
  pd[0].reg_flags |= PFM_REGFL_OVFL_NOTIFY;
  pd[0].reg_value = -SMPL_PERIOD;
  pd[0].reg_long_reset = -SMPL_PERIOD;
  pd[0].reg_short_reset = -SMPL_PERIOD;
  

  fd = pfm_create_context(&ctx, NULL, NULL, 0);
  

  if (pfm_write_pmcs(fd, pc, outp.pfp_pmc_count)) {
    errx(1, "pfm_write_pmcs failed");
  }
  if (pfm_write_pmds(fd, pd, outp.pfp_pmd_count)) {
    errx(1, "pfm_write_pmds failed");
  }
  load_args.load_pid = tid;

  if (pfm_load_context(fd, &load_args) != 0)
    errx(1, "pfm_load_context failed");

  flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_ASYNC) < 0)
    errx(1, "fcntl SETFL failed");

  owner_ex.type = F_OWNER_TID;
  owner_ex.pid = tid;
  if (fcntl(fd, F_SETOWN_EX, &owner_ex) < 0)
    printf("fcntl SETOWN failed");
  if (fcntl(fd, F_SETSIG, signum) < 0)
    errx(1, "fcntl SETSIG failed");

  pfm_self_start(fd);

  pthread_mutex_lock(&tid_fd_map_mutex);
  tid_fd_map[tid] = fd;
  pthread_mutex_unlock(&tid_fd_map_mutex);

  printf("tid %d and its fd is %d\n", tid, fd);
  return true;
}

// TODO(tianweis): Handle the return value correctly.
static bool pmuprofiler_context_attach_core_pebs() {
  void *buf_addr;
  int i, fd, flags;
  pfmlib_input_param_t inp;
  pfmlib_output_param_t outp;
  pfmlib_core_input_param_t mod_inp;
  pfarg_pmd_t pd[NUM_PMDS];
  pfarg_pmc_t pc[NUM_PMCS];
  pfarg_ctx_t ctx;
  pfarg_load_t load_arg;
  pfarg_msg_t msg;
  smpl_arg_t buf_arg;
  smpl_hdr_t *hdr;
  struct f_owner_ex owner_ex;
  pid_t tid = get_current_tid();
  pid_t pid = getpid();
  

  memset(pd, 0, sizeof(pd));
  memset(pc, 0, sizeof(pc));
  memset(&inp, 0, sizeof(inp));
  memset(&outp, 0, sizeof(outp));
  memset(&mod_inp, 0, sizeof(mod_inp));

  memset(&ctx, 0, sizeof(ctx));
  memset(&load_arg, 0, sizeof(load_arg));
  memset(&buf_arg, 0, sizeof(buf_arg));

  /*
   * search for our sampling event
   */
  if (pfm_find_full_event(SMPL_EVENT, &inp.pfp_events[0]) != PFMLIB_SUCCESS)
    fatal_error("cannot find sampling event %s\n", SMPL_EVENT);
  
  inp.pfp_event_count = 1;
  inp.pfp_dfl_plm = PFM_PLM3|PFM_PLM0;
  inp.pfp_flags = 0;

  /*
   * important: inform libpfm we do use PEBS
   */
  mod_inp.pfp_core_pebs.pebs_used = 1;

  /*
   * sampling buffer parameters
   */
  // TODO(tianweis): Double check if this is enough.
  buf_arg.buf_size = sizeof(core_smpl_entry_t) + sizeof(smpl_hdr_t);

  /*
   * sampling period cannot use more bits than HW counter can supoprt
   */
  buf_arg.cnt_reset[0] = -SMPL_PERIOD;

  ctx.ctx_flags = PFM_FL_OVFL_NO_MSG;

  /*
   * create context and sampling buffer
   */
  fd = pfm_create_context(&ctx, (char *)FMT_CORE_NAME, &buf_arg, sizeof(buf_arg));
  printf("errno after create_context in core_pebs is %d\n", errno);
  if (fd < 0)
    errx(1, "pfm_create_context failed");

  buf_addr = mmap(NULL, (size_t)buf_arg.buf_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (buf_addr == MAP_FAILED)
    fatal_error("cannot mmap sampling buffer errno %d\n", errno);

  // Only one thread call the pthread_key_create once.
  // TODO(tianweis): Check the return value.
  Create_Hdr_Key();
  // Store this mapped address into thread local storage.
  pthread_setspecific(Tls_Hdr_Key, buf_addr);
  hdr = (smpl_hdr_t*)buf_addr;
  
  printf("pebs_start=%p pebs_end=%p version=%u.%u.%u entry_size=%u\n",
	 hdr+1,
	 hdr+1,
	 (hdr->version >> 16) & 0xff,
	 (hdr->version >> 8) & 0xff, 
	 hdr->version & 0xff, 
	 hdr->entry_size);
  if (((hdr->version >> 16) & 0xff) < 1)
    errx(1, "invalid buffer format version");  

  printf("max PEBS entries: %zu\n", (size_t)hdr->pebs_size / hdr->entry_size);
  
  /*
   * let libpfm figure out how to assign event onto PMU registers
   */
  if (pfm_dispatch_events(&inp, &mod_inp, &outp, NULL) != PFMLIB_SUCCESS)
    fatal_error("cannot assign event %s\n", SMPL_EVENT);

  for (i = 0; i < outp.pfp_pmc_count; i++) {
    pc[i].reg_num =   outp.pfp_pmcs[i].reg_num;
    pc[i].reg_value = outp.pfp_pmcs[i].reg_value;
    /*
     * must disable 64-bit emulation on the PMC0 counter
     * PMC0 is the only counter useable with PEBS. We must disable
     * 64-bit emulation to avoid getting interrupts for each
		 * sampling period, PEBS takes care of this part.
		 */
    if (pc[i].reg_num == 0)
      pc[i].reg_flags = PFM_REGFL_NO_EMUL64;
  }
  for (i = 0; i < outp.pfp_pmd_count; i++) {
		pd[i].reg_num = outp.pfp_pmds[i].reg_num;
  }
  
  pd[0].reg_flags |= PFM_REGFL_OVFL_NOTIFY;
  pd[0].reg_value = -SMPL_PERIOD;
  pd[0].reg_long_reset = -SMPL_PERIOD;
  pd[0].reg_short_reset = -SMPL_PERIOD;
  
  if (pfm_write_pmcs(fd, pc, outp.pfp_pmc_count))
      printf("pfm_write_pmcs failed");
  
  if (pfm_write_pmds(fd, pd, outp.pfp_pmd_count))
    printf("pfm_write_pmds failed");
  
  load_arg.load_pid = tid;
  
  if (pfm_load_context(fd, &load_arg) != 0) {
    printf("errno after loading context is %d\n", errno);
    printf("pfm_load_context failed");
  }
  flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_ASYNC) < 0)
    printf("fcntl SETFL failed");
  
  owner_ex.type = F_OWNER_TID;
  owner_ex.pid = tid;
  if (fcntl(fd, F_SETOWN_EX, &owner_ex) < 0)
    printf("fcntl SETOWN failed");
  
  if (fcntl(fd, F_SETSIG, signum) < 0)
    printf("fcntl SETSIG failed");

  pthread_mutex_lock(&tid_fd_map_mutex);
  tid_fd_map[tid] = fd;
  pthread_mutex_unlock(&tid_fd_map_mutex);
  
  pfm_self_start(fd);
  printf("start monitor thread id %d with fd %d\n",tid, fd);
  //printf("errno after pfm_self_start is %d\n", errno);
}

// TODO(tianweis): Handle the return value correctly.
static bool pmuprofiler_context_attach_nhm_pebs() {
  void *buf_addr;
  int i, fd, flags;
  pfmlib_input_param_t inp;
  pfmlib_output_param_t outp;
  pfmlib_nhm_input_param_t mod_inp;
  pfmlib_options_t pfmlib_options;
  pfarg_pmd_t pd[NUM_PMDS];
  pfarg_pmc_t pc[NUM_PMCS];
  pfarg_ctx_t ctx;
  pfarg_load_t load_arg;
  pfarg_msg_t msg;
  smpl_arg_t buf_arg;
  smpl_hdr_t *hdr;
  struct f_owner_ex owner_ex;
  pid_t tid = get_current_tid();
  pid_t pid = getpid();
  if (pfm_initialize() != PFMLIB_SUCCESS)
    printf("libpfm intialization failed\n");

  printf("we are in nhm pebs attach function\n");
  memset(&pfmlib_options, 0, sizeof(pfmlib_options));
  //  pfmlib_options.pfm_debug   = 0; /* set to 1 for debug */
  //pfmlib_options.pfm_verbose = 1; /* set to 1 for verbose */
  //pfm_set_options(&pfmlib_options);

  memset(pd, 0, sizeof(pd));
  memset(pc, 0, sizeof(pc));
  memset(&inp, 0, sizeof(inp));
  memset(&outp, 0, sizeof(outp));
  memset(&mod_inp, 0, sizeof(mod_inp));

  memset(&ctx, 0, sizeof(ctx));
  memset(&load_arg, 0, sizeof(load_arg));
  memset(&buf_arg, 0, sizeof(buf_arg));

  /*
   * search for our sampling event
   */
  if (pfm_find_full_event(SMPL_EVENT, &inp.pfp_events[0]) != PFMLIB_SUCCESS)
    fatal_error("cannot find sampling event %s\n", SMPL_EVENT);
  
  inp.pfp_event_count = 1;
  inp.pfp_dfl_plm = PFM_PLM3|PFM_PLM0;
  inp.pfp_flags = 0;

  /*
   * important: inform libpfm we do use PEBS
   */
  mod_inp.pfp_nhm_pebs.pebs_used = 1;

  /*
   * sampling buffer parameters
   */
  buf_arg.buf_size = sizeof(nhm_smpl_entry_t) + sizeof(smpl_hdr_t);

  /*
   * sampling period cannot use more bits than HW counter can supoprt
   */
  buf_arg.cnt_reset[0] = -SMPL_PERIOD;

  ctx.ctx_flags = PFM_FL_OVFL_NO_MSG;

  /*
   * create context and sampling buffer
   */
  fd = pfm_create_context(&ctx, (char *)FMT_NHM_NAME, &buf_arg, sizeof(buf_arg));
  printf("errno after create_context is %d\n", errno);
  if (fd < 0)
    errx(1, "pfm_create_context failed");

  buf_addr = mmap(NULL, (size_t)buf_arg.buf_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (buf_addr == MAP_FAILED)
    fatal_error("cannot mmap sampling buffer errno %d\n", errno);

  // Only one thread call the pthread_key_create once.
  // TODO(tianweis): Check the return value.
  Create_Hdr_Key();
  // Store this mapped address into thread local storage.
  pthread_setspecific(Tls_Hdr_Key, buf_addr);
  hdr = (smpl_hdr_t*)buf_addr;
  
  printf("pebs_start=%p pebs_end=%p version=%u.%u.%u entry_size=%u\n",
	 hdr+1,
	 hdr+1,
	 (hdr->version >> 16) & 0xff,
	 (hdr->version >> 8) & 0xff, 
	 hdr->version & 0xff, 
	 hdr->entry_size);

  if (((hdr->version >> 16) & 0xff) < 1)
    errx(1, "invalid buffer format version");  

  printf("max PEBS entries: %zu\n", (size_t)hdr->pebs_size / hdr->entry_size);

  /*
   * let libpfm figure out how to assign event onto PMU registers
   */
  if (pfm_dispatch_events(&inp, &mod_inp, &outp, NULL) != PFMLIB_SUCCESS)
    fatal_error("cannot assign event %s\n", SMPL_EVENT);
  
  for (i = 0; i < outp.pfp_pmc_count; i++) {
    pc[i].reg_num =   outp.pfp_pmcs[i].reg_num;
    pc[i].reg_value = outp.pfp_pmcs[i].reg_value;
    /*
     * must disable 64-bit emulation on the PMC0 counter
     * PMC0 is the only counter useable with PEBS. We must disable
     * 64-bit emulation to avoid getting interrupts for each
		 * sampling period, PEBS takes care of this part.
		 */
    if (pc[i].reg_num == 0)
      pc[i].reg_flags = PFM_REGFL_NO_EMUL64;
  }
  for (i = 0; i < outp.pfp_pmd_count; i++) {
    pd[i].reg_num = outp.pfp_pmds[i].reg_num;
  }
  
  pd[0].reg_flags |= PFM_REGFL_OVFL_NOTIFY;
  pd[0].reg_value = -SMPL_PERIOD;
  pd[0].reg_long_reset = -SMPL_PERIOD;
  pd[0].reg_short_reset = -SMPL_PERIOD;
  
  if (pfm_write_pmcs(fd, pc, outp.pfp_pmc_count))
      printf("pfm_write_pmcs failed");
  
  if (pfm_write_pmds(fd, pd, outp.pfp_pmd_count))
    printf("pfm_write_pmds failed");
  
  load_arg.load_pid = tid;
  
  if (pfm_load_context(fd, &load_arg) != 0) {
    printf("errno after loading context is %d\n", errno);
    printf("pfm_load_context failed");
  }
  flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_ASYNC) < 0)
    printf("fcntl SETFL failed");
  
  owner_ex.type = F_OWNER_TID;
  owner_ex.pid = tid;
  if (fcntl(fd, F_SETOWN_EX, &owner_ex) < 0)
    printf("fcntl SETOWN failed");
  
  if (fcntl(fd, F_SETSIG, signum) < 0)
    printf("fcntl SETSIG failed");

  pthread_mutex_lock(&tid_fd_map_mutex);
  tid_fd_map[tid] = fd;
  pthread_mutex_unlock(&tid_fd_map_mutex);
  
  pfm_self_start(fd);
  printf("start monitor thread id %d with fd %d\n",tid, fd);
  //printf("errno after pfm_self_start is %d\n", errno);
}

// TODO(tianweis): Handle the return value correctly.
static bool pmuprofiler_context_attach_amd_ibs() {
  pfarg_pmc_t pc[NUM_PMCS];
  pfarg_pmd_t pd[NUM_PMDS];

  ibsopctl_t ibsopctl;
  struct f_owner_ex owner_ex;
  pfarg_ctx_t ctx;
  pfarg_load_t load_arg;
  int fd, flags;
  int ret;

  pid_t tid = get_current_tid();
  pid_t pid = getpid();
   
  memset(&ctx, 0, sizeof(ctx));
  memset(pc, 0, sizeof(pc));
  memset(pd, 0, sizeof(pd));
  memset(&load_arg, 0, sizeof(load_arg));
  

  ibsopctl.val = 0;
  ibsopctl.reg.ibsopen = 1;
  ibsopctl.reg.ibsopmaxcnt = 0xFFFF0 >> 4;
  
  /* PMC_IBSOPCTL */
  pc[0].reg_num = 5;
  pc[0].reg_value = ibsopctl.val ;

  /* PMD_IBSOPCTL */
  pd[0].reg_num = 7;
  pd[0].reg_value = 0;
  
  /* setup all IBSOP registers for sampling */
  pd[0].reg_flags = PFM_REGFL_OVFL_NOTIFY;
  pd[0].reg_smpl_pmds[0] =
    ((1UL << PMD_IBSOP_NUM) - 1) << 7;
  
  fd = pfm_create_context(&ctx, NULL, NULL, 0);
  if (fd < 0)
    err(1, "pfm_create_context failed");
  
  if (pfm_write_pmcs(fd, pc, 1))
    err(1, "pfm_write_pmcs failed");
  
  if (pfm_write_pmds(fd, pd, 1))
    err(1, "pfm_write_pmds failed");
  
  load_arg.load_pid = tid;
  if (pfm_load_context(fd, &load_arg) != 0)
    err(1, "pfm_load_context failed");
  
  flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_ASYNC) < 0)
    err(1, "fcntl SETFL failed");
  
  owner_ex.type = F_OWNER_TID;
  owner_ex.pid  = tid;
  
  if (fcntl(fd, F_SETOWN_EX, &owner_ex) < 0)
    printf("fcntl SETOWN failed");
  
  if (fcntl(fd, F_SETSIG, signum) < 0)
    err(1, "fcntl SETSIG failed");

  pthread_mutex_lock(&tid_fd_map_mutex);
  tid_fd_map[tid] = fd;
  pthread_mutex_unlock(&tid_fd_map_mutex);

  pfm_self_start(fd);

}

// This is the main dispatch function according to the current PMU type.
static bool pmuprofiler_context_attach() {
  if (pfm_initialize() != PFMLIB_SUCCESS)
    fatal_error("libpfm intialization failed 2\n");
  pfm_get_pmu_type(&type);  

  // TODO(tianweis) : merge core and nhm into one function.
  if (type == PFMLIB_INTEL_CORE_PMU) 
    return pmuprofiler_context_attach_core_pebs();
  else if (type == PFMLIB_INTEL_NHM_PMU)
    return pmuprofiler_context_attach_nhm_pebs();
  else if (type == PFMLIB_AMD64_PMU) {
    return pmuprofiler_context_attach_amd_ibs();
  }
  //TODO(tianweis): Add more supported PMU type here.
  else {
    // Now we should not reach here.
    assert(0);
  }
}
// Now we only support pthread_create profiler start, i.e,
// one thread start its profiler by itself. 
// TODO(tianweis): log every live thread, start them together at
// a certain time.  
int pmu_profiler_perthread_attach() {
  // TODO(tianweis): Learn the order between enable signal
  // handler and pfm_start.
  if(racez_enable == 0)
   return false;
  if(!pmuprofiler_context_attach())
    return false;
  return true;
}

// The main start function. This function can be
// called by main thread to set up pre-configuration
// flag for racez. includes:
//     a: set up the buffer to store data
//     b: set up the signal-handler
//     c: set up other global things.
// TODO(tianweis): Do we need to enable racez for this main thread.
int pmu_profiler_start() {

    pfmlib_options_t pfmlib_options;
    memset(&pfmlib_options, 0, sizeof(pfmlib_options));
    pfmlib_options.pfm_debug   = 0; /* set to 1 for debug */
    pfmlib_options.pfm_verbose = 0; /* set to 1 for verbose */
    pfm_set_options(&pfmlib_options);


  int ret = pfm_initialize();
  if (ret != PFMLIB_SUCCESS) {
    printf("pfm_stop error errno %d\n",errno);
    printf("ret is %d\n",ret);
    fatal_error("libpfm intialization failed\n");
  }
  // Check we are on an Intel Core PMU.
  pfm_get_pmu_type(&type);
  if(!(type == PFMLIB_INTEL_CORE_PMU || type == PFMLIB_INTEL_NHM_PMU || 
       type == PFMLIB_AMD64_PMU)) {
    printf("We only support intel core, nhm and amd64 CPU now\n");
    return false;
  }
  InitBuffer(file_name);
  assert(sig_enable == false);
  setup_signal_handler();
}

// The main stop function, must be put before program exit;
int pmu_profiler_stop() {
  // stop the all left fd in tid_fd_map
  pthread_mutex_lock(&tid_fd_map_mutex);
  for(TID_FD_MAP_ITER iter = tid_fd_map.begin();
      iter != tid_fd_map.end(); iter++) {
    int fd = iter->second;
    assert(fd != 0);
    printf("pfm stop %d\n", fd);
    // TODO(tianweis): Now we have some requirements for insert this pmu_profiler_stop
    // if some threads already exit, pfm_stop the exited thread will failed.
    // also we must disable all pfm fd, otherwsie left fds will give us SIGIO after
    // disable the signal handler. 
    if (pfm_stop(fd) != 0) {
      //fatal_error("pfm_stop error errno %d\n",errno);
      // printf("pfm_stop error errno %d\n",errno);
    }
  }
  pthread_mutex_unlock(&tid_fd_map_mutex);

  // The order of this disable is important, we must disable the SIGIO
  // after we pfm_stop all threads. otherwise, if this pmu_profiler_stop is
  // called by some child threads, i.e, all threads does not exit yet, 
  // some threads will continue to receieve the signal, however, we do not 
  // have any signal-handler now. 
  // TODO(tianweis): how about between this disable and pfm_stop, there is 
  // new threads entering this? we need to do some things for this. 
  disable_signal_handler();
 
  StopBuffer();
  printf("totoal samples are %d\n", num_stat);
  return true;
}
