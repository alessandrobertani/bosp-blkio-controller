################################################################################
# The Barbeque RTRM Building Configuration
################################################################################

# Define CMake compatibility version
cmake_minimum_required (VERSION 2.6 FATAL_ERROR)

# The name of this project.
project (BarbequeRTRM)


INCLUDE (${CONFIGPATH})
include (${PROJECT_SOURCE_DIR}/../build/platforms/BbquePMConfig.cmake)

################################################################################
# Installation items
################################################################################

# Barbeque DAEMON configuraiton
set (BBQUE_DAEMON_NAME     "bbqued")
set (BBQUE_DAEMON_UID      "root")
set (BBQUE_DAEMON_LOCKFILE "var/lock/bbqued.lock")
set (BBQUE_DAEMON_PIDFILE  "var/run/bbqued.pid")
set (BBQUE_DAEMON_LOGFILE  "var/bbque.log")
set (BBQUE_DAEMON_RUNDIR   "")

# Barbeque RPC configuration
set (BBQUE_RPC_TIMEOUT     ${CONFIG_BBQUE_RPC_TIMEOUT})

# Barbeque installation paths
set (BBQUE_PATH_BBQ        "sbin")
set (BBQUE_PATH_INIT       "etc/init.d")
set (BBQUE_PATH_DEFAULTS   "etc/default")
set (BBQUE_PATH_CONF       "etc/bbque")
set (BBQUE_PATH_PILS       "etc/bbque/pil")
set (BBQUE_PATH_RECIPES    "etc/bbque/recipes")
set (BBQUE_PATH_HEADERS    "include/bbque")
set (BBQUE_PATH_LIBS       "lib")
set (BBQUE_PATH_RTLIB      "lib/bbque")
set (BBQUE_PATH_PLUGINS    "lib/bbque/plugins")
set (BBQUE_PATH_BINDINGS   "lib/bbque/bindings")
set (BBQUE_PATH_APPS       "usr/bin")
set (BBQUE_PATH_DOCS       "usr/share/bbque")
set (BBQUE_PATH_CMAKE      "usr/share/bbque")
set (BBQUE_PATH_VAR        "var/bbque")
set (BBQUE_PATH_TEMP       "/tmp")

# Barbeque configuration file
set (BBQUE_CONF_FILE "bbque.conf")
set (BBQUE_RTLIB_CONF_FILE "rtlib.conf")

# Setup the CGroup-based Platform Integration Layers (PIL)
set (BBQUE_PIL_CGROUP_MOUNT "mnt/cgroup")
set (BBQUE_PIL_FILE         "systems.xml")

if (CONFIG_TARGET_EMULATED_HOST)
  set (CONFIG_TARGET_ID "tpd")
  set (BBQUE_PIL_FILE   "symsys.xml")
  message(STATUS "Setting CONFIG_TARGET_ID=${CONFIG_TARGET_ID}")
endif (CONFIG_TARGET_EMULATED_HOST)

if (CONFIG_TARGET_ID AND
  EXISTS "${PROJECT_SOURCE_DIR}/config/pil/${CONFIG_TARGET_ID}")
	install (FILES
		${PROJECT_SOURCE_DIR}/config/pil/${CONFIG_TARGET_ID}/${BBQUE_PIL_FILE}
		${PROJECT_SOURCE_DIR}/config/pil/${CONFIG_TARGET_ID}/${CONFIG_TARGET_ID}.xml
		DESTINATION ${BBQUE_PATH_PILS}
		COMPONENT BarbequeRTRM)
endif(CONFIG_TARGET_ID AND
  EXISTS "${PROJECT_SOURCE_DIR}/config/pil/${CONFIG_TARGET_ID}")


# Setup the RPATH and RWPATH
if ("${CONFIG_BOSP_RUNTIME_PATH}" STREQUAL "")
	set (CONFIG_BOSP_RUNTIME_PATH "${PROJECT_SOURCE_DIR}/../out")
endif ("${CONFIG_BOSP_RUNTIME_PATH}" STREQUAL "")
if ("${CONFIG_BOSP_RUNTIME_RWPATH}" STREQUAL "")
	set (CONFIG_BOSP_RUNTIME_RWPATH "${PROJECT_SOURCE_DIR}/../out/var/bbque")
endif ("${CONFIG_BOSP_RUNTIME_RWPATH}" STREQUAL "")

# Barbeque CGroups path
if(CONFIG_BBQUE_LINUX_CG_SYSTEMD)
	set (BBQUE_CGMOUNT_DIR "/sys/fs/cgroup/cpu,cpuset,cpuacct,memory,net_cls")
else(CONFIG_BBQUE_LINUX_CG_SYSTEMD)
	set (BBQUE_CGMOUNT_DIR "${CONFIG_BOSP_RUNTIME_PATH}/${BBQUE_PIL_CGROUP_MOUNT}")
endif(CONFIG_BBQUE_LINUX_CG_SYSTEMD)

################################################################################
# Common initialization settings
################################################################################

# Load additional CMake modules
list (APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/.cmake/")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/.cmake"
			DESTINATION "${BBQUE_PATH_CMAKE}"
			FILES_MATCHING PATTERN "*.cmake*")

