
## Add package version definition
configure_file (
	"${PROJECT_SOURCE_DIR}/bbque/version.cc.in"
	"${PROJECT_BINARY_DIR}/bbque/version.cc" @ONLY
)

# Add "barbeque" specific flags
#set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=c++0x")
# This flags should be used only when a monolitic build profile will be defined
#set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
#set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--gc-sections")

# Generate configuration file
configure_file (
	"${PROJECT_SOURCE_DIR}/include/bbque/config.h.in"
	"${PROJECT_BINARY_DIR}/include/bbque/config.h"
)

# Setup configuration files based on installation paths
configure_file (
	"${PROJECT_SOURCE_DIR}/config/bbque.conf.in"
	"${PROJECT_BINARY_DIR}/config/bbque.conf"
)
configure_file (
	"${PROJECT_SOURCE_DIR}/config/bbque.conf_dbg.in"
	"${PROJECT_BINARY_DIR}/config/bbque.conf_dbg"
)
configure_file (
	"${PROJECT_SOURCE_DIR}/config/rtlib.conf.in"
	"${PROJECT_BINARY_DIR}/config/rtlib.conf"
)
configure_file (
	"${PROJECT_SOURCE_DIR}/config/bbque_2parma_demo2.conf.in"
	"${PROJECT_BINARY_DIR}/config/bbque_2parma_demo2.conf"
)

# Setup the daemon initialization script
configure_file (
	"${PROJECT_SOURCE_DIR}/config/bbqued.in"
	"${PROJECT_BINARY_DIR}/config/bbqued"
	@ONLY
)
configure_file (
	"${PROJECT_SOURCE_DIR}/config/bbqued.defaults.in"
	"${PROJECT_BINARY_DIR}/config/default/bbqued"
)

# Setup BOSP shell
configure_file (
	"${PROJECT_SOURCE_DIR}/config/bosp_init.env.in"
	"${PROJECT_BINARY_DIR}/config/bosp_init.env"
	@ONLY
)

# Setup BPL generator
if (CONFIG_BBQUE_PIL_LEGACY)
	configure_file (
		"${PROJECT_SOURCE_DIR}/config/pil/generator.in"
		"${PROJECT_BINARY_DIR}/config/pil/bbque-pil-generator"
		@ONLY
	)
endif (CONFIG_BBQUE_PIL_LEGACY)
if (CONFIG_BBQUE_PIL_LOADER)
	configure_file (
		"${PROJECT_SOURCE_DIR}/config/pil/generator_xml.in"
		"${PROJECT_BINARY_DIR}/config/pil/bbque-pil-generator"
		@ONLY
	)
endif (CONFIG_BBQUE_PIL_LOADER)


# Setup Template generator
configure_file (
	"${PROJECT_SOURCE_DIR}/config/bbque-layapp.sh.in"
	"${PROJECT_BINARY_DIR}/config/bbque-layapp"
	@ONLY
)
configure_file (
	"${PROJECT_SOURCE_DIR}/config/templates/cpp/.cproject.in"
	"${PROJECT_SOURCE_DIR}/config/templates/cpp/.cproject"
	@ONLY
)
configure_file (
	"${PROJECT_SOURCE_DIR}/config/templates/cpp/.project.in"
	"${PROJECT_SOURCE_DIR}/config/templates/cpp/.project"
	@ONLY
)


#  ----------------- Sub-directories ------------------------------------------

add_subdirectory(utils)
add_subdirectory(app)
add_subdirectory(res)

if (NOT CONFIG_TARGET_SUPPORT_CPP11)
  add_subdirectory(cpp11)
endif (NOT CONFIG_TARGET_SUPPORT_CPP11)

if (CONFIG_TARGET_ANDROID)
  add_subdirectory(android)
endif (CONFIG_TARGET_ANDROID)

if (CONFIG_BBQUE_PM)
  add_subdirectory(pm)
endif (CONFIG_BBQUE_PM)

if (CONFIG_BBQUE_EM)
  add_subdirectory(em)
endif (CONFIG_BBQUE_EM)

# --------------------- Source files ------------------------------------------

set (BARBEQUE_SRC barbeque version object configuration_manager)
set (BARBEQUE_SRC platform_services dynamic_library ${BARBEQUE_SRC})
set (BARBEQUE_SRC plugin_manager modules_factory ${BARBEQUE_SRC})
set (BARBEQUE_SRC resource_manager application_manager ${BARBEQUE_SRC})
set (BARBEQUE_SRC resource_accounter ${BARBEQUE_SRC})
set (BARBEQUE_SRC binding_manager ${BARBEQUE_SRC})
set (BARBEQUE_SRC application_proxy rpc_proxy rpc_messages ${BARBEQUE_SRC})
set (BARBEQUE_SRC command_manager ${BARBEQUE_SRC})
set (BARBEQUE_SRC signals_manager scheduler_manager ${BARBEQUE_SRC})
set (BARBEQUE_SRC synchronization_manager ${BARBEQUE_SRC})
set (BARBEQUE_SRC profile_manager ${BARBEQUE_SRC})
set (BARBEQUE_SRC daemonize ${BARBEQUE_SRC})
set (BARBEQUE_SRC resource_partition_validator ${BARBEQUE_SRC})
set (BARBEQUE_SRC platform_manager ${BARBEQUE_SRC})
set (BARBEQUE_SRC platform_proxy ${BARBEQUE_SRC})
set (BARBEQUE_SRC pp/local_platform_proxy ${BARBEQUE_SRC})

