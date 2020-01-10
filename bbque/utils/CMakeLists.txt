
# Build subdirs
add_subdirectory(logging)

# Add sources in the current directory to the target binary
set (BBQUE_UTILS_SRC timer deferrable worker)
set (BBQUE_UTILS_SRC ${BBQUE_UTILS_SRC} metrics_collector)
set (BBQUE_UTILS_SRC ${BBQUE_UTILS_SRC} extra_data_container)
set (BBQUE_UTILS_SRC ${BBQUE_UTILS_SRC} schedlog)

if(CONFIG_BBQUE_BUILD_DEBUG)
	set (BBQUE_UTILS_SRC ${BBQUE_UTILS_SRC} assert)
endif(CONFIG_BBQUE_BUILD_DEBUG)

if (CONFIG_BBQUE_RTLIB_PERF_SUPPORT)
	set (BBQUE_UTILS_SRC ${BBQUE_UTILS_SRC} perf)
endif (CONFIG_BBQUE_RTLIB_PERF_SUPPORT)
if (CONFIG_BBQUE_RTLIB_CGROUPS_SUPPORT)
	set (BBQUE_UTILS_SRC ${BBQUE_UTILS_SRC} cgroups)
endif (CONFIG_BBQUE_RTLIB_CGROUPS_SUPPORT)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#Add as library
add_library(bbque_utils STATIC ${BBQUE_UTILS_SRC})

# Linking dependencies
target_link_libraries(
	bbque_utils
	${CGroup_LIBRARIES}
	bbque_logger
)