# Make sure the compiler can find include files
include_directories (
	${PROJECT_BINARY_DIR}/include
	${PROJECT_SOURCE_DIR}/include
)

message ( STATUS )
message ( STATUS "Checking dependencies..." )

# Setup the source tree versioning number
include (GetGitRevisionDescription)
git_describe(GIT_SHA1)
set (VERSION "${GIT_SHA1}")

# Check if running un UNIX like systems
if (UNIX)
	add_definitions(-DUNIX)
endif(UNIX)


################################################################################
# Build version specific configurations
################################################################################
# Choose the type of build, options are:
#    None Debug Release RelWithDebInfo MinSizeRel

# Set default release version to RELEASE
if (NOT CMAKE_BUILD_TYPE)
	set (CMAKE_BUILD_TYPE RelWithDebInfo)
endif (NOT CMAKE_BUILD_TYPE)

# Add flags common to all builds
set (BOSP_CPP11 "0x")
if (GCC_TAG GREATER "46")
	set (BOSP_CPP11 "11")
endif (GCC_TAG GREATER "46")
if (GCC_TAG LESS "48")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_NANOSLEEP")
endif (GCC_TAG LESS "48")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++${BOSP_CPP11} -Wall")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-local-typedefs")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGCC_VERSION=${GCC_VERSION}")

# Add "Generic-Linux" specific flags
if (CONFIG_TARGET_LINUX_NATIVE)
set (BBQUE_TARGET_OS "Native-Linux")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif (CONFIG_TARGET_LINUX_NATIVE)

# Add "ARM-Linux" specific flags
if (CONFIG_TARGET_LINUX_ARM)
set (BBQUE_TARGET_OS "ARM-Linux")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif (CONFIG_TARGET_LINUX_ARM)

# Add "Android-Linux" specific flags
if (CONFIG_TARGET_ANDROID)
set (BBQUE_TARGET_OS "Android-Linux")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -latomic -I../../../out/include -D__STDINT_LIMITS -D_GLIBCXX_USE_C99_STDINT_TR1 -D__BIONIC__ -Wno-psabi -march=armv7-a -mtune=xscale -msoft-float -fexceptions -frtti -mthumb -fomit-frame-pointer -fno-strict-aliasing -finline-limit=64 -DANDROID  -Wa,--noexecstack")
endif (CONFIG_TARGET_ANDROID)

# Add "Mango-Linux" specific flags
if (CONFIG_TARGET_LINUX_MANGO)
set (BBQUE_TARGET_OS "Mango-Linux")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif (CONFIG_TARGET_LINUX_MANGO)


# Check target bitness
set (BBQUE_TARGET_ARCH 32)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  set (BBQUE_TARGET_ARCH 64)
endif (CMAKE_SIZEOF_VOID_P EQUAL 8)

set (BBQUE_BUILD_FLAVOR
	"${BBQUE_TARGET_OS} (${BBQUE_TARGET_ARCH}bit - ${CMAKE_BUILD_TYPE})")

# Options for build version: DEBUG
set (CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wextra -pedantic -DBBQUE_DEBUG")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set (BBQUE_DEBUG ON)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")

