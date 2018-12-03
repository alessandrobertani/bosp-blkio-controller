
#----- Add "THROTTLE" target dynamic library

if (NOT CONFIG_BBQUE_SCHEDPOL_THROTTLE)
	return(throttle)
endif(NOT CONFIG_BBQUE_SCHEDPOL_THROTTLE)

# Set the macro for the scheduling policy loading
if (CONFIG_SCHEDPOL_DEFAULT_THROTTLE)
  set (BBQUE_SCHEDPOL_DEFAULT "throttle" CACHE STRING
	  "Setting scheduling policy name" FORCE)
endif (CONFIG_SCHEDPOL_DEFAULT_THROTTLE)

set(PLUGIN_THROTTLE_SRC throttle_schedpol throttle_plugin)

add_library(bbque_schedpol_throttle MODULE ${PLUGIN_THROTTLE_SRC})

target_link_libraries(
	bbque_schedpol_throttle
	${Boost_LIBRARIES}
)

install(TARGETS bbque_schedpol_throttle LIBRARY
		DESTINATION ${BBQUE_PATH_PLUGINS}
		COMPONENT BarbequeRTRM)
