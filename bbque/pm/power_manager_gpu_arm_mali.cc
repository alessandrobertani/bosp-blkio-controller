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

#include "bbque/pm/power_manager_gpu_arm_mali.h"
#include "bbque/utils/iofs.h"

namespace bu = bbque::utils;

namespace bbque {

ARM_Mali_GPUPowerManager::ARM_Mali_GPUPowerManager() {
	// Enable sensors
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_GPU"enable", 1);
	// Supported frequencies
	InitAvailableFrequencies();
}

ARM_Mali_GPUPowerManager::~ARM_Mali_GPUPowerManager() {
	freqs.clear();
	// Disable sensors
	bu::IoFs::WriteValueTo<int>(BBQUE_ODROID_SENSORS_DIR_GPU"enable", 0);
}

/* Load and temperature */

PowerManager::PMResult
ARM_Mali_GPUPowerManager::GetLoad(
		br::ResourcePathPtr_t const & rp,
		uint32_t &perc) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	result = bu::IoFs::ReadIntValueFrom<uint32_t>(BBQUE_ARM_MALI_SYS_LOAD, perc);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;
	return PMResult::OK;
}

PowerManager::PMResult
ARM_Mali_GPUPowerManager::GetTemperature(
		br::ResourcePathPtr_t const & rp,
		uint32_t &celsius) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	std::string value;

	result = bu::IoFs::ReadValueFromWithOffset(
			BBQUE_ODROID_SENSORS_TEMP,
			value, 6,
			BBQUE_ODROID_SENSORS_OFFSET_GPU);
	if (result == bu::IoFs::ERR_FILE_NOT_FOUND) {
		return PMResult::ERR_SENSORS_ERROR;
	}
	celsius = std::stoi(value) / 1000;

	return PMResult::OK;
}

/* Clock frequency */

PowerManager::PMResult
ARM_Mali_GPUPowerManager::GetClockFrequency(
		br::ResourcePathPtr_t const & rp,
		uint32_t &khz) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	result = bu::IoFs::ReadIntValueFrom<uint32_t>(
			BBQUE_ARM_MALI_SYS_FREQ, khz, 1000);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;
	return PMResult::OK;
}

void
ARM_Mali_GPUPowerManager::InitAvailableFrequencies() {
	bu::IoFs::ExitCode_t result;
	char buffer[100];
	result = bu::IoFs::ReadValueFrom(BBQUE_ARM_MALI_SYS_FREQS, buffer, 100);
	if (result != bu::IoFs::ExitCode_t::OK) {
		logger->Warn("ARM Mali GPU: Missing available frequencies table");
		return;
	}
	buffer[strlen(buffer)-2] = '\0';
	logger->Info("ARM Mali GPU: Frequency set = { %s }", buffer);

	char * f = strtok(buffer, " ");
	while (f) {
		freqs.push_back(std::stol(f));
		f = strtok(NULL, " ");
	}
}

PowerManager::PMResult
ARM_Mali_GPUPowerManager::GetAvailableFrequencies(
		br::ResourcePathPtr_t const & rp,
		std::vector<unsigned long> & gpu_freqs) {
	(void) rp;
	if (freqs.empty()) {
		logger->Warn("ARM Mali GPU: No frequencies table available");
		return PMResult::ERR_INFO_NOT_SUPPORTED;
	}
	gpu_freqs = freqs;
	return PMResult::OK;
}

PowerManager::PMResult
ARM_Mali_GPUPowerManager::SetClockFrequency(
		br::ResourcePathPtr_t const & rp,
		uint32_t khz) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	result = bu::IoFs::WriteValueTo<uint32_t>(BBQUE_ARM_MALI_SYS_FREQ, khz* 1000 );
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;
	return PMResult::OK;
}

/* Power consumption */

PowerManager::PMResult
ARM_Mali_GPUPowerManager::GetPowerUsage(
		br::ResourcePathPtr_t const & rp, uint32_t & mwatt) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	float value;
	result = bu::IoFs::ReadFloatValueFrom(
			BBQUE_ARM_MALI_SYS_POWER, value, 1000);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;
	mwatt = static_cast<uint32_t>(value);
	return PMResult::OK;
}

PowerManager::PMResult
ARM_Mali_GPUPowerManager::GetVoltage(
		br::ResourcePathPtr_t const & rp, uint32_t & mvolt) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	result = bu::IoFs::ReadIntValueFrom<uint32_t>(
			BBQUE_ARM_MALI_SYS_VOLTAGE, mvolt);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;
	return PMResult::OK;
}


/* Power states */

PowerManager::PMResult
ARM_Mali_GPUPowerManager::GetPowerState(
		br::ResourcePathPtr_t const & rp,
		uint32_t & state) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	result = bu::IoFs::ReadIntValueFrom<uint32_t>(
			BBQUE_ARM_MALI_SYS_WSTATE, state);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;
	return PMResult::OK;
}

PowerManager::PMResult
ARM_Mali_GPUPowerManager::SetPowerState(
		br::ResourcePathPtr_t const & rp,
		uint32_t state) {
	(void) rp;
	bu::IoFs::ExitCode_t result;
	result = bu::IoFs::WriteValueTo<uint32_t>(BBQUE_ARM_MALI_SYS_WSTATE, state);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;
	return PMResult::OK;
}

} // namespace bbque

