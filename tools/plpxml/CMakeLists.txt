if (CONFIG_BBQUE_PIL_LOADER)

	# Add "barbeque" specific flags
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=c++0x")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--gc-sections")

	#----- Add thereafter all the regression tests we want to run
	set(BBQUE_PLPXML_SRC plp_translator plpxml)


	#----- Add "bbque-plp" target application
	add_executable(bbque-plp ${BBQUE_PLPXML_SRC})

	target_link_libraries(bbque-plp
		${Boost_LIBRARIES})

	set_target_properties(${TARGET_NAME} PROPERTIES
		INSTALL_RPATH "${CONFIG_BOSP_RUNTIME_PATH}/lib:${CONFIG_BOSP_RUNTIME_PATH}/lib/bbque"
		INSTALL_RPATH_USE_LINK TRUE)

        install(TARGETS bbque-plp
                DESTINATION ${BBQUE_PATH_BBQ}
                COMPONENT BarbequeTOOLS)

endif (CONFIG_BBQUE_PIL_LOADER)


