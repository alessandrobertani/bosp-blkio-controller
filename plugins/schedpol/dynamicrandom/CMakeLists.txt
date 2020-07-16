
#----- Add "DYNAMICRANDOM" target dynamic library

if (NOT CONFIG_BBQUE_SCHEDPOL_DYNAMICRANDOM)
	return(dynamicrandom)
endif(NOT CONFIG_BBQUE_SCHEDPOL_DYNAMICRANDOM)

# Set the macro for the scheduling policy loading
if (CONFIG_BBQUE_SCHEDPOL_DEFAULT_DYNAMICRANDOM)
  set (BBQUE_SCHEDPOL_DEFAULT "dynamicrandom" CACHE STRING
	  "Setting scheduling policy name" FORCE)
endif (CONFIG_BBQUE_SCHEDPOL_DEFAULT_DYNAMICRANDOM)

set(PLUGIN_DYNAMICRANDOM_SRC dynamicrandom_schedpol dynamicrandom_plugin)

add_library(bbque_schedpol_dynamicrandom MODULE ${PLUGIN_DYNAMICRANDOM_SRC})

target_link_libraries(
	bbque_schedpol_dynamicrandom
	${Boost_LIBRARIES}
)

install(TARGETS bbque_schedpol_dynamicrandom LIBRARY
		DESTINATION ${BBQUE_PATH_PLUGINS}
		COMPONENT BarbequeRTRM)
