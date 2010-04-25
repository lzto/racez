READ_TOOL=read
READ_OBJ=read.o
DETECT_TOOL=detect
DETECT_OBJ=detect.o
CONVERT_TOOL=convert
CONVERT_OBJ=convert.o
# C compiler
CXX=g++
CC=gcc

# global optimization level
OPT_LEVEL = -g -O0
EXTRA_LIBS =  
#
# END of user-modifiable variables.
#

CXXFLAGS = $(OPT_LEVEL) $(EXTRA_FLAGS)
CFLAGS = $(OPT_LEVEL) $(EXTRA_FLAGS)

all: $(READ_TOOL) $(DETECT_TOOL) $(CONVERT_TOOL)

$(READ_TOOL): $(READ_OBJ)
	$(CXX) $(CXXFLAGS)  -o $@ $(READ_OBJ) $(EXTRA_LIBS)
$(DETECT_TOOL): $(DETECT_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(DETECT_OBJ) $(EXTRA_LIBS)
$(CONVERT_TOOL): $(CONVERT_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(CONVERT_OBJ) $(EXTRA_LIBS)
.cc.o:
	$(CXX) $(CXXFLAGS) -c $<
.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	/bin/rm -f *.o $(READ_TOOL) $(CONVERT_TOOL) $(DETECT_TOOL) 

