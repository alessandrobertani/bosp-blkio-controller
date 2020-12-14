#
# Locate Control Group include paths and libraries
# libcgroup can be found at http://libcg.sourceforge.net/

# This module defines
# CGroup_INCLUDE_DIR, where to find libcgroup.h, etc.
# CGroup_LIBRARIES, the libraries to link against to use libcgroup.
# CGroup_FOUND, If false, don't try to use libcgroup.

find_path(CGroup_INCLUDE_DIR libcgroup.h
	HINTS ${BOSP_SYSROOT}
	PATH_SUFFIXES include usr/include
	NO_DEFAULT_PATH
	NO_SYSTEM_ENVIRONMENT_PATH
)

find_library(CGroup_LIBRARIES cgroup
	HINTS ${BOSP_SYSROOT}
	PATH_SUFFIXES lib lib/bbque usr/lib
	NO_DEFAULT_PATH
	NO_SYSTEM_ENVIRONMENT_PATH
)

set(CGroup_FOUND 0)
if (CGroup_INCLUDE_DIR)
  if (CGroup_LIBRARIES)
    set(CGroup_FOUND 1)
    message(STATUS "CGroup library.............: ${CGroup_LIBRARIES}")
  endif (CGroup_LIBRARIES)
endif (CGroup_INCLUDE_DIR)

mark_as_advanced(
  CGroup_INCLUDE_DIR
  CGroup_LIBRARIES
)
