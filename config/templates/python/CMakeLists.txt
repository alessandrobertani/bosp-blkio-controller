###############################################################################
# The AEM MyApp Python Application
################################################################################

cmake_minimum_required(VERSION 3.0)

# The name of this project.
project (MyApp)

################################################################################
# Installation items
################################################################################

set (MYAPP_PATH_BINS    "usr/bin")
set (MYAPP_PATH_CONFIG  "etc/bbque")
set (MYAPP_PATH_RECIPES "etc/bbque/recipes")

if (CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
	set (CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/out" )
endif (CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")


install(DIRECTORY "${PROJECT_SOURCE_DIR}/recipes/"
	DESTINATION "${MYAPP_PATH_RECIPES}"
	FILES_MATCHING PATTERN "*.recipe")

install(FILES "${PROJECT_SOURCE_DIR}/MyApp.py"
	PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
	DESTINATION "${MYAPP_PATH_BINS}"
	RENAME "myapp.py")

configure_file (
	"${PROJECT_SOURCE_DIR}/MyApp.conf.in"
	"${PROJECT_BINARY_DIR}/MyApp.conf")

################################################################################
# Configuration resume
################################################################################

# display status message for important variables
message ( STATUS )
message ( STATUS "=====[ Barbeque MyApp - Building System Configuration ]"
                 "=================" )
message ( STATUS "Installation prefix... ${CMAKE_INSTALL_PREFIX}" )
message ( STATUS "   Binary............. <prefix>/${MYAPP_PATH_BINS}" )
message ( STATUS "   Recipes............ <prefix>/${MYAPP_PATH_RECIPES}" )
message ( STATUS )
message ( STATUS "Default values could be changes at command line, with:")
message ( STATUS "  cmake -D<Variable>=<Value>" )
message ( STATUS "or use the '-i' cmake flag to run in interactive mode" )
message ( STATUS "======================================================"
                 "==========================" )
message(STATUS)
