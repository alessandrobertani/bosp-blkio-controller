find_package(PythonInterp)
find_package(PythonLibs REQUIRED)

if (PYTHONLIBS_FOUND)
  include_directories ("${PYTHON_INCLUDE_DIRS}")
else ()
  message (FATAL_ERROR "Unable to find PythonLibs.")
endif()

add_library(bbque_python_bindings MODULE
  rtlib_python.cc
  rtlib_enums.cc
  rtlib_types_wrappers.cc
  rtlib_bbqueexc.cc
  )

set_target_properties(bbque_python_bindings PROPERTIES
  OUTPUT_NAME "barbeque"
  PREFIX ""
  INSTALL_RPATH "${CONFIG_BOSP_RUNTIME_PATH}/${BBQUE_PATH_LIBS};${CONFIG_BOSP_RUNTIME_PATH}/${BBQUE_PATH_RTLIB}"
  )

target_link_libraries(bbque_python_bindings
  bbque_rtlib
  ${Boost_LIBRARIES}
  ${PYTHON_LIBRARIES}
  )

install(TARGETS bbque_python_bindings LIBRARY
  DESTINATION "${BBQUE_PATH_BINDINGS}/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}"
  )
