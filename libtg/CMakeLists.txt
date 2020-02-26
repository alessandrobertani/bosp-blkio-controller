# Sources
set (TARGET_NAME bbque_tg)
set (VERSION_STRING 1.0.0)
set (SOURCE task_graph partition)

# Output: a shared library
add_library(${TARGET_NAME} SHARED ${SOURCE})

# Include
include_directories(
	${PROJECT_BINARY_DIR}/include/tg
	${PROJECT_SOURCE_DIR}/include/tg
	${Boost_INCLUDE_DIR}
)

# Link to:
target_link_libraries(${TARGET_NAME}
	${Boost_LIBRARIES}
)

# Set the public headers to install
set_property (TARGET ${TARGET_NAME} PROPERTY PUBLIC_HEADER
	${PROJECT_SOURCE_DIR}/include/tg/event.h
	${PROJECT_SOURCE_DIR}/include/tg/mem_buffer.h
	${PROJECT_SOURCE_DIR}/include/tg/partition.h
	${PROJECT_SOURCE_DIR}/include/tg/pointers.h
	${PROJECT_SOURCE_DIR}/include/tg/profilable.h
	${PROJECT_SOURCE_DIR}/include/tg/task.h
	${PROJECT_SOURCE_DIR}/include/tg/task_graph.h
	${PROJECT_SOURCE_DIR}/include/tg/hw.h
)

set_target_properties(${TARGET_NAME} PROPERTIES
	VERSION ${VERSION_STRING}
	INSTALL_RPATH "${CONFIG_BOSP_RUNTIME_PATH}/lib;${CONFIG_BOSP_RUNTIME_PATH}/lib/bbque"
	INSTALL_RPATH_USE_LINK TRUE)

# Install the library
install (TARGETS ${TARGET_NAME} LIBRARY
	DESTINATION ${BBQUE_PATH_RTLIB}
	PUBLIC_HEADER DESTINATION ${BBQUE_PATH_HEADERS}/tg
)
