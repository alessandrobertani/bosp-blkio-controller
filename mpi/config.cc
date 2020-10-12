/*
 * Copyright (C) 2016  Politecnico di Milano
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
#include "config.h"

#include <exception>
#include <memory>
#include <iostream>
#include <fstream>

#include "version.h"
#include <bbque/utils/utility.h>

extern std::string recipe;

namespace po = boost::program_options;


namespace mpirun {


    Config::Config() {
        conf_file = BBQUE_PATH_PREFIX "/" BBQUE_PATH_CONF "/MpiRun.conf";
        opts_desc = std::unique_ptr<po::options_description>
                    (new po::options_description("bbque-mpirun Configuration Options"));

        this->desc_initialization();
        this->parse_config_file();
    }

    void Config::set_arguments(int argc, char *argv[]) {

        // First of all separates the command line parameters, from argv to the two already allocated vectors
        bool in_bbq_mode = false;

        for (int i=1; i<argc; i++) {
            if (::strcmp(argv[i],"--bbque-start") == 0) {
                in_bbq_mode = true;
                continue;
            }
            if (::strcmp(argv[i],"--bbque-end") == 0) {
                if (in_bbq_mode) {
                    in_bbq_mode = false;
                    continue;
                }
                else {
                    std::cout << "Unconsistent use of --bbque-start and --bbque-end" << std::endl;
                    ::exit(EXIT_FAILURE);
                }
            }

            if (in_bbq_mode) {
                barbeque_arguments.push_back(argv[i]);
            }
            else {
                mpirun_arguments.push_back(argv[i]);
            }
        }

        barbeque_arguments.insert(barbeque_arguments.begin(), argv[0]);
        mpirun_arguments.insert(mpirun_arguments.begin(), argv[0]);


        // Parse command line params
        try {
        po::store(po::parse_command_line(barbeque_arguments.size(), barbeque_arguments.data(), *opts_desc), opts_varmap);
        } catch(...) {
            std::cout << "Usage: bbque-mpirun [--bbque-start optionsBBQ --bbque-end] [optionsMPI] [program]\n";
            std::cout << *opts_desc << std::endl;
            ::exit(EXIT_FAILURE);
        }
        po::notify(opts_varmap);

        // Check for help request
        if (opts_varmap.count("help")) {
            std::cout << "Usage: bbque-mpirun [--bbque-start optionsBBQ --bbque-end] [optionsMPI] [program]\n";
            std::cout << *opts_desc << std::endl;
            ::exit(EXIT_SUCCESS);
        }

        // Check for version request
        if (opts_varmap.count("version")) {
            std::cout << "bbque-mpirun (ver. " << g_git_version << ")\n";
            std::cout << "Copyright (C) 2016 Politecnico di Milano\n";
            std::cout << "\n";
            std::cout << "Built on " <<
                __DATE__ << " " <<
                __TIME__ << "\n";
            std::cout << "\n";
            std::cout << "This is free software; see the source for "
                "copying conditions.  There is NO\n";
            std::cout << "warranty; not even for MERCHANTABILITY or "
                "FITNESS FOR A PARTICULAR PURPOSE.";
            std::cout << "\n" << std::endl;
            ::exit(EXIT_SUCCESS);
        }

    }

    void Config::desc_initialization() noexcept {

        opts_desc->add_options()
            ("help,h", "print this help message")
            ("version,v", "print program version")

            ("conf,c", po::value<std::string>(&conf_file)->
                default_value(conf_file),
                "bbque-mpirun configuration file")

            ("recipe,r", po::value<std::string>(&recipe)->
                default_value("MpiRun"),
                "recipe name (for all EXCs)")

            ("ompi.nodes.addrs", po::value<std::string>(&addresses)->
                    required(),
                    "OpenMPI addresses")
            ("ompi.nodes.slots", po::value<std::string>(&slots_per_addr)->
                    required(),
                    "OpenMPI slots per address")

            ("ompi.updatetime.resources", po::value<int>
                    (&updatetime_resources)->
                    default_value(100),
                    "OpenMPI time to wait to reply to ompi in milliseconds")

            ("ompi.test.mig_time", po::value<int>
                    (&mig_time)->
                    default_value(0),
                    "Test value for migration start.")

            ("ompi.test.mig_source_node", po::value<std::string>
                    (&mig_source)->
                    default_value(""),
                    "Test source node for migration.")

            ("ompi.test.mig_destination_node", po::value<std::string>
                    (&mig_destination)->
                    default_value(""),
                    "Test destination node for migration.")

        ;

    }

    void Config::parse_config_file() {
        std::ifstream in(BBQUE_CONF_FILE);

        // Parse configuration file (allowing for unregistered options)
        po::store(po::parse_config_file(in, *opts_desc, true), opts_varmap);
        po::notify(opts_varmap);


    }

    Config& Config::get() {
        static Config c;
        return c;
    }

}
