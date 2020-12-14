
if (NOT CONFIG_BBQUE_RLOADER_RXML)
	return(rxml)
endif(NOT CONFIG_BBQUE_RLOADER_RXML)

# Set the macro for the scheduling policy loading
if (CONFIG_BBQUE_RLOADER_DEFAULT_RXML)
  set (BBQUE_RLOADER_DEFAULT "rxml" CACHE STRING "Setting recipe loader name" FORCE)
endif (CONFIG_BBQUE_RLOADER_DEFAULT_RXML)

# Sources
set(PLUGIN_RXML_SRC rxml_rloader rxml_plugin)

add_library(bbque_rloader_rxml STATIC ${PLUGIN_RXML_SRC})

include_directories(
    ${CMAKE_INSTALL_PREFIX}/include
)

target_link_libraries(bbque_rloader_rxml
    ${Boost_LIBRARIES}
)

set_target_properties(bbque_rloader_rxml PROPERTIES
	CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${BBQUE_PATH_LIBS}/"
	CMAKE_INSTALL_RPATH_USE_LINK TRUE)

