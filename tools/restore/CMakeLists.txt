# Define CMake compatibility version
cmake_minimum_required (VERSION 3.0 FATAL_ERROR)

set (PROJECT_NAME bbque-restore-exc)

# The name of this project.
project (${PROJECT_NAME})

# Load additional CMake modules
list (APPEND CMAKE_MODULE_PATH "${BOSP_CMAKE_MODULES}")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--gc-sections")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

# Options for build version: DEBUG
set (CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wextra -pedantic -DBBQUE_DEBUG")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set (BBQUE_DEBUG ON)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")

# Options for build version: DESKTOP RELEASE
set (CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Options for build version: EMBEDDED RELEASE
set (CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")


################################################################################
# Installation items
################################################################################

# Compatibility with GCC versions < 4.8
if (GCC_TAG LESS "48")
	add_definitions(-D_GLIBCXX_USE_NANOSLEEP)
	message(STATUS "GCC: " ${GCC_TAG})
endif(GCC_TAG LESS "48")

#----- Add the target
set(BBQUE_RESTOREXC_SRC main restore_exc)

add_executable(${PROJECT_NAME} ${BBQUE_RESTOREXC_SRC})

################################################################################
# Lookup for library dependencies
################################################################################

find_package(BbqRTLib REQUIRED)
find_package(Boost 1.58.0 REQUIRED program_options)

include_directories(
	include
	${CMAKE_INSTALL_PREFIX}/include/bbque
	${BBQUE_RTLIB_INCLUDE_DIR}
	${Boost_INCLUDE_DIRS}
)

link_directories(
	${Boost_LIBRARY_DIRS}
	${BBQUE_RTLIB_LIBRARY_DIRS}
)

target_link_libraries(${PROJECT_NAME}
	${BBQUE_RTLIB_LIBRARY}
	${Boost_LIBRARIES}
	pthread
)

#if (CONFIG_BBQUE_RELIABILITY)
  target_link_libraries (${PROJECT_NAME}
	criu)
#endif (CONFIG_BBQUE_RELIABILITY)

set_target_properties (${PROJECT_NAME} PROPERTIES
	INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib/bbque;${CMAKE_INSTALL_PREFIX}}/lib"
	INSTALL_RPATH_USE_LINK_PATH TRUE)

install (TARGETS ${PROJECT_NAME} RUNTIME
	DESTINATION "${CMAKE_INSTALL_PREFIX}/usr/bin")

install(FILES dummy.recipe
	DESTINATION "${CMAKE_INSTALL_PREFIX}/etc/bbque/recipes")

################################################################################
# Configuration resume
###############################################################################a

message ( STATUS )
message ( STATUS "===================[ ${PROJECT_NAME} - Building System Configuration ]"
                 "====================" )
message ( STATUS "Build Flavor.......... ${CMAKE_BUILD_TYPE}" )
message ( STATUS "   CFLAGS............. ${CMAKE_C_FLAGS}")
message ( STATUS " CXXFLAGS............. ${CMAKE_CXX_FLAGS}")
message ( STATUS "Installation path..... ${CMAKE_INSTALL_PREFIX}" )
message ( STATUS "Using RTLib........... ${BBQUE_RTLIB_LIBRARY}" )
message ( STATUS "Boost library......... ${Boost_LIBRARY_DIRS}" )
message ( STATUS )
message ( STATUS "Default values could be changes at command line, with:")
message ( STATUS "  cmake -D<Variable>=<Value>" )
message ( STATUS "or use the '-i' cmake flag to run in interactive mode" )
message ( STATUS "======================================================"
                 "==========================" )
message(STATUS)