# Process management
if (CONFIG_BBQUE_LINUX_PROC_MANAGER)
	set (BARBEQUE_SRC process_manager ${BARBEQUE_SRC})
	set (BARBEQUE_SRC pp/proc_listener ${BARBEQUE_SRC})
endif (CONFIG_BBQUE_LINUX_PROC_MANAGER)

# Reliability management
if (CONFIG_BBQUE_RELIABILITY)
	set (BARBEQUE_SRC reliability_manager ${BARBEQUE_SRC})
endif (CONFIG_BBQUE_RELIABILITY)

# Data management
if (CONFIG_BBQUE_DM)
	set (BARBEQUE_SRC data_manager ${BARBEQUE_SRC})
endif (CONFIG_BBQUE_DM)

# Distributed mode
if (CONFIG_BBQUE_DIST_MODE)
	set (BARBEQUE_SRC pp/remote_platform_proxy ${BARBEQUE_SRC})
endif (CONFIG_BBQUE_DIST_MODE)

# Target platforms
if (CONFIG_TARGET_EMULATED_HOST)
	set (BARBEQUE_SRC pp/test_platform_proxy ${BARBEQUE_SRC})
endif (CONFIG_TARGET_EMULATED_HOST)

if (CONFIG_TARGET_LINUX)
	set (BARBEQUE_SRC pp/linux_platform_proxy ${BARBEQUE_SRC})
endif (CONFIG_TARGET_LINUX)

if (CONFIG_TARGET_ANDROID)
        set (BARBEQUE_SRC pp/android_platform_proxy ${BARBEQUE_SRC})
endif (CONFIG_TARGET_ANDROID)

if (CONFIG_TARGET_LINUX_MANGO)
	set (BARBEQUE_SRC pp/mango_platform_proxy  ${BARBEQUE_SRC})
endif (CONFIG_TARGET_LINUX_MANGO)

if (CONFIG_TARGET_LINUX_RECIPE)
	set (BARBEQUE_SRC pp/recipe_platform_proxy ${BARBEQUE_SRC})
endif (CONFIG_TARGET_LINUX_RECIPE)

if (CONFIG_TARGET_OPENCL)
	set (BARBEQUE_SRC pp/opencl_platform_proxy ${BARBEQUE_SRC})
endif (CONFIG_TARGET_OPENCL)

if (CONFIG_TARGET_NVIDIA)
	set (BARBEQUE_SRC pp/nvml_platform_proxy ${BARBEQUE_SRC})
endif (CONFIG_TARGET_NVIDIA)

# Power monitoring
if (CONFIG_BBQUE_PM)
	set (BARBEQUE_SRC power_monitor ${BARBEQUE_SRC})
endif (CONFIG_BBQUE_PM)


# Add "barbeque" target binary
add_executable (barbeque ${BARBEQUE_SRC})


# --------------- Linking dependencies --------------------------------------

target_link_libraries(barbeque
	bbque_utils
	bbque_resources
	bbque_apps
	${Boost_LIBRARIES}
	${CMAKE_THREAD_LIBS_INIT}
	dl
	rt
)

# Linking dependencies due to task-graph based programming models
if (CONFIG_BBQUE_TG_PROG_MODEL)
add_dependencies(barbeque
	bbque_tg)
target_link_libraries(barbeque
	bbque_tg)
endif (CONFIG_BBQUE_TG_PROG_MODEL)

# Linking dependencies for Generic-Linux targets
if (CONFIG_TARGET_LINUX)
target_link_libraries(barbeque
	${CGroup_LIBRARIES}
	-ldl -lrt
)
endif (CONFIG_TARGET_LINUX)

# Linking dependencies for Android-Linux targets
if (CONFIG_TARGET_ANDROID)
target_link_libraries(barbeque
	bbque_android
)
endif (CONFIG_TARGET_ANDROID)

# Linking dependencies for OpenCL support
if (CONFIG_TARGET_OPENCL)
target_link_libraries(barbeque
	${OPENCL_LIBRARY}
)
endif (CONFIG_TARGET_OPENCL)

if (CONFIG_TARGET_NVIDIA)
target_link_libraries(barbeque
	${NVML_LIBRARY}
)
endif (CONFIG_TARGET_NVIDIA)


# Linking PowerManagement support
if (CONFIG_BBQUE_PM)
target_link_libraries(barbeque
	bbque_pm
)
endif (CONFIG_BBQUE_PM)

# Linking EventManagement support
if (CONFIG_BBQUE_EM)
target_link_libraries(
	barbeque
	bbque_em
)
endif (CONFIG_BBQUE_EM)

