#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>
#include <perfmon/perfmon_pebs_core_smpl.h>
#include <string.h>
#include <assert.h>

#include <string>
#include <vector>
#include <algorithm>
using namespace std;

using std::string;
using std::lower_bound;

string readelf = "/usr/bin/readelf";

#define SIZE 10000

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
      int is_write_;
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


struct RacezFunctionEntry {
  RacezFunctionEntry(const string &function_name, unsigned long long offset,
  unsigned long size) :
    function_name_(function_name), offset_(offset), size_(size) { }
  string function_name_;
  unsigned long long offset_;
  unsigned long  size_;
};


struct MyCompare {
  bool operator ()( const RacezFunctionEntry* const & entry1, 
                    const RacezFunctionEntry* const & entry2) const {
    return entry1->offset_ < entry2->offset_;
  }
};

typedef std::vector<RacezFunctionEntry* > RacezFunctionEntryVect;
typedef RacezFunctionEntryVect::iterator RacezFunctionEntryVectIter;
typedef RacezFunctionEntryVect::const_iterator RacezFunctionEntryVectConstIter;

class RacezRawReader {
 public:
  RacezRawReader(const char *raw_filename, const char *sym_filename)
    : raw_filename_(raw_filename), sym_filename_(sym_filename), raw_file_(NULL), sym_file_(NULL),
      raw_buffer_(NULL), sym_buffer_(NULL), buffer_size_(0) { }

  bool ReadRawFile(FILE *raw_output_fp);
  bool ReadSymFile(RacezFunctionEntryVect *functions);
private:
  bool ReadRawLine();
  bool ReadSymLine();
  void WriteRawRecord(struct Record* record, FILE* raw_output_fp);

  void CleanUp();

  static const int kBufferSizeIncrement;
  const char *const raw_filename_;
  const char *const sym_filename_;
  FILE *raw_file_;
  FILE *sym_file_; 
  char *raw_buffer_;
  char *sym_buffer_;
  int buffer_size_;

  RacezRawReader(const RacezRawReader&);
  void operator=(const RacezRawReader&);
};

const int RacezRawReader::kBufferSizeIncrement = SIZE;

bool RacezRawReader::ReadSymLine() {
  if (feof(sym_file_))
    return false;

  char *ptr = sym_buffer_;
  int amt = buffer_size_;
  while (fgets(ptr, amt, sym_file_)) {
    int len = strlen(ptr);
    if (ptr[strlen(ptr) - 1] != '\n' && !feof(sym_file_)) {
      // TODO(tianweis): Can not handle large line now.
      assert(0);
    } else {
      return true;
    }
  }
  return false;
}

void RacezRawReader::CleanUp() {
  if (sym_file_) {
    fclose(sym_file_);
    sym_file_ = NULL;
  }

  if (sym_buffer_) {
    free(sym_buffer_);
    buffer_size_ = 0;
  }
}

void RacezRawReader::WriteRawRecord(struct Record* record,
                                    FILE* raw_output_fp) {
  if(record->type_ == MEMORY_OP) {
    fprintf(raw_output_fp, "0x%08llx\n",(unsigned long long)record->op_.memory_access_.pip_);
  } else if(record->type_== MEMORY_ALLOC) {
    // here, we do nothing.
    fprintf(raw_output_fp, "invalid\n");
  } else {
    printf("we did not support other type yet\n");
    fprintf(raw_output_fp, "invalid\n");
  }
}

bool RacezRawReader::ReadRawFile(FILE* raw_output_fp) {
  int result;
  // Open the file
  assert(raw_file_ == NULL);
  raw_file_ = fopen(raw_filename_, "r");
  if (!raw_file_) {
    fprintf(stderr, "Could not open racez raw file: %s\n", sym_filename_);
    return false;
  }
  struct Record* record = (struct Record*)malloc(sizeof(struct Record));
  memset(record, 0, sizeof(struct Record));
  //printf("size is %ld\n",sizeof(struct Record));
  while(!feof(raw_file_)) {
    //count++;
    result = fread(record, sizeof(struct Record), 1, raw_file_);
    //printf("count = %d\n", count);
    if(result != 1) {
      printf("reach end, stop reading\n");
      fclose(raw_file_);
      return true;
      //exit(0);
    }
    WriteRawRecord(record, raw_output_fp);
  }
  return true;
}


