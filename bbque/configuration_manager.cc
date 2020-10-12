/*
 * Copyright (C) 2012  Politecnico di Milano
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bbque/configuration_manager.h"

#include <cstdlib>
#include <iostream>
#include <fstream>

#include "bbque/barbeque.h"

namespace po = boost::program_options;

namespace bbque {

ConfigurationManager::ConfigurationManager() :
	core_opts_desc("Generic Options"),
	all_opts_desc(""),
#ifdef BBQUE_DEBUG
	//dbg_opts_desc("Debugging Options"),
#endif
	cmd_opts_desc("") {

	// BBQ core options (exposed to command line)
	core_opts_desc.add_options()
		("help,h", "print this help message")
		("daemon,d", "run as daemon in background")
		("config,c", po::value<std::string>(&conf_file_path)->
			default_value(BBQUE_CONF_FILE),
			"configuration file path")
		("bbque.plugins,p", po::value<std::string>(&plugins_dir)->
			default_value(BBQUE_PATH_PREFIX "/" BBQUE_PATH_PLUGINS),
			"plugins folder")
		("bbque.test,t", "Run TESTs plugins")
		("version,v", "print program version")
		;

	// All options (not all exposed to command line)
	all_opts_desc.add(core_opts_desc);
	all_opts_desc.add_options()
		("bbque.daemon_name", po::value<std::string>(&daemon_name)->
			default_value(BBQUE_DAEMON_NAME),
			"the BBQ daemon process name")
		("bbque.uid", po::value<std::string>(&daemon_uid)->
			default_value(BBQUE_DAEMON_UID),
			"user ID to run the daemon under")
		("bbque.lockfile", po::value<std::string>(&daemon_lockfile)->
			default_value(BBQUE_PATH_PREFIX "/" BBQUE_DAEMON_LOCKFILE),
			"daemon lock-file")
		("bbque.pidfile", po::value<std::string>(&daemon_pidfile)->
			default_value(BBQUE_PATH_PREFIX "/" BBQUE_DAEMON_PIDFILE),
			"group ID to run the daemon under")
		("bbque.rundir", po::value<std::string>(&daemon_rundir)->
			default_value(BBQUE_PATH_PREFIX "/" BBQUE_DAEMON_RUNDIR),
			"daemon run directory")
		;

	// Options exposed to command line
	cmd_opts_desc.add(core_opts_desc);

#ifdef BBQUE_DEBUG
	//dbg_opts_desc.add_options()
	//	("debug.test_time", po::value<uint16_t>(&test_run)->
	//		default_value(5),
	//		"how long [s] to run")
	//	;
	//all_opts_desc.add(dbg_opts_desc);
	//cmd_opts_desc.add(dbg_opts_desc);
#endif

}

ConfigurationManager::~ConfigurationManager() {

}

ConfigurationManager & ConfigurationManager::GetInstance() {
	static ConfigurationManager instance;
	return instance;
}

void ConfigurationManager::ParseCommandLine(int argc, char *argv[]) {

	// Parse command line params
	try {
	po::store(po::parse_command_line(argc, argv, cmd_opts_desc), opts_vm);
	} catch(...) {
		std::cout << "Usage: " << argv[0] << " [options]\n";
		std::cout << cmd_opts_desc << std::endl;
		::exit(EXIT_FAILURE);
	}
	po::notify(opts_vm);

	// Check for help request
	if (opts_vm.count("help")) {
		std::cout << "Usage: " << argv[0] << " [options]\n";
		std::cout << cmd_opts_desc << std::endl;
		::exit(EXIT_SUCCESS);
	}

	// Check for version request
	if (opts_vm.count("version")) {
		std::cout << "Barbeque RTRM (ver. " << g_git_version << ")\n";
		std::cout << "Copyright (C) 2011 Politecnico di Milano\n";
		std::cout << "\n";
		std::cout << "Built on " << __DATE__ << " " << __TIME__ << "\n";
		std::cout << "flavor: " BBQUE_BUILD_FLAVOR << "\n";
		std::cout << "\n";
		std::cout << "This is free software; see the source for copying conditions.  There is NO\n";
		std::cout << "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.";
		std::cout << "\n" << std::endl;
		::exit(EXIT_SUCCESS);
	}

	ParseConfigurationFile(all_opts_desc, opts_vm);

}

void ConfigurationManager::ParseConfigurationFile(
		po::options_description const & opts_desc,
		po::variables_map & opts) {
	std::ifstream in(conf_file_path);

	// Parse configuration file (allowing for unregistered options)
	po::store(po::parse_config_file(in, opts_desc, true), opts);
	po::notify(opts);

}

} // namespace bbque

