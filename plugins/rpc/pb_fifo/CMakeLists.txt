
#----- Add "RPC PB FIFO" target dynamic library
set(PLUGIN_RPC_PB_FIFO_SRC  pb_fifo_rpc fifo_plugin ${PROJECT_SOURCE_DIR}/rtlib/rpc_pb_message_factory)
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROJECT_SOURCE_DIR}/include/bbque/rtlib/rpc/pb_fifo/rpc_messages.proto)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
set(PLUGIN_RPC_PB_FIFO_SRC ${PROTO_SRCS} ${PROTO_HDRS}  ${PLUGIN_RPC_PB_FIFO_SRC})

add_library(bbque_rpc_pb_fifo MODULE ${PLUGIN_RPC_PB_FIFO_SRC})
target_link_libraries(
	bbque_rpc_pb_fifo
	${Boost_LIBRARIES}
    ${PROTOBUF_LIBRARY}
)
install(TARGETS bbque_rpc_pb_fifo LIBRARY
		DESTINATION ${BBQUE_PATH_PLUGINS}
		COMPONENT BarbequeRTRM)

#----- Add "RPC PB FIFO" specific flags
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--gc-sections")