bool RacezRawReader::ReadSymFile(RacezFunctionEntryVect *functions) {
  // Open the file
  assert(sym_file_ == NULL);
  sym_file_ = fopen(sym_filename_, "r");
  if (!sym_file_) {
    fprintf(stderr, "Could not open sample profile file: %s\n", sym_filename_);
    CleanUp();
    return false;
  }

  // Initialize the buffer used to read data
  buffer_size_ = kBufferSizeIncrement;
  sym_buffer_ = (char *)malloc(buffer_size_);

  int first_occ = 0;
  // Read the data line by line
  while (ReadSymLine()) {
    char *ptr, *delim, *endptr;
    int len;

    // Extract the filename
    ptr = sym_buffer_;

    // First check if the line contains ".symtab" to indicate the start of
    // symbol table.
    // normally, it will be "Symbol table '.symtab' contains xxx entries"
    if (strstr(ptr, ".symtab") == NULL && first_occ == 0)
      continue;
    else {
      first_occ += 1;
    }

    // Skip the first and second line;
    // it looks like:
    // Num:    Value          Size Type    Bind   Vis      Ndx Name
    // 0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND 
    // 1: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS init.c
    if (first_occ <=2 ) {
      continue;
    }
    
    // first go to the first no NULL char
    while(*ptr == ' ') {
      ptr++;
    }
    
    // first one is the index, skip it
    delim = strchr(ptr, ' ');
    
    // second one is the IP address,i.e function offset
    ptr = delim + 1;
    delim = strchr(ptr, ' ');
    if (!delim)
      goto parse_error;
    *delim = '\0';
    unsigned long long offset = strtoll(ptr, &endptr, 16);
    
    // go the first no NULL char
    ptr = delim + 1;
    while(*ptr == ' ') {
      ptr++;
    }
    
    // next one is the Size.
    delim = strchr(ptr, ' ');
    if (!delim)
      goto parse_error;
    *delim = '\0';
    unsigned long size = strtoll(ptr, &endptr, 0);

    ptr = delim + 1;
    // skip the next 4 fields
    for (int i = 0; i < 4; i++) {
      while(*ptr == ' ')
        ptr++;
      delim = strchr(ptr, ' ');
      ptr = delim + 1;
    }

    // Now ptr points-to the start of last field
    // which is function name.
    delim = strchr(ptr, '\n');
    if (!delim)
      goto parse_error;
    len = delim - ptr;
    string func(ptr, len);

    //printf("function :%s, offset = %lld, size = %ld\n", func.c_str(), offset, size);

    RacezFunctionEntry *function = new RacezFunctionEntry(func, offset, size);
    // Store the sample and push back the element
    //functions->push_back( RacezFunctionEntry(func, offset, size));
    functions->push_back(function);
  }

  CleanUp();
  return true;

parse_error:
  fprintf(stderr, "Could not parse sym  file line: %s\n", sym_buffer_);
  CleanUp();
  return false;
}

void print_vect(RacezFunctionEntryVect &functions) {
  for(RacezFunctionEntryVectIter iter = functions.begin();
      iter != functions.end(); iter++) {
    printf("function :%s, offset = %lld, size = %ld\n",
           (*iter)->function_name_.c_str(),
           (*iter)->offset_, (*iter)->size_);
  }
}

// Give a offset(IP address), we convert it into "function+offset"
// currenly we directly print it into a file.
static void ConvertIP(RacezFunctionEntryVect& functions, const char* raw_output, FILE* function_output) {
  FILE* raw_output_fp = fopen(raw_output, "r");
  if (raw_output_fp == NULL) {
    printf("can not open raw output file!\n");
    exit(0);
  }

  // First order the function list, then 
  // compute the function + offset for a sampled IP address.
  sort(functions.begin(), functions.end(), MyCompare());

  RacezFunctionEntryVectConstIter low;
  char buffer[100];
  
  while(fgets(buffer, 100, raw_output_fp)) {
    if(strcmp(buffer, "invalid") == 1) {
      fprintf(function_output, "invalid\n");
      continue;
    }
    int len = strlen(buffer);
    assert(buffer[len-1] == '\n');
    char *endptr;
    unsigned long long offset = strtoll(buffer, &endptr, 16);
    RacezFunctionEntry value("random", offset, 0);
    low = lower_bound(functions.begin(), functions.end(), &value, MyCompare());
    //assert(low != functions.begin());
    if(low == functions.end() || (*low)->offset_ != offset)
      low = low -1;
    fprintf(function_output, "%s\t%lld\n", (*low)->function_name_.c_str(),
            offset-(*low)->offset_);
  }
  fclose(raw_output_fp);
}

