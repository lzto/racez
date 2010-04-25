LIB=libracez.so
OBJS= racez_mutex.o racez_lockset.o racez_preload.o racez_pmuprofiler.o racez_bufferdata.o
# C compiler
CXX=g++
CC=gcc

# global optimization level
OPT_LEVEL = -g -O0
EXTRA_FLAGS = -fPIC -D_GNU_SOURCE -rdynamic 
EXTRA_LIBS = -lpthread -ldl -lstdc++ -lpfm -lunwind-x86_64 
#
# END of user-modifiable variables.
#

CXXFLAGS = $(OPT_LEVEL) $(EXTRA_FLAGS)
CFLAGS = $(OPT_LEVEL) $(EXTRA_FLAGS)

$(LIB): $(OBJS)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(OBJS) $(EXTRA_LIBS)

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<
.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	/bin/rm -f $(LIB) $(OBJS) 

