#
# Locate Barbeque RTLib include paths and libraries

# This module defines
# BBQUE_RTLIB_INCLUDE_DIR, where to find rtlib.h, etc.
# BBQUE_RTLIB_LIBRARIES, the libraries to link against to use the RTLib.
# BBQUE_RTLIB_FOUND, If false, don't try to use RTLib.

message(STATUS "BOSP Sysroot ..............:" ${BOSP_SYSROOT})

find_path(BBQUE_RTLIB_INCLUDE_DIR bbque/rtlib.h
	HINTS ${BOSP_SYSROOT}
	PATHS ${BOSP_SYSROOT}
	PATH_SUFFIXES include
	NO_SYSTEM_ENVIRONMENT_PATH
	NO_CMAKE_SYSTEM_PATH
)

find_library(BBQUE_RTLIB_LIBRARY bbque_rtlib
	HINTS ${BOSP_SYSROOT}
	PATHS ${BOSP_SYSROOT}
	PATH_SUFFIXES lib/bbque
	NO_SYSTEM_ENVIRONMENT_PATH
	NO_CMAKE_SYSTEM_PATH
)

set(BBQUE_RTLIB_FOUND 0)
if (BBQUE_RTLIB_INCLUDE_DIR)
  if (BBQUE_RTLIB_LIBRARY)
    set(BBQUE_RTLIB_FOUND 1)
    message(STATUS "RTLib path ................: ${BBQUE_RTLIB_LIBRARY}")
    message(STATUS "RTLib include directory ...: ${BBQUE_RTLIB_INCLUDE_DIR}")
  else (BBQUE_RTLIB_INCLUDE_DIR)
    message(FATAL_ERROR "Barbeque RTLib NOT FOUND!")
  endif (BBQUE_RTLIB_LIBRARY)
endif (BBQUE_RTLIB_INCLUDE_DIR)

mark_as_advanced(
  BBQUE_RTLIB_INCLUDE_DIR
  BBQUE_RTLIB_LIBRARY
)
