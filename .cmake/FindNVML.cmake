# Module for locating NVIDIA Display Library (NVML)
#
# Customizable variables:
#   NVML_ROOT_DIR
#     Specifies NVML's root directory. The find module uses this variable to
#     locate NVML header files. The variable will be filled automatically unless
#     explicitly set using CMake's -D command-line option. Instead of setting a
#     CMake variable, an environment variable called NVMLROOT can be used.
#
# Read-only variables:
#   NVML_FOUND
#     Indicates whether NVML has been found.
#
#   NVML_INCLUDE_DIR
#     Specifies the NVML include directories.
#
#   NVML_LIBRARY
#     Specifies the NVML libraries that should be passed to
#     target_link_libararies.

INCLUDE (FindPackageHandleStandardArgs)

set (NVML_ROOT_DIR ${CONFIG_TARGET_NVIDIA_RUNTIME_PATH})

set (INC_SUFFIXES include targets/x86_64-linux/include x86_64-linux/include)
set (LIB_SUFFIXES lib lib/x86_64-linux/)

FIND_PATH (NVML_INCLUDE_DIR
  NAMES nvml.h
        NVML/nvml.h
        nvml/nvml.h
  HINTS ${NVML_ROOT_DIR}
  PATH_SUFFIXES ${INC_SUFFIXES}
  DOC "NVIDIA Display Library (NVML) include directory")

FIND_LIBRARY(NVML_LIBRARY
  NAMES libnvidia-ml.so
  HINTS ${NVML_LIBRARY_DIR} ${NVML_ROOT_DIR}
  PATH_SUFFIXES ${LIB_SUFFIXES}
  DOC "NVIDIA Display Library (NVML) location")

MESSAGE("** NVIDIA Display Library (NVML) root directory .....: " ${NVML_ROOT_DIR})
IF (NVML_INCLUDE_DIR)
  MESSAGE("** NVIDIA Display Library (NVML) include directory...: " ${NVML_INCLUDE_DIR})
ENDIF (NVML_INCLUDE_DIR)
IF (NVML_LIBRARY)
  MESSAGE("** NVIDIA Display Library (NVML) library.............: " ${NVML_LIBRARY})
ELSE (NVML_LIBRARY)
  MESSAGE("** NVIDIA Display Library (NVML) library.............: NOT FOUND")
  MESSAGE("")
  MESSAGE("If your have an NVIDIA GPU, please install NVIDIA Device Drivers!")
  MESSAGE("Otherwise, you should configure BOSP to DISABLE NVIDIA Power Management")
  MESSAGE("")
ENDIF (NVML_LIBRARY)

MARK_AS_ADVANCED (NVML_INCLUDE_DIR NVML_LIBRARY)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(NVML DEFAULT_MSG NVML_INCLUDE_DIR)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(NVML DEFAULT_MSG NVML_LIBRARY)

