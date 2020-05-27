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

#ifndef BBQUE_LINUX_RECIPE_OCLPROXY_H_
#define BBQUE_LINUX_RECIPE_OCLPROXY_H_

#include <fstream>
#include <list>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bbque/config.h"

#include "bbque/pp/opencl_platform_proxy.h"

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque {
namespace pp {

class RecipePlatformProxy : public OpenCLPlatformProxy
{
public:

	static RecipePlatformProxy * GetInstance();

	/**
	 * @brief Destructor
	 */
	virtual ~RecipePlatformProxy();

	ExitCode_t MapResources(SchedPtr_t psched,
				ResourceAssignmentMapPtr_t pres,
				bool excl) noexcept;

#ifdef CONFIG_BBQUE_CR_FPGA

	ReliabilityActionsIF::ExitCode_t Dump(app::SchedPtr_t psched) override;

	ReliabilityActionsIF::ExitCode_t Restore(uint32_t pid, std::string exe_name) override;

	ReliabilityActionsIF::ExitCode_t Freeze(app::SchedPtr_t psched) override;

	ReliabilityActionsIF::ExitCode_t Thaw(app::SchedPtr_t papp) override;

#endif

private:

	RecipePlatformProxy();

	/**
	 * @brief The logger
	 */
	std::unique_ptr<bu::Logger> logger;


#ifdef CONFIG_BBQUE_CR_FPGA

	/**
	 * @brief Initialize the support for freezing, checkpoint, restore actions
	 */
	void InitReliabilitySupport();

	bool HasAssignedResources(app::SchedPtr_t psched);
#endif


};

} // namespace pp
} // namespace bbque

#endif // BBQUE_LINUX_RECIPE_OCLPROXY_H_

