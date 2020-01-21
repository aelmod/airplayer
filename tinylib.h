#include <cstdint>
#include <ntdef.h>
#include <profileapi.h>

extern uint64_t rx_hrtime();

extern uint64_t rx_hrtime()
{
#if defined(__APPLE__)
  mach_timebase_info_data_t info;
  if(mach_timebase_info(&info) != KERN_SUCCESS) {
    abort();
  }
  return mach_absolute_time() * info.numer / info.denom;

#elif defined(__linux)
  static clock_t fast_clock_id = -1;
  struct timespec t;
  clock_t clock_id;

  if(fast_clock_id == -1) {
    if(clock_getres(CLOCK_MONOTONIC_COARSE, &t) == 0 && t.tv_nsec <= 1 * 1000 * 1000LLU) {
      fast_clock_id = CLOCK_MONOTONIC_COARSE;
    }
    else {
      fast_clock_id = CLOCK_MONOTONIC;
    }
  }

  clock_id =  CLOCK_MONOTONIC;
  if(clock_gettime(clock_id, &t)) {
    return 0;
  }

  return t.tv_sec * (uint64_t)1e9 +t.tv_nsec;

#elif defined(_WIN32)
  LARGE_INTEGER timer_freq;
  LARGE_INTEGER timer_time;
  QueryPerformanceCounter(&timer_time);
  QueryPerformanceFrequency(&timer_freq);
  static double freq = (double) timer_freq.QuadPart / (double) 1000000000;
  return (uint64_t) ((double) timer_time.QuadPart / freq);
#endif
};