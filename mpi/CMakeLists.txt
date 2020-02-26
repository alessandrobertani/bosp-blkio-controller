# Define CMake compatibility version
cmake_minimum_required (VERSION 2.6 FATAL_ERROR)

# The name of this project.
project (mpirun)

# Load additional CMake modules
list (APPEND CMAKE_MODULE_PATH
	"${BOSP_CMAKE_MODULES}")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBBQUE_MPI")
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

set (MPIRUN_PATH_BINS    "${CMAKE_INSTALL_PREFIX}/usr/bin")
set (MPIRUN_PATH_CONFIG  "${CMAKE_INSTALL_PREFIX}/etc/bbque")
set (MPIRUN_PATH_RECIPES "${CMAKE_INSTALL_PREFIX}/etc/bbque/recipes")
set (MPIRUN_PATH_DOCS    "${CMAKE_INSTALL_PREFIX}/usr/share/bbque/bbque-mpirun")

set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib/bbque;${CMAKE_INSTALL_PREFIX}}/lib")
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


# Compatibility with GCC versions < 4.8
if (GCC_TAG LESS "48")
	add_definitions(-D_GLIBCXX_USE_NANOSLEEP)
	message(STATUS "GCC: " ${GCC_TAG})
endif(GCC_TAG LESS "48")


################################################################################
# Lookup for library dependencies
################################################################################

#----- Check for the required RTLib library
find_package(BbqRTLib REQUIRED)

# Force usage of BOSP provided BOOST installation
unset(Boost_INCLUDE_DIR CACHE)
unset(Boost_LIBRARY_DIRS CACHE)
set (Boost_NO_SYSTEM_PATHS ON)
set (BOOST_ROOT ${CMAKE_INSTALL_PREFIX})

# Check for required libs and packages (headers + lib)
find_package(Boost 1.45.0 REQUIRED program_options)


#----- Add "mpirun" target application
set(MPIRUN_SRC
	mpirun_core
	mpirun_exc
	mpirun_start
	process_checker
	command_manager
	config
)
add_executable(bbque-mpirun ${MPIRUN_SRC})


#----- Add compilation dependencies
include_directories(
	./include/
	${BBQUE_PATH_HEADERS}
	${BBQUE_RTLIB_INCLUDE_DIR}
	${Boost_INCLUDE_DIRS}
)


# Add linking dependencies
link_directories(
	${Boost_LIBRARY_DIRS}
	${BBQUE_RTLIB_LIBRARY_DIRS}
)

#----- Linking dependencies
target_link_libraries(bbque-mpirun
	${Boost_LIBRARIES}
	${BBQUE_RTLIB_LIBRARY}
	pthread
)

# Use link path ad RPATH
set_property(TARGET bbque-mpirun PROPERTY
	INSTALL_RPATH_USE_LINK_PATH TRUE)

#----- Install bbque-mpirun
install (TARGETS bbque-mpirun RUNTIME
	DESTINATION ${MPIRUN_PATH_BINS})

install(DIRECTORY "${PROJECT_SOURCE_DIR}/recipes/"
	DESTINATION "${MPIRUN_PATH_RECIPES}"
	FILES_MATCHING PATTERN "*.recipe")

################################################################################
# Doxygen Documentation
################################################################################
# check if doxygen is even installed
find_package(Doxygen)
if (DOXYGEN_FOUND STREQUAL "NO")
	message(FATAL_ERROR "Doxygen not found. "
		"Please get a copy http://www.doxygen.org")
endif (DOXYGEN_FOUND STREQUAL "NO")
# prepare doxygen configuration file
configure_file (${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
	${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
# add doxygen as target
add_custom_target (doxygen ${DOXYGEN_EXECUTABLE}
	${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
# cleanup $build/api-doc on "make clean"
set_property(DIRECTORY APPEND PROPERTY
		     ADDITIONAL_MAKE_CLEAN_FILES api-doc)
# add doxygen as dependency to doc-target
get_target_property (DOC_TARGET doc TYPE)
if (NOT DOC_TARGET)
	add_custom_target(doc)
endif ()
add_dependencies (doc doxygen)
# install HTML API documentation and manual pages
set (DOC_PATH "${RTLIB_TUTORIAL_PATH_DOCS}")
install (DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/api-doc/html
	DESTINATION ${MPIRUN_PATH_DOCS} OPTIONAL)
# install man pages into packages, scope is now project root..
install (DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/api-doc/man/man3
	DESTINATION ${MPIRUN_PATH_DOCS} OPTIONAL)


################################################################################
# Configuration resume
###############################################################################a
# Install MpiRun configuration file
configure_file (
	"${PROJECT_SOURCE_DIR}/mpirun.conf.in"
        "${PROJECT_BINARY_DIR}/bbque-mpirun.conf"
)

install (FILES "${PROJECT_BINARY_DIR}/bbque-mpirun.conf"
	DESTINATION ${MPIRUN_PATH_CONFIG})


# display status message for important variables
message ( STATUS )
message ( STATUS "===================[ bbque-mpirun - Building System Configuration ]"
                 "====================" )
message ( STATUS "Version............... ${VERSION}" )
message ( STATUS "Build Flavor.......... ${CMAKE_BUILD_TYPE}" )
message ( STATUS "   Target............. ${CONFIG_TARGET_NAME} ${CONFIG_TARGET_DESCRIPTION}" )
message ( STATUS "   CFLAGS............. ${CMAKE_C_FLAGS}")
message ( STATUS " CXXFLAGS............. ${CMAKE_CXX_FLAGS}")
message ( STATUS "Installation prefix... ${CMAKE_INSTALL_PREFIX}" )
message ( STATUS "   Binary............. ${MPIRUN_PATH_BINS}" )
message ( STATUS "   Documentation...... ${MPIRUN_PATH_DOCS}" )
message ( STATUS "Using RTLib........... ${BBQUE_RTLIB_LIBRARY}" )
message ( STATUS "Boost library......... ${Boost_LIBRARY_DIRS}" )
message ( STATUS )
message ( STATUS "Default values could be changes at command line, with:")
message ( STATUS "  cmake -D<Variable>=<Value>" )
message ( STATUS "or use the '-i' cmake flag to run in interactive mode" )
message ( STATUS "======================================================"
                 "==========================" )
message(STATUS)

