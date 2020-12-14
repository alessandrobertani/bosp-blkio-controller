
# Add EventManager main class
set (EVENT_MANAGER_SRC event event_wrapper event_manager)

# Add as library
add_library(bbque_em STATIC ${EVENT_MANAGER_SRC})

# Link static library
target_link_libraries(
	bbque_em
	${Boost_LIBRARIES}
	bbque_logger
)

install(TARGETS bbque_em ARCHIVE
	DESTINATION "${CONFIG_BOSP_RUNTIME_PATH}/${BBQUE_PATH_LIBS}")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/bbque/em"
	DESTINATION "${BBQUE_PATH_HEADERS}"
	FILES_MATCHING PATTERN "*.h")

