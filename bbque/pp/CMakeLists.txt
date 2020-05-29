
# ============== Headers dependencies ===================== #

if (CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH)
	include_directories (/usr/include/libnl3/)
endif ()

if (CONFIG_TARGET_LINUX_RECIPE)
	include_directories (${CONFIG_MANGO_ROOT_PATH}/include/)
endif ()

if (CONFIG_BBQUE_CR_FPGA)
	include_directories (${CONFIG_BBQUE_LIBACRE_PATH}/include/)
	include_directories (${CONFIG_BBQUE_LIBACRE_PATH}/include/acre)
	message (STATUS "Advanced Checkpoint/Restore Engine path : ${CONFIG_BBQUE_LIBACRE_PATH}")
endif ()


# ================== Source files ======================== #

set (BBQUE_PP_SRC local_platform_proxy)

# Distributed mode
if (CONFIG_BBQUE_DIST_MODE)
	set (BBQUE_PP_SRC remote_platform_proxy ${BBQUE_PP_SRC})
endif ()

# Target platforms
if (CONFIG_TARGET_EMULATED_HOST)
	set (BBQUE_PP_SRC test_platform_proxy ${BBQUE_PP_SRC})
endif ()

if (CONFIG_TARGET_LINUX)
	set (BBQUE_PP_SRC linux_platform_proxy ${BBQUE_PP_SRC})
endif ()

if (CONFIG_BBQUE_LINUX_PROC_MANAGER)
	set (BBQUE_PP_SRC proc_listener ${BBQUE_PP_SRC})
endif ()

if (CONFIG_TARGET_ANDROID)
        set (BBQUE_PP_SRC android_platform_proxy ${BBQUE_PP_SRC})
endif ()

if (CONFIG_TARGET_LINUX_MANGO)
	set (BBQUE_PP_SRC mango_platform_proxy  ${BBQUE_PP_SRC})
endif ()

if (CONFIG_TARGET_LINUX_RECIPE)
	set (BBQUE_PP_SRC recipe_platform_proxy ${BBQUE_PP_SRC})
endif ()

if (CONFIG_TARGET_OPENCL)
	set (BBQUE_PP_SRC opencl_platform_proxy ${BBQUE_PP_SRC})
endif ()

if (CONFIG_TARGET_NVIDIA)
	set (BBQUE_PP_SRC nvml_platform_proxy ${BBQUE_PP_SRC})
endif ()


# =================== Target ============================== #

add_library(bbque_pp STATIC ${BBQUE_PP_SRC})

set_target_properties (bbque_pp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============== Linking dependencies ===================== #


if (CONFIG_TARGET_LINUX)
	target_link_libraries (bbque_pp ${CGroup_LIBRARIES})
endif ()

if (CONFIG_TARGET_ANDROID)
	target_link_libraries (bbque_pp bbque_android)
endif ()

if (CONFIG_TARGET_OPENCL)
	target_link_libraries (bbque_pp ${OPENCL_LIBRARY})
endif ()

if (CONFIG_TARGET_NVIDIA)
	target_link_libraries (bbque_pp ${NVML_LIBRARY})
endif ()

if (CONFIG_BBQUE_RELIABILITY)
	target_link_libraries (bbque_pp criu)
endif ()

if (CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH)
	target_link_libraries (bbque_pp netlink)
endif ()

if (CONFIG_TARGET_LINUX_MANGO)
	message(STATUS "MANGO installation path.....: " ${CONFIG_MANGO_ROOT_PATH})
	include (${PROJECT_SOURCE_DIR}/../../global.cmake)
	set (LIBHN_POSSIBLE_PATHS "${CONFIG_MANGO_ROOT_PATH}/lib;/usr/lib;/usr/lib64;/usr/local/lib;/usr/local/lib64")

	if (CONFIG_LIBMANGO_GN)
		find_library (HN gn
			PATHS ${LIBHN_POSSIBLE_PATHS}
			NO_DEFAULT_PATH)
	else ()
		find_library (HN hn
			PATHS ${LIBHN_POSSIBLE_PATHS}
			NO_DEFAULT_PATH)
	endif ()

	if(NOT HN)
		message(FATAL_ERROR "MANGO GN/HN library Not found")
	endif()

	include_directories (${CONFIG_MANGO_ROOT_PATH}/include/)
	target_link_libraries (bbque_pp ${HN})
endif ()


# C/R for FPGA devices
if (CONFIG_BBQUE_CR_FPGA)
	find_library(LIBACRE acre_npu
	      HINTS ${CONFIG_BBQUE_LIBACRE_PATH}
	      PATH_SUFFIXES lib)
	message (STATUS "Advanced Checkpoint/Restore Engine......: ${LIBACRE}")
	target_link_libraries (bbque_pp acre)
endif ()


