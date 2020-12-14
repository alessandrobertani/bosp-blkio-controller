## Add package version definition
configure_file (
	"${PROJECT_SOURCE_DIR}/bbque/version.cc.in"
	"${PROJECT_BINARY_DIR}/bbque/version.cc" @ONLY
)

## Copy the AEM header file where it is expected by AS-RTRM
configure_file (
	"${PROJECT_SOURCE_DIR}/include/bbque/rtlib/bbque_exc.h"
	"${PROJECT_BINARY_DIR}/include/bbque/bbque_exc.h" COPYONLY
)

# Compilation flags
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBBQUE_RTLIB")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--gc-sections")

# Options for build version: DEBUG
set (CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wextra -pedantic -DBBQUE_DEBUG")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set (BBQUE_DEBUG ON)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")

# Source files
set (RTLIB_SRC bbque_rtlib bbque_rpc ${PROJECT_BINARY_DIR}/bbque/version.cc)
set (RTLIB_SRC bbque_exc ${RTLIB_SRC})

# OpenCL support
if (CONFIG_TARGET_OPENCL)
	add_definitions(-DCL_TARGET_OPENCL_VERSION=${CONFIG_TARGET_OPENCL_VERSION})
	include_directories(${OPENCL_INCLUDE_DIR})
	set (RTLIB_SRC bbque_ocl ${RTLIB_SRC})
endif (CONFIG_TARGET_OPENCL)

# FIFO based RPC channel
if (CONFIG_BBQUE_RPC_FIFO)
	set (RTLIB_SRC rpc_fifo_client ${RTLIB_SRC})
endif (CONFIG_BBQUE_RPC_FIFO)

# FIFO based with Protocols Buffers
if (CONFIG_BBQUE_RPC_PB_FIFO)
    #set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable")
    find_package(Protobuf REQUIRED)
    message(STATUS "Protobuf compiler: " ${PROTOBUF_PROTOC_EXECUTABLE})
    include_directories(SYSTEM ${Protobuf_INCLUDE_DIR})
    protobuf_generate_cpp(
	PROTO_SRCS PROTO_HDRS
	${PROJECT_SOURCE_DIR}/include/bbque/rtlib/rpc/pb_fifo/rpc_messages.proto)
    include_directories(${CMAKE_CURRENT_BINARY_DIR})
    set (RTLIB_SRC rpc_pb_message_factory ${RTLIB_SRC})
    set (RTLIB_SRC rpc_pb_fifo_client ${RTLIB_SRC})
    set (RTLIB_SRC ${PROTO_SRCS} ${PROTO_HDRS} ${RTLIB_SRC})
endif (CONFIG_BBQUE_RPC_PB_FIFO)

# Monitoring library subdirectory
if (CONFIG_BBQUE_RTLIB_MONITORS)
	add_subdirectory(monitors)
endif (CONFIG_BBQUE_RTLIB_MONITORS)


#----- Add "bbque_rtlib" target dinamic binary
add_library(bbque_rtlib SHARED ${RTLIB_SRC})

target_link_libraries(bbque_rtlib bbque_utils)

# Protocol buffer dependency (for PB_FIFO channel)
if (CONFIG_BBQUE_RPC_PB_FIFO)
    target_link_libraries(bbque_rtlib
            ${PROTOBUF_LIBRARY}
    )
endif (CONFIG_BBQUE_RPC_PB_FIFO)


# Linking dependencies for not Android-Linux targets
if (NOT CONFIG_TARGET_ANDROID)
  target_link_libraries(bbque_rtlib
	pthread
	dl
	rt
  )
endif (NOT CONFIG_TARGET_ANDROID)

# Use link path ad RPATH
set_target_properties(bbque_rtlib PROPERTIES
	INSTALL_RPATH "${CONFIG_BOSP_RUNTIME_PATH}/${BBQUE_PATH_LIBS};${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_RTLIB};${CMAKE_INSTALL_PREFIX}/lib"
	INSTALL_RPATH_USE_LINK_PATH TRUE
	FRAMEWORK ON)

set_property(TARGET bbque_rtlib PROPERTY
	PUBLIC_HEADER
		"${PROJECT_SOURCE_DIR}/include/bbque/rtlib.h"
		"${PROJECT_SOURCE_DIR}/include/bbque/rtlib/bbque_exc.h"
                "${PROTO_HDRS}"
		"${PROJECT_SOURCE_DIR}/include/bbque/rtlib/rpc/pb_fifo/rpc_pb_message_factory.h")

# Install the RTLib
install (TARGETS bbque_rtlib LIBRARY
		DESTINATION ${BBQUE_PATH_RTLIB}
		PUBLIC_HEADER DESTINATION ${BBQUE_PATH_HEADERS}
		COMPONENT BarbequeRTLib)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/bbque/utils"
		DESTINATION "${BBQUE_PATH_HEADERS}"
		FILES_MATCHING PATTERN "*.h")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/bbque/cpp11"
		DESTINATION "${BBQUE_PATH_HEADERS}"
		FILES_MATCHING PATTERN "*.h")

# RTLib language wrappers
add_subdirectory (bindings)

