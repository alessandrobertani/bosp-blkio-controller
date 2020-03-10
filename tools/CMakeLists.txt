
# .:: Mandatory Tools required for the usage of the BarbequeRTRM
# These tools must be:
# - part of the BarbequeTOOLS componet
# - have a name starting by "bbque-"

set (BBQUE_TOOLS_INSTALL_DIR ${CONFIG_BOSP_RUNTIME_PATH}/sbin/bbque/)

#----- Deploy the PIL Layouts parser and CGroup generator script
if (CONFIG_BBQUE_PIL_LEGACY)
	install(PROGRAMS bbquePlatformLayoutParser.awk
		DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
		COMPONENT BarbequeTOOLS
		RENAME bbque-plp)
endif (CONFIG_BBQUE_PIL_LEGACY)

add_subdirectory(plpxml)

# .:: Accessory Tools to simplify the usage of the BarbequeRTRM
# These tools must be:
# - part of the BarbequeUTILS componet
# - have a name starting by "bbque-"

#----- Deploy the BBQ CGroups Release Agent
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/bbque-release-agent.in"
	"${PROJECT_BINARY_DIR}/tools/bbque-release-agent"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/bbque-release-agent"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-release-agent)


#----- Deploy the BBQ Control Console script
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/bbqueConsole.sh.in"
	"${PROJECT_BINARY_DIR}/tools/bbqueConsole.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/bbqueConsole.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-console)


#----- Deploy the BBQ Stress Test script and recipe generator
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/test/bbqueStressTest.sh.in"
	"${PROJECT_BINARY_DIR}/tools/test/bbqueStressTest.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/test/bbqueStressTest.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-test-stress)


configure_file (
	"${PROJECT_SOURCE_DIR}/tools/test/bbqueRecipeGenerator.sh.in"
	"${PROJECT_BINARY_DIR}/tools/monitor/bbqueRecipeGenerator.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/monitor/bbqueRecipeGenerator.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	RENAME bbque-recipegen)


#----- Deploy the launcher for multiple bbque-testapp instance
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/test/bbqueTestappLauncher.sh.in"
	"${PROJECT_BINARY_DIR}/tools/test/bbqueTestappLauncher.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/test/bbqueTestappLauncher.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-testapp-launcher)


#----- Deploy the BBQ OP List generator script
install(PROGRAMS "${PROJECT_SOURCE_DIR}/tools/bbqueOpListBuilder.awk"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-oplb)


#----- Deploy the BBQ Metrics Plotting scripts
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/monitor/bbqueLogParser.awk.in"
	"${PROJECT_BINARY_DIR}/tools/monitor/bbqueLogParser.awk"
	@ONLY
)
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/monitor/bbqueLogPlots.sh.in"
	"${PROJECT_BINARY_DIR}/tools/monitor/bbqueLogPlots.sh"
	@ONLY
)

install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/monitor/bbqueLogParser.awk"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-logparser)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/monitor/bbqueLogPlots.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-logplots)


# ----- CPU Usage dump
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/monitor/bbqueCpuUsageDumpParser.awk.in"
	"${PROJECT_BINARY_DIR}/tools/monitor/bbqueCpuUsageDumpParser.awk"
	@ONLY
)
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/monitor/bbqueCpuUsageDump.sh.in"
	"${PROJECT_BINARY_DIR}/tools/monitor/bbqueCpuUsageDump.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/monitor/bbqueCpuUsageDumpParser.awk"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	RENAME bbque-cpu-parser)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/monitor/bbqueCpuUsageDump.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	RENAME bbque-cpu-dump)


#----- Deploy the BBQ Signal Stability Analysis script
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/bbqueStabilityAnalysisPlotter.sh.in"
	"${PROJECT_BINARY_DIR}/tools/bbqueStabilityAnalysisPlotter.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/bbqueStabilityAnalysisPlotter.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-stability-analysis)


#----- Deploy the Plugin development script
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/development/bbquePluginDev.sh.in"
	"${PROJECT_BINARY_DIR}/tools/development/bbquePluginDev.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/development/bbquePluginDev.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	RENAME bbque-dev-plugin
)

#----- Deploy the Code Restyling script
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/development/bbqueCodeStyle.sh.in"
	"${PROJECT_BINARY_DIR}/tools/development/bbqueCodeStyle.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/development/bbqueCodeStyle.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	RENAME bbque-dev-codestyler
)
install(FILES
	"${PROJECT_SOURCE_DIR}/tools/development/templates/plugin_templates.tar.bz2"
	"${PROJECT_SOURCE_DIR}/tools/development/templates/schedpol_templates.tar.bz2"
	"${PROJECT_SOURCE_DIR}/tools/development/templates/rloader_templates.tar.bz2"
	DESTINATION ${BBQUE_PATH_CMAKE}
)

#----- Deploy the python scheduler tester
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/pysched/modules/barbeque_utils.py.in"
	"${PROJECT_BINARY_DIR}/tools/pysched/modules/barbeque_utils.py"
	@ONLY
)
install(
	DIRECTORY ${PROJECT_SOURCE_DIR}/tools/pysched/
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}/pysched/
	USE_SOURCE_PERMISSIONS
	FILES_MATCHING
	PATTERN "*.py"
	PATTERN "*.conf"
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/pysched/modules/barbeque_utils.py"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}/pysched/modules/
)

# ------- Deploy the BarbequeRTRM power traces plotter
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/monitor/bbquePlotPowerTrace.py.in"
	"${PROJECT_BINARY_DIR}/tools/monitor/bbquePlotPowerTrace.py"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/monitor/bbquePlotPowerTrace.py"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	RENAME bbque-plotrace)
install(PROGRAMS
	"${PROJECT_SOURCE_DIR}/tools/monitor/bbqueCpuThermalPlotter.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	RENAME bbque-cpu-temp)


# ------- Deploy the BarbequeRTRM systemd configuration patch applier
configure_file (
	"${PROJECT_SOURCE_DIR}/tools/bbqueSystemdSetup.sh"
	"${PROJECT_BINARY_DIR}/tools/bbqueSystemdSetup.sh"
	@ONLY
)
install(PROGRAMS "${PROJECT_BINARY_DIR}/tools/bbqueSystemdSetup.sh"
	DESTINATION ${BBQUE_TOOLS_INSTALL_DIR}
	COMPONENT BarbequeUTILS
	RENAME bbque-systemd-setup)


