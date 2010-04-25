// This file defines the interface for pmuprofilez.
#ifndef RACEZ_PMUPROFILER_H
#define RACEZ_PMUPROFILER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern int pmu_profiler_start();
extern int pmu_profiler_perthread_attach();
extern int pmu_profiler_stop();

#ifdef __cplusplus
}
#endif

#endif  /* ! APR_RACEZ_PMUPROFILER_H */
