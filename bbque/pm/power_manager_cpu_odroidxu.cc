/*
 * Copyright (C) 2015  Politecnico di Milano
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

#include <cstring>

#include "bbque/pm/power_manager_cpu_odroidxu.h"
#include "bbque/platform_manager.h"
#include "bbque/platform_proxy.h"
#include "bbque/utils/iofs.h"

namespace bu = bbque::utils;

namespace bbque {


ODROID_XU_CPUPowerManager::ODROID_XU_CPUPowerManager() {
	// Enable sensors
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_A7"/enable",  1);
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_A15"/enable", 1);
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_MEM"/enable", 1);
}

ODROID_XU_CPUPowerManager::~ODROID_XU_CPUPowerManager() {
	// Disable sensors
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_A7"/enable",  0);
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_A15"/enable", 0);
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_MEM"/enable", 0);
}

std::string
ODROID_XU_CPUPowerManager::GetSensorsPrefixPath(
		br::ResourcePathPtr_t const & rp) {
	std::string filepath;

	if (rp->Type() == br::ResourceType::MEMORY)
		filepath = BBQUE_ODROID_SENSORS_DIR_MEM;
	else if (rp->Type() == br::ResourceType::PROC_ELEMENT) {
        PlatformManager & plm(PlatformManager::GetInstance());
		PlatformProxy const & local_pp(plm.GetLocalPlatformProxy());
        if (local_pp.IsHighPerformance(rp))
			filepath = BBQUE_ODROID_SENSORS_DIR_A15;
		else
			filepath = BBQUE_ODROID_SENSORS_DIR_A7;
	}
	else {
		logger->Error("ODROID-XU: Resource type '%s 'not available",
				rp->ToString().c_str());
		return std::string();
	}
	filepath += BBQUE_ODROID_SENSORS_ATT_W;
	return filepath;
}

PowerManager::PMResult
ODROID_XU_CPUPowerManager::GetPowerUsage(
		br::ResourcePathPtr_t const & rp,
		uint32_t & mwatt) {
	bu::IoFs::ExitCode_t result;
	float value;

	result = bu::IoFs::ReadFloatValueFrom(GetSensorsPrefixPath(rp), value, 1000);
	if (result != bu::IoFs::OK) {
		logger->Error("ODROID-XU: Power consumption not available for %s",
			rp->ToString().c_str());
		return PMResult::ERR_SENSORS_ERROR;
	}
	mwatt  = static_cast<uint32_t>(value);
	return PMResult::OK;
}

PowerManager::PMResult
ODROID_XU_CPUPowerManager::GetTemperature(
		br::ResourcePathPtr_t const & rp,
		uint32_t & celsius) {
	bu::IoFs::ExitCode_t result;
	std::string value;
	celsius = 0;

	// Sensors available only for A15 cores
	PlatformManager & plm(PlatformManager::GetInstance());
	PlatformProxy const & local_pp(plm.GetLocalPlatformProxy());
	if (!local_pp.IsHighPerformance(rp)) {
		logger->Warn("GetTemperature: <%s> is not a big core",
				rp->ToString().c_str());
		return PMResult::ERR_INFO_NOT_SUPPORTED;
	}

	// Get the offset in the TMU file
	int core_id = rp->GetID(br::ResourceType::PROC_ELEMENT);
	int offset  = BBQUE_ODROID_SENSORS_OFFSET_A15_0 +
			BBQUE_ODROID_SENSORS_OFFSET_SHIFT *
			(core_id - 4);

	// Get the temperature
	result = bu::IoFs::ReadValueFromWithOffset(
			BBQUE_ODROID_SENSORS_TEMP, value, 6, offset);
	if (result != bu::IoFs::OK) {
		logger->Error("GetTemperature: <%s> sensor read failed",
				rp->ToString().c_str());
		return PMResult::ERR_SENSORS_ERROR;
	}
	celsius = std::stoi(value) / 1000;

	return PMResult::OK;
}

} // namespace bbque
