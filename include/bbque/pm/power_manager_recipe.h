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
#ifndef BBQUE_POWER_MANAGER_RECIPE_H_
#define BBQUE_POWER_MANAGER_RECIPE_H_

#include "bbque/pm/power_manager.h"
#include <CL/cl.h>

namespace bu = bbque::utils;

namespace bbque {

/**
 * @class RecipePowerManager
 *
 * @brief This class provides the abstraction towards the management of
 * the runtime information from hardware included in the H2020-RECIPE
 * project prototype.
 */
class RecipePowerManager: public PowerManager {

public:

	RecipePowerManager();

	virtual ~RecipePowerManager();


	/** Temperature */

	PMResult GetTemperature(
		br::ResourcePathPtr_t const & rp, uint32_t &celsius);

protected:

	cl_platform_id recipe_ocl_platform;

	cl_platform_id * ocl_platforms = nullptr;

	cl_device_id * ocl_devices = nullptr;

};

}

#endif // BBQUE_POWER_MANAGER_RECIPE_H_