# Options for build version: DESKTOP RELEASE
set (CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Options for build version: EMBEDDED RELEASE
set (CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")

if (BBQUE_DEBUG)
	set (BBQUE_LOG_LEVEL DEBUG)
else (BBQUE_DEBUG)
	set (BBQUE_LOG_LEVEL NOTICE)
endif (BBQUE_DEBUG)


################################################################################
# Installation locations
################################################################################

# set default install prefix to project root directory
#set (CMAKE_INSTALL_PREFIX ${CONFIG_BOSP_RUNTIME_PATH})
if (CMAKE_INSTALL_PREFIX STREQUAL "")
	set (CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/out" )
endif (CMAKE_INSTALL_PREFIX STREQUAL "")

if (BOSP_SYSROOT STREQUAL "")
	set (BOSP_SYSROOT ${CMAKE_INSTALL_PREFIX})
endif (BOSP_SYSROOT STREQUAL "")

################################################################################
# Lookup for library dependencies
################################################################################


# Force usage of BOSP provided BOOST installation
set (Boost_NO_SYSTEM_PATHS ON)
set (Boost_DETAILED_FAILURE_MSG ON)
set (BOOST_ROOT ${CMAKE_INSTALL_PREFIX})
set (BOOST_INCLUDEDIR ${CMAKE_INSTALL_PREFIX})

# Check for required libs and packages (headers + lib)
find_package(Boost 1.55.0 REQUIRED system program_options filesystem serialization)
if (CONFIG_EXTERNAL_LIBCG)
	find_package(CGroup REQUIRED)
endif (CONFIG_EXTERNAL_LIBCG)

# Check for pthread
find_package (Threads REQUIRED)

# OpenCL support
if (CONFIG_TARGET_OPENCL)
	find_package(OpenCL REQUIRED)
endif (CONFIG_TARGET_OPENCL)

# NVIDIA runtime support
if (CONFIG_TARGET_NVIDIA)
	find_package(NVML REQUIRED)
endif (CONFIG_TARGET_NVIDIA)

## AMD Display Library
if (CONFIG_BBQUE_PM_AMD)
	set(ADL_ROOT_DIR ${CMAKE_SOURCE_DIR}/include/bbque/pm)
	find_package(ADL REQUIRED)
endif (CONFIG_BBQUE_PM_AMD)

## NVIDIA Display Library
if (CONFIG_BBQUE_PM_NVIDIA)
	set(NVIDIA_ROOT_DIR ${CMAKE_SOURCE_DIR}/include/bbque/pm)
endif (CONFIG_BBQUE_PM_NVIDIA)

# Add compilation dependencies
include_directories(SYSTEM
	${Boost_INCLUDE_DIRS}
	${CGroup_INCLUDE_DIRS}
	${OPENCL_INCLUDE_DIR}
	${ADL_INCLUDE_DIR}
	${NVML_INCLUDE_DIR}
)

# Add linking dependencies
link_directories(
	${Boost_LIBRARY_DIRS}
	${CGroup_LIBRARY_DIRS}
	${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_LIBS}
	${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_RTLIB}
)

set (CMAKE_INSTALL_RPATH "${CONFIG_BOSP_RUNTIME_PATH}/lib;${CONFIG_BOSP_RUNTIME_PATH}/lib/bbque")
set (CMAKE_INSTALL_RPATH_USE_LINK TRUE)


################################################################################
# Subdirectories analysis
################################################################################

# Recurse into project subfolders
add_subdirectory (rtlib)
add_subdirectory (plugins)
add_subdirectory (bbque)
add_subdirectory (tools)
add_subdirectory (demos)

if (CONFIG_BBQUE_BUILD_TESTS)
add_subdirectory(test)
endif (CONFIG_BBQUE_BUILD_TESTS)

if (CONFIG_BBQUE_TG_PROG_MODEL)
add_subdirectory(libpms) # Programming model support for task-graph based applications
add_subdirectory(libtg)  # Support for task-graph representations
endif (CONFIG_BBQUE_TG_PROG_MODEL)

if (CONFIG_BBQUE_DM)
add_subdirectory(libdci) # Support for Data Communication Interface
endif (CONFIG_BBQUE_DM)

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
add_custom_target (doc)
add_dependencies (doc doxygen)

# install HTML API documentation and manual pages
set (DOC_PATH "${BBQUE_PATH_DOCS}")

install (DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/api-doc/html
	DESTINATION ${DOC_PATH} OPTIONAL)

# install man pages into packages, scope is now project root..
install (DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/api-doc/man/man3
	DESTINATION ${DOC_PATH} OPTIONAL)

################################################################################
# Configuration resume
################################################################################

# display status message for important variables
message ( STATUS )
message ( STATUS "=====[ BarbequeRTRM - Building System Configuration ]"
                 "===========================" )
if (CONFIG_TARGET_ANDROID)
message ( STATUS "Target Android" )
endif (CONFIG_TARGET_ANDROID)
message ( STATUS "Version............... ${VERSION}" )
message ( STATUS "Build Flavor.......... ${BBQUE_BUILD_FLAVOR}" )
message ( STATUS "   Target............. ${CONFIG_TARGET_NAME} ${CONFIG_TARGET_DESCRIPTION}" )
message ( STATUS "   CFLAGS............. ${CMAKE_C_FLAGS}")
message ( STATUS " CXXFLAGS............. ${CMAKE_CXX_FLAGS}")
message ( STATUS "Binary directory...... ${CMAKE_BINARY_DIR}" )
message ( STATUS "Installation prefix... ${CMAKE_INSTALL_PREFIX}" )
message ( STATUS "   BarbequeRTRM....... ${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_BBQ}" )
message ( STATUS "   Configuration...... ${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_CONF}/${BBQUE_CONF_FILE}" )
message ( STATUS "   Recipes............ ${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_RECIPES}" )
message ( STATUS "   Plugins............ ${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_PLUGINS}" )
message ( STATUS "   Var(RPC-Channel)... ${CONFIG_BOSP_RUNTIME_RWPATH}" )
message ( STATUS "   Headers............ ${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_HEADERS}" )
message ( STATUS "   RTLib library...... ${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_RTLIB}" )
message ( STATUS "   Documentation...... ${CMAKE_BINARY_DIR}/${BBQUE_PATH_DOCS}" )
message ( STATUS "Dependencies:")
message ( STATUS "   Boost library...... ${Boost_LIBRARY_DIRS}" )
message ( STATUS "BOSP Configuration:")
message ( STATUS "   Base dir........... ${BASE_DIR}" )
message ( STATUS "   SySroot............ ${BOSP_SYSROOT}" )
message ( STATUS "   CMake Modules...... ${BOSP_CMAKE_MODULES}" )
message ( STATUS )
message ( STATUS "======================================================"
                 "==========================" )
message ( STATUS "To change build settings, run the configuration menu with:")
message ( STATUS "$ make menuconfig")
message ( STATUS "======================================================"
                 "==========================" )
message(STATUS)

