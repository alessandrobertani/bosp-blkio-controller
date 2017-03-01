
set (TARGET_NAME bbque_pms)

# Add "barbeque" specific flags
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBBQUE_PMSLIB")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--gc-sections")

set (SOURCE exec_synchronizer)

add_library(${TARGET_NAME} SHARED ${SOURCE})

target_link_libraries(${TARGET_NAME}
        bbque_rtlib
        bbque_tg
	${Boost_LIBRARIES}
#	bbque_utils
)

# Add "Generic-Linux" specific flags
if (CONFIG_TARGET_LINUX)
target_link_libraries(${TARGET_NAME}
	-ldl -lrt
)
endif (CONFIG_TARGET_LINUX)
# Linking dependencies for not Android-Linux targets
if (NOT CONFIG_TARGET_ANDROID)
  target_link_libraries(${TARGET_NAME} pthread)
endif (NOT CONFIG_TARGET_ANDROID)


# Use link path ad RPATH
set_property (TARGET ${TARGET_NAME} PROPERTY INSTALL_RPATH_USE_LINK_PATH TRUE)

set_property (TARGET ${TARGET_NAME} PROPERTY FRAMEWORK ON)

set_property (TARGET ${TARGET_NAME} PROPERTY PUBLIC_HEADER
		${PROJECT_SOURCE_DIR}/include/pmsl/exec_synchronizer.h

# Install the Programming Model Synchronization Library
install (TARGETS ${TARGET_NAME} LIBRARY
		DESTINATION ${BBQUE_PATH_RTLIB}/../pmsl
		PUBLIC_HEADER DESTINATION ${BBQUE_PATH_HEADERS}/../pmsl
		COMPONENT BarbequeSynclib)