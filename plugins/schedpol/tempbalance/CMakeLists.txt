
#----- Add "TEMPBALANCE" target dynamic library

if (NOT CONFIG_BBQUE_SCHEDPOL_TEMPBALANCE)
	return(tempbalance)
endif(NOT CONFIG_BBQUE_SCHEDPOL_TEMPBALANCE)

# Set the macro for the scheduling policy loading
if (CONFIG_BBQUE_SCHEDPOL_DEFAULT_TEMPBALANCE)
  set (BBQUE_SCHEDPOL_DEFAULT "tempbalance" CACHE STRING
	  "Setting scheduling policy name" FORCE)
endif (CONFIG_BBQUE_SCHEDPOL_DEFAULT_TEMPBALANCE)

set(PLUGIN_TEMPBALANCE_SRC tempbalance_schedpol tempbalance_plugin)

add_library(bbque_schedpol_tempbalance MODULE ${PLUGIN_TEMPBALANCE_SRC})

target_link_libraries(
	bbque_schedpol_tempbalance
	${Boost_LIBRARIES}
)

install(TARGETS bbque_schedpol_tempbalance LIBRARY
		DESTINATION ${BBQUE_PATH_PLUGINS}
		COMPONENT BarbequeRTRM)
