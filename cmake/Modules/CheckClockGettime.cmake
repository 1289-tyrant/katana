include(CheckSymbolExists)

set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(clock_gettime time.h HAVE_CLOCK_GETTIME_INTERNAL)
if(HAVE_CLOCK_GETTIME_INTERNAL)
  set(CLOCK_GETTIME_FOUND "${HAVE_CLOCK_GETTIME_INTERNAL}")
  set(CLOCK_GETTIME_LIBRARIES)
else()
  set(CMAKE_REQUIRED_LIBRARIES "-lrt")
  check_symbol_exists(clock_gettime time.h HAVE_CLOCK_GETTIME_RT_INTERNAL)
  if(HAVE_CLOCK_GETTIME_RT_INTERNAL)
    message(STATUS "clock_gettime found (in librt)")
    set(CLOCK_GETTIME_FOUND "${HAVE_CLOCK_GETTIME_RT_INTERNAL}")
    set(CLOCK_GETTIME_LIBRARIES rt)
  endif()
endif()