void Combine(const char* function_output, const char* filename_output, const
             char* final_output) {
  FILE* function_fp = fopen(function_output, "r");
  if (function_fp == NULL) {
    printf("can not open function output file!\n");
    exit(0);
  }
  FILE* filename_fp = fopen(filename_output, "r");
  if (filename_fp == NULL) {
    printf("can not open filename output file!\n");
    exit(0);
  }
  FILE* final_fp = fopen(final_output, "w+");
  if (final_fp == NULL) {
    printf("can not open function output file!\n");
    exit(0);
  }
  char filename_buffer[2000];
  char function_buffer[200];
  int count = 0;
  while(fgets(filename_buffer, 2000, filename_fp) &&
        fgets(function_buffer, 200, function_fp)) {
    count++;
    if(strcmp(function_buffer, "invalid") == 0) {
      //fprintf(final_fp, "invalid\n");
      continue;
    }
    if(strncmp(filename_buffer, "??:0",4) == 0) {
      //fprintf(final_fp, "invalid\n");
      continue;
    }

    int filename_len = strlen(filename_buffer);
    int function_len = strlen(function_buffer);
    assert(filename_buffer[filename_len - 1] == '\n');
    assert(function_buffer[function_len - 1] == '\n');
    filename_buffer[filename_len - 1] = '\0';
    function_buffer[function_len - 1] = '\0';
    // NOTE: the count-1 is very imporant, the mao phase will use this to 
    // as the index to binary data.
    fprintf(final_fp, "%d\t%s\t%s\n", count-1, filename_buffer, function_buffer);
  }

  fclose(filename_fp);
  fclose(function_fp);
  fclose(final_fp);
}

// Usage: argv[1] the rawfile from racez(binary version), 
// argv[2] is the binary
// TODO(tianweis): Add the final output file
int main(int argc, char** argv) {
  if(argc < 4) {
    printf("wrong usage, should have 3 args\n");
    exit(0);
  }
  // ip.txt generated by read the raw record from racez
  // sym_output comes from the output of readelf
  const char* raw_output = "ip.txt";
  const char* sym_output = "sym.txt";

  // function_output comes from read each ip from the ip.txt
  // and query the vector generated by processing sym.txt.
  // filename_output is produced by "cat ip.txt | addr2line -e argv[2] > filename.txt".
  const char* function_output = "functions.txt";
  const char* filename_output = "filename.txt";

  // Final_out is the result file which combines the function_output +
  // filename_output. 
  char* final_output = argv[3];

  // sym and raw filename are the two input files.
  const char* sym_filename = sym_output;
  char* raw_filename = argv[1];
  

  RacezRawReader reader(raw_filename, sym_filename);

  // First we read the raw binary format record, and extract the IP address
  // into raw_output(ip.txt).
  FILE* raw_output_fp = fopen(raw_output, "w+");
  if (raw_output_fp == NULL) {
    printf("can not open raw output file!\n");
    exit(0);
  }
  reader.ReadRawFile(raw_output_fp);
  fclose(raw_output_fp);
  // Second we use readelf to get the symbol table
  // we finally get a vector and each entry records the
  // function information: name+offset+size.
  string binary = "/usr/bin/readelf";
  string option = " -s ";
  string woption = " -W ";
  string direct = " >  ";
  binary = binary + option + argv[2] + woption +  direct + sym_filename;
  printf("readelf command is %s\n", binary.c_str());

  if(system(binary.c_str()) == -1) {
    perror("can not execute readelf\n");
  }

  FILE* fp = fopen(sym_filename, "r");
  if (fp == NULL) {
    printf("can not open symbol table file!\n");
    exit(0);
  }
  RacezFunctionEntryVect functions;
  reader.ReadSymFile(&functions);

  // Third Give the function and ip_file, output function name+ offset
  FILE* function_file = fopen(function_output, "w+");
  if (function_file == NULL) {
    printf("can not create function output file!\n");
    exit(0);
  }
  ConvertIP(functions, raw_output, function_file);
  fclose(function_file);


  // Forth Give the ip_file, use addr2line to get the filename 
  string cat_binary = "/bin/cat ";
  string pipe = " | ";
  string addr_binary = "/usr/bin/addr2line";
  string addr_option = " -e ";
  string final_addr_binary = cat_binary + raw_output + pipe
    + addr_binary + addr_option + argv[2] + direct + filename_output;
  printf("addr2line command is %s\n", final_addr_binary.c_str());
  if(system(final_addr_binary.c_str()) == -1) {
    perror("can not execute addr2line\n");
  }

  // Fifth combine the function_file and filename_file into the final output
  // file.
  Combine(function_output, filename_output, final_output);
  return 0;
}
