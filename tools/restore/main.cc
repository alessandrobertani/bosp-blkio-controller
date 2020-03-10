/*
 * Copyright (C) 2020  Politecnico di Milano
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

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <sys/types.h>
#include <sys/wait.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "config.h"
#include "restore_exc.h"
#include <bbque/utils/utility.h>
#include <bbque/utils/logging/logger.h>

// Setup logging
#undef  BBQUE_LOG_MODULE
#define BBQUE_LOG_MODULE "restore"

namespace bt = bbque::tools;
namespace bu = bbque::utils;
namespace po = boost::program_options;

/**
 * @brief A pointer to an EXC
 */
std::unique_ptr<bu::Logger> logger;


int main(int argc, char *argv[])
{
	const char * exe_name = basename(argv[0]);
	if (argc < 3) {
		std::cout << "ERROR: ./%s <name> <pid> <checkpoint_dir>"
	          << exe_name << std::endl;
		exit(EXIT_FAILURE);
	}

	std::string recipe("dummy");
	const char * name = argv[1];
	uint32_t pid = atoi(argv[2]);
	std::string chkp_dir(argv[3]);

	logger = bu::Logger::GetLogger(BBQUE_LOG_MODULE);
	logger->Debug("RTLib initialization...");
	RTLIB_Services_t * rtlib;
	RTLIB_Init(name, &rtlib, pid);
	if (!rtlib) {
		logger->Error("BarbequeRTRM not reachable");
		exit(EXIT_FAILURE);
	}
	assert(rtlib);

	logger->Debug("Registering EXC (recipe=%s)...", recipe.c_str());
	auto pexc = std::make_shared<bt::BbqueRestoreEXC>(
			exe_name, recipe, rtlib, chkp_dir, pid);
	if (!pexc->isRegistered()) {
		logger->Error("Registration failed: check the recipe file");
		exit(EXIT_FAILURE);
	}

	logger->Info("Launching the restore of [name=%s pid=%d]...", name, pid);
	logger->Info("Checkpoint image directory: %s ", chkp_dir.c_str());
	pexc->Start();

	logger->Info("Waiting for [name=%s pid=%d] to terminate...", name, pid);
	pexc->WaitCompletion();
	logger->Info("Application [name=%s pid=%d] terminated", name, pid);

	return EXIT_SUCCESS;
}