# Linking network project
if (CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH)
target_link_libraries(barbeque
	netlink
)
endif (CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH)

#Linking the Mango library
if (CONFIG_TARGET_LINUX_MANGO)
  message(STATUS "MANGO installation path.....: " ${CONFIG_MANGO_ROOT_PATH})

  include(${PROJECT_SOURCE_DIR}/../../global.cmake)
  set (LIBHN_POSSIBLE_PATHS "${CONFIG_MANGO_ROOT_PATH}/lib;/usr/lib;/usr/lib64;/usr/local/lib;/usr/local/lib64")

  if (CONFIG_LIBMANGO_GN)
    find_library (HN gn
	PATHS ${LIBHN_POSSIBLE_PATHS}
	NO_DEFAULT_PATH
    )
  else (CONFIG_LIBMANGO_GN)
    find_library (HN hn
	PATHS ${LIBHN_POSSIBLE_PATHS}
	NO_DEFAULT_PATH
  )
  endif (CONFIG_LIBMANGO_GN)

  if(NOT HN)
    message(FATAL_ERROR "MANGO GN/HN library Not found")
  endif()

  include_directories (${CONFIG_MANGO_ROOT_PATH}/include/)
  target_link_libraries (barbeque ${HN})
endif (CONFIG_TARGET_LINUX_MANGO)

if (CONFIG_TARGET_LINUX_RECIPE)
  include_directories (${CONFIG_MANGO_ROOT_PATH}/include/)
endif (CONFIG_TARGET_LINUX_RECIPE)

if (CONFIG_BBQUE_RELIABILITY)
  target_link_libraries (barbeque criu)
endif (CONFIG_BBQUE_RELIABILITY)


# -------------- Static libraries and plugin linking  ------------------

# Recipe Loader
set (BBQUE_RLOADER  "bbque_rloader_${BBQUE_RLOADER_DEFAULT}")
target_link_libraries(barbeque
	-Wl,-whole-archive ${BBQUE_RLOADER} -Wl,-no-whole-archive
)
# Platform Loader
if (CONFIG_BBQUE_PIL_LOADER)
  set (BBQUE_PLOADER  "bbque_ploader_${BBQUE_PIL_LOADER_DEFAULT}")
  target_link_libraries(barbeque
	-Wl,-whole-archive ${BBQUE_PLOADER} -Wl,-no-whole-archive
  )
endif (CONFIG_BBQUE_PIL_LOADER)

# ----------------------------------------------------------------------

# Use link path ad RPATH
set_target_properties(barbeque PROPERTIES
	INSTALL_RPATH "${CONFIG_BOSP_RUNTIME_PATH}/lib;${CONFIG_BOSP_RUNTIME_PATH}/lib/bbque"
	INSTALL_RPATH_USE_LINK_PATH TRUE)

# Install the configuration file
install(FILES "${PROJECT_BINARY_DIR}/include/bbque/config.h"
		DESTINATION "${BBQUE_PATH_HEADERS}")

# Install the Barbeque target
install (TARGETS barbeque RUNTIME
		DESTINATION ${BBQUE_PATH_BBQ}
		COMPONENT BarbequeRTRM)

# Install the Barbeque configuration files
install (FILES "${PROJECT_BINARY_DIR}/config/bbque.conf"
		DESTINATION ${BBQUE_PATH_CONF}
		COMPONENT BarbequeRTRM)
install (FILES "${PROJECT_BINARY_DIR}/config/bbque.conf_dbg"
		DESTINATION ${BBQUE_PATH_CONF}
		COMPONENT BarbequeRTRM)
install (FILES "${PROJECT_BINARY_DIR}/config/rtlib.conf"
		DESTINATION ${BBQUE_PATH_CONF}
		COMPONENT BarbequeRTRM)
install (FILES "${PROJECT_BINARY_DIR}/config/bbque_2parma_demo2.conf"
		DESTINATION ${BBQUE_PATH_CONF}
		COMPONENT BarbequeDEMOS)

# Install the daemon init script and its configuration file
install (PROGRAMS "${PROJECT_BINARY_DIR}/config/bbqued"
	DESTINATION ${BBQUE_PATH_INIT}
		COMPONENT BarbequeRTRM)
install (FILES "${PROJECT_BINARY_DIR}/config/default/bbqued"
	DESTINATION ${BBQUE_PATH_DEFAULTS}
		COMPONENT BarbequeRTRM)
# Install the BOSP Shell environment setup
install (FILES "${PROJECT_BINARY_DIR}/config/bosp_init.env"
		DESTINATION ${BBQUE_PATH_CONF}
		COMPONENT BarbequeRTRM)
# Install the PIL configurations
install (PROGRAMS "${PROJECT_BINARY_DIR}/config/pil/bbque-pil-generator"
		DESTINATION ${BBQUE_PATH_BBQ}
		COMPONENT BarbequeRTRM)

install (PROGRAMS "${PROJECT_BINARY_DIR}/config/bbque-layapp"
		DESTINATION ${BBQUE_PATH_BBQ}
		COMPONENT BarbequeRTRM)

