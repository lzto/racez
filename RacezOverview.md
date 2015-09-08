This page shows the overview of Racez's architecture. The paper for Racez has been accepted by ICSE'11 and will appear next May at Waikiki, Honolulu, Hawaii.

# Introduction #
All data race detection tools require traces containing synchronization
operations and memory references. Traditional race detection
methods instrument both and apply race detection algorithms for
online or postmortem analysis. As illustrated in the following example, traditional
methods instrument the lock and unlock operations, as well
as the two memory operations(read,write). RACEZ also instruments
the lock/unlock operations. However, it uses a different approach
to obtain the trace of memory addresses. Instead of instrumenting
each memory access, addresses are obtained from PMU samples.

# Examples #

A program with a Race
```
1 T1:
2 Lock(L)
3 ..........
4 Write X
5 ..........
6 Unlock(L)
7 .........
8 T2:
9 ..........
10 Read X
11 ........
```

Traditional method:
```
T1:
//Instrument lock operation
Lock(L)
..........
//Instrument Memory operation
Write X
..........
Unlock(L)
//Instrument unlock operation
T2:
..........
//Instrument Memory operation
Read X
.....
```

Racez Method:
```
T1:
//Instrument lock operation
Lock(L)
..........
//Read the address from PMU
Write X
..........
Unlock(L)
//Instrument unlock operation
T2:
..........
//Read the address from PMU
Read

```

# Architecture #
Racez uses self-monitoring to collect PMU samples
from the server application; i.e., RACEZ runs as a component of the
target application and resides in the same address space. With monitoring
enabled, the thread library redirects synchronization calls
to wrapper functions which update lockset information for each
thread. A separate signal handler retrieves memory address information
whenever an instruction of that thread is sampled by the
PMU. The hardware PMU is accessed through a kernel system call
interface. For each sample, the PMU generates processor interrupts
which the kernel eventually transforms into an asynchronous signal
delivered to the user application.

For the code in above figure, when the Write operation is sampled
by the PMU, the signal handler in T1 records the current register
values together with the currently held lockset {L} to the log file. If
a Read operation of the same shared variable with a disjoint lockset
is sampled by the PMU in another thread, RACEZ’s offline analysis
tool will report a warning for these two memory references.