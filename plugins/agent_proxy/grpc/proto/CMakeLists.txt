
# ----------- Google libraries dependencies ----------- #
set(GOOGLE_DIR ${CONFIG_BOSP_RUNTIME_PATH})

## Protocol Buffers compiler
find_program(PROTOBUF_COMPILER protoc
	PATHS ${CONFIG_BOSP_RUNTIME_PATH}/bin
	HINTS ${CONFIG_BOSP_RUNTIME_PATH}/bin
)
if(${PROTOBUF_COMPILER} EQUAL "PROTOBUF_COMPILER-NOTFOUND")
  message(ERROR "Protocol Buffers compiler not found!")
endif(${PROTOBUF_COMPILER} EQUAL "PROTOBUF_COMPILER-NOTFOUND")
message(STATUS "Protocol Buffers compiler: " ${PROTOBUF_COMPILER})

## Protocol buffers library
find_library(PROTOBUF_LIB protobuf PATH ${GOOGLE_DIR}/lib)
if(${PROTOBUF_LIB} EQUAL "PROTOBUF_LIB-NOTFOUND")
  message(ERROR "Protocol Buffers not found!")
endif(${PROTOBUF_LIB} EQUAL "PROTOBUF_LIB-NOTFOUND")
message(STATUS "Protocol Buffers library: " ${PROTOBUF_LIB})

## gRPC library
find_library(GRPC_LIB grpc PATH ${GOOGLE_DIR}/lib EXACT)
if(${GRPC_LIB} EQUAL "GRPC_LIB-NOTFOUND")
  message(ERROR "gRPC not found!")
endif(${GRPC_LIB} EQUAL "GRPC_LIB-NOTFOUND")
message(STATUS "gRPC library: " ${GRPCXX_LIB})

## gRPC++ library
find_library(GRPCXX_LIB grpc++ PATH ${GOOGLE_DIR}/lib EXACT)
if(${GRPCXX_LIB} EQUAL "GRPCXX_LIB-NOTFOUND")
  message(ERROR "gRPC++ not found!")
endif(${GRPCXX_LIB} EQUAL "GRPCXX_LIB-NOTFOUND")
message(STATUS "gRPC++ library: " ${GRPCXX_LIB})


# ----------- Protocol Buffers compilations ----------- #

set(PROTO_FILENAME agent_com)

add_custom_command(
    OUTPUT ${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}/${PROTO_FILENAME}.pb.cc
    COMMAND ${PROTOBUF_COMPILER}
       -I=${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}
       --cpp_out=${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}
	${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}/${PROTO_FILENAME}.proto
    DEPENDS ${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}/${PROTO_FILENAME}.proto
)

####
add_custom_command(
    OUTPUT ${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}/${PROTO_FILENAME}.grpc.pb.cc
    COMMAND ${PROTOBUF_COMPILER}
       -I=${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}
       --grpc_out=${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}
       --plugin=protoc-gen-grpc=${GOOGLE_DIR}/bin/grpc_cpp_plugin
	${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}/${PROTO_FILENAME}.proto
    DEPENDS ${BBQUE_AGENT_PROXY_PLUGIN_SOURCE_DIR}/${PROTO_DIR}/${PROTO_FILENAME}.pb.cc
)

add_library(${PROTO_LIB} STATIC
    ${PROTO_FILENAME}.pb.cc
    ${PROTO_FILENAME}.grpc.pb.cc)

include_directories(${GOOGLE_DIR}/include)

target_link_libraries(${PROTO_LIB}
    ${PROTOBUF_LIB}
)
