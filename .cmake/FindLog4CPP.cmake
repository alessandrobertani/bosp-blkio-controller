#
# Locate log4cpp include paths and libraries
# log4cpp can be found at http://log4cpp.sourceforge.net/
# Written by Manfred Ulz, manfred.ulz_at_tugraz.at

# This module defines
# LOG4CPP_INCLUDE_DIR, where to find ptlib.h, etc.
# LOG4CPP_LIBRARIES, the libraries to link against to use pwlib.
# LOG4CPP_FOUND, If false, don't try to use pwlib.

set(LIB_PATH ${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_LIBS})
#message(STATUS "Log4CPP looking into: " ${LIB_PATH})

find_path(LOG4CPP_INCLUDE_DIR log4cpp/Category.hh)
find_library(LOG4CPP_LIBRARIES log4cpp
	PATHS ${LIB_PATH}
	HINTS ${LIB_PATH}
)

set(LOG4CPP_FOUND 0)
if (LOG4CPP_INCLUDE_DIR)
  if (LOG4CPP_LIBRARIES)
    set(LOG4CPP_FOUND 1)
    message(STATUS "Log4CPP location ............: ${LOG4CPP_LIBRARIES}")
  endif (LOG4CPP_LIBRARIES)
endif (LOG4CPP_INCLUDE_DIR)

mark_as_advanced(
  LOG4CPP_INCLUDE_DIR
  LOG4CPP_LIBRARIES
)
