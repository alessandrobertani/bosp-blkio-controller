
# Add the power manager
set (POWER_MANAGER_SRC power_manager)

# Add model manages and platform P/T models
set (POWER_MANAGER_SRC model_manager ${POWER_MANAGER_SRC})
add_subdirectory(models)

# Add the battery manager
if (CONFIG_BBQUE_PM_BATTERY)
	set (POWER_MANAGER_LIBS boost_filesystem ${POWER_MANAGER_LIBS})
	set (POWER_MANAGER_SRC battery_manager ${POWER_MANAGER_SRC})
	set (POWER_MANAGER_SRC battery ${POWER_MANAGER_SRC})
endif ()

# Add CPUs power manager
if (CONFIG_BBQUE_PM_CPU)
	set (POWER_MANAGER_SRC power_manager_cpu ${POWER_MANAGER_SRC})
	if (CONFIG_TARGET_ODROID_XU)
		set (POWER_MANAGER_SRC power_manager_cpu_odroidxu ${POWER_MANAGER_SRC})
	endif ()
	set (POWER_MANAGER_LIBS boost_regex ${POWER_MANAGER_LIBS})
endif ()

# Add NVIDIA GPUs power manager
if (CONFIG_BBQUE_PM_NVIDIA)
	set (POWER_MANAGER_SRC power_manager_nvidia ${POWER_MANAGER_SRC})
	set (POWER_MANAGER_LIBS ${NVML_LIBRARY} ${POWER_MANAGER_LIBS})
	include_directories(
		 ${NVML_INCLUDE_DIR})
endif ()

# Add AMD GPUs power manager
if (CONFIG_BBQUE_PM_AMD)
	set (POWER_MANAGER_SRC power_manager_amd ${POWER_MANAGER_SRC})
	set (POWER_MANAGER_LIBS ${ADL_LIBRARY} ${POWER_MANAGER_LIBS})
endif ()

# Add ARM Mali GPU power manager
if (CONFIG_BBQUE_PM_GPU_ARM_MALI)
	set (POWER_MANAGER_SRC power_manager_gpu_arm_mali ${POWER_MANAGER_SRC})
endif ()

# H2020-MANGO power manager
if (CONFIG_BBQUE_PM_MANGO)
	set (POWER_MANAGER_SRC power_manager_mango ${POWER_MANAGER_SRC})
	include_directories(
		${CONFIG_MANGO_ROOT_PATH}/include
	)
	set (POWER_MANAGER_LIBS ${HN} ${POWER_MANAGER_LIBS})
endif ()

# H2020-RECIPE power manager
if (CONFIG_BBQUE_PM_RECIPE)
	set (POWER_MANAGER_SRC power_manager_recipe ${POWER_MANAGER_SRC})
        message (STATUS "FPGA Monitoring Library path...: ${CONFIG_BBQUE_LIBFPGAMON_PATH}")
	include_directories(
	      ${CONFIG_BBQUE_LIBFPGAMON_PATH}/include
	)

	find_library(LIBFPGAMON fpgamon
		      HINTS ${CONFIG_BBQUE_LIBFPGAMON_PATH}
		      PATH_SUFFIXES lib)
	message (STATUS "FPGA Monitoring Library........: ${LIBFPGAMON}")
	set (POWER_MANAGER_LIBS ${LIBFPGAMON} ${POWER_MANAGER_LIBS})
endif ()

# Add as library
add_library(bbque_pm STATIC ${POWER_MANAGER_SRC})

# Link static library
target_link_libraries(
	bbque_pm
	bbque_pm_models
	${POWER_MANAGER_LIBS}
)
