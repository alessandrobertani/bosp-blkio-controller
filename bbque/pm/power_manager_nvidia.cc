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

#include <dlfcn.h>

#include "bbque/pm/power_manager_nvidia.h"

#define numFreq 1000

#define  GET_DEVICE_ID(rp, device) \
	 nvmlDevice_t device; \
	 unsigned int id_num; \
	id_num = GetDeviceId(rp, device); \
	if (device == NULL) { \
		logger->Error("GetDeviceId: The path does not resolve a resource"); \
		return PMResult::ERR_RSRC_INVALID_PATH; \
	}


namespace br = bbque::res;


namespace bbque {

const char * convertToComputeModeString(nvmlComputeMode_t mode)
{
	switch (mode) {
	case NVML_COMPUTEMODE_DEFAULT:
		return "Default";
	case NVML_COMPUTEMODE_EXCLUSIVE_THREAD:
		return "Exclusive_Thread";
	case NVML_COMPUTEMODE_PROHIBITED:
		return "Prohibited";
	case NVML_COMPUTEMODE_EXCLUSIVE_PROCESS:
		return "Exclusive Process";
	default:
		return "Unknown";
	}
}

NVIDIAPowerManager & NVIDIAPowerManager::GetInstance()
{
	static NVIDIAPowerManager instance;
	return instance;
}

NVIDIAPowerManager::NVIDIAPowerManager()
{
	// Retrieve information about the GPU(s) of the system
	LoadDevicesInfo();
}

void NVIDIAPowerManager::LoadDevicesInfo()
{
	nvmlReturn_t result;
	result = nvmlInit();

	if (NVML_SUCCESS != result) {
		logger->Warn("LoadDevicesInfo: Control initialization failed [Err:%s]",
			nvmlErrorString(result) );
		return;
	}
	logger->Info("LoadDevicesInfo: Initialized correctly performed");

	// Devices enumeration
	result = nvmlDeviceGetCount(&device_count);
	if (NVML_SUCCESS != result) {
		logger->Warn("LoadDevicesInfo: No device(s) available on the system [Err:%s]",
			nvmlErrorString(result));
		return;
	}
	logger->Info("LoadDevicesInfo: Number of device(s) count = %d", device_count);

	for (uint i = 0; i < device_count; ++i) {
		DeviceInfo device_info;
		nvmlDevice_t device;

		device_info.id_num = i;
		// Query for device handle to perform operations on a device
		result = nvmlDeviceGetHandleByIndex(i, &device);
		if (NVML_SUCCESS != result) {
			logger->Debug("LoadDevicesInfo: skipping '%d' [Err:%d] ", i, nvmlErrorString(result));
			continue;
		}

		// Devices ID mapping and resource path
		devices_map.emplace(devices_map.size(), device);

		result = nvmlDeviceGetName(device, device_info.name,
					NVML_DEVICE_NAME_BUFFER_SIZE);
		if (NVML_SUCCESS != result) {
			logger->Warn("LoadDevicesInfo: failed to get name of device %i: %s", i, nvmlErrorString(result));
		}

		// pci.busId is very useful to know which device physically you're talking to
		// Using PCI identifier you can also match nvmlDevice handle to CUDA device.
		result = nvmlDeviceGetPciInfo(device, &device_info.pci);
		if (NVML_SUCCESS != result) {
			logger->Warn("LoadDevicesInfo: failed to get PCI info for device %i: %s", i,
				nvmlErrorString(result));
		}

		logger->Debug("%d. %s [%s] %d", i, device_info.name, device_info.pci.busId,
			device);

		// Power control capabilities
		result = nvmlDeviceGetComputeMode(device, &device_info.compute_mode);
		if (NVML_ERROR_NOT_SUPPORTED == result)
			logger->Warn("LoadDevicesInfo: this is not CUDA capable device");
		else if (NVML_SUCCESS != result) {
			logger->Warn("LoadDevicesInfo: failed to get compute mode for device %i: %s", i,
				nvmlErrorString(result));
			continue;
		}
		else {
			// try to change compute mode
			logger->Debug("LoadDevicesInfo: changing device's compute mode from '%s' to '%s'",
				convertToComputeModeString(device_info.compute_mode),
				convertToComputeModeString(NVML_COMPUTEMODE_PROHIBITED));

			result = nvmlDeviceSetComputeMode(device, NVML_COMPUTEMODE_PROHIBITED);
			if (NVML_ERROR_NO_PERMISSION == result)
				logger->Warn("LoadDevicesInfo: need root privileges to do that: %s", nvmlErrorString(result));
			else if (NVML_ERROR_NOT_SUPPORTED == result)
				logger->Warn("LoadDevicesInfo: compute mode prohibited not supported. You might be running on"
					"windows in WDDM driver model or on non-CUDA capable GPU.");

			else if (NVML_SUCCESS != result) {
				logger->Warn("LoadDevicesInfo: failed to set compute mode for device %i: %s", i,
					nvmlErrorString(result));
			}
			else {
				//All is gone correctly
				logger->Debug("LoadDevicesInfo: device initialized");
				// Mapping information Devices per devices
				info_map.emplace(device, device_info);
			}

		}

		// Check power reading availability
		unsigned int power;
		result = nvmlDeviceGetPowerUsage(device, &power);
		if (result == NVML_ERROR_NOT_SUPPORTED)
			power_read_supported = false;
		else
			power_read_supported = true;

		logger->Info("LoadDevicesInfo: device=%d power read supported: %s", i,
			energy_read_supported ? "YES" : "NO");


		// Initialize energy consumption monitoring
		unsigned long long curr_energy;
		result = nvmlDeviceGetTotalEnergyConsumption(device, &curr_energy);
		if (result == NVML_ERROR_NOT_SUPPORTED)
			energy_read_supported = false;
		else
			energy_read_supported = true;

		logger->Info("LoadDevicesInfo: device=%d energy read supported: %s", i,
			energy_read_supported ? "YES" : "NO");

		this->energy_values[device] = 0;
		this->is_sampling[device] = false;
	}

	initialized = true;
	logger->Notice("LoadDevicesInfo: Devices [#=%d] information initialized",
		devices_map.size());

	std::map<BBQUE_RID_TYPE, nvmlDevice_t>::iterator it = devices_map.begin();
	for (; it != devices_map.end(); ++it) {
		auto it2 = info_map.find(it->second);
		if (it2 != info_map.end()) {
			logger->Debug("LoadDevicesInfo: restoring device's compute mode back to '%s'",
				convertToComputeModeString(it2->second.compute_mode));
			result = nvmlDeviceSetComputeMode(it->second, it2->second.compute_mode);
			if (NVML_SUCCESS != result) {
				logger->Warn("LoadDevicesInfo: failed to restore compute mode for device %s: %s",
					it2->second.name, nvmlErrorString(result));
			}
			logger->Debug("LoadDevicesInfo: Restored correctly");
		}
		else
			logger->Warn("LoadDevicesInfo: Error inside the matching between device_map and info_map");

	}
}

NVIDIAPowerManager::~NVIDIAPowerManager()
{
	nvmlReturn_t result;

	std::map<BBQUE_RID_TYPE, nvmlDevice_t>::iterator it = devices_map.begin();
	for (; it != devices_map.end(); ++it) {
		auto it2 = info_map.find(it->second);
		if (it2 != info_map.end()) {
			logger->Debug("NVIDIAPowerManager: restoring device's compute mode back to '%s'",
				convertToComputeModeString(it2->second.compute_mode));
			result = nvmlDeviceSetComputeMode(it->second, it2->second.compute_mode);
			if (NVML_SUCCESS != result) {
				logger->Warn("NVIDIAPowerManager: failed to restore compute mode for device %s: %s",
					it2->second.name, nvmlErrorString(result));
			}
			logger->Info("NVIDIAPowerManager: correctly restored");
		}
		else
			logger->Warn("NVIDIAPowerManager: error inside the matching between device_map and info_map");

		auto & device(it->second);
		this->is_sampling[device] = false;
		this->energy_threads[device].join();
	}

	result = nvmlShutdown();
	if (NVML_SUCCESS != result) {
		logger->Warn("NVIDIAPowerManager: failed to shutdown NVML: [Err:%s]",
			nvmlErrorString(result));
	}
	logger->Notice("NVIDIAPowerManager: NVML shutdown done");

	devices_map.clear();
	info_map.clear();
}

int NVIDIAPowerManager::GetDeviceId(br::ResourcePathPtr_t const & rp,
				    nvmlDevice_t & device) const
{
	std::map<BBQUE_RID_TYPE, nvmlDevice_t>::const_iterator it;
	std::map<nvmlDevice_t, DeviceInfo>::const_iterator it2;
	if (rp == nullptr) {
		logger->Debug("GetDeviceId: null resource path");
		return -1;
	}

	it = devices_map.find(rp->GetID(br::ResourceType::GPU));
	it2 = info_map.find(it->second);
	if (it == devices_map.end()) {
		logger->Warn("GetDeviceId: missing GPU id=%d", rp->GetID(br::ResourceType::GPU));
		return -2;
	}
	if (it2 == info_map.end()) {
		logger->Warn("GetDeviceId: missing GPU id=%d information",
			rp->GetID(br::ResourceType::GPU));
		return -2;
	}

	device = it->second;
	return it2->second.id_num;
}

PowerManager::PMResult
NVIDIAPowerManager::GetLoad(br::ResourcePathPtr_t const & rp, uint32_t & perc)
{
	nvmlReturn_t result;
	nvmlUtilization_t utilization;

	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetUtilizationRates(device, &utilization);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetLoad: cannot get GPU (Device %d) load", device);
		logger->Warn("GetLoad: failed to to query the utilization rate for device %i: %s",
			device, nvmlErrorString(result));
	}

	logger->Debug("GetLoad: [GPU-%d] utilization rate %u ", id_num, utilization.gpu);
	logger->Debug("GetLoad: [GPU-%d] memory utilization rate %u", id_num,
		utilization.memory);
	perc = utilization.gpu;
	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::GetTemperature(br::ResourcePathPtr_t const & rp,
				   uint32_t & celsius)
{
	nvmlReturn_t result;
	celsius = 0;
	unsigned int temp;

	if (!initialized) {
		logger->Warn("GetTemperature: Cannot get GPU(s) temperature");
		return PMResult::ERR_API_NOT_SUPPORTED;
	}

	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetTemperature: failed to to query the temperature: %s",
			nvmlErrorString(result));
		logger->Warn("GetTemperature: [GPU-%d] temperature not available [%d]", id_num,
			nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}
	celsius = temp;
	logger->Debug("GetTemperature: [GPU-%d] temperature=%d C", id_num, temp);

	return PMResult::OK;
}

/* Clock frequency */

PowerManager::PMResult
NVIDIAPowerManager::GetAvailableFrequencies(br::ResourcePathPtr_t const & rp,
					    std::vector<uint32_t> & freqs)
{
	nvmlReturn_t result;
	unsigned int memoryClockMHz;
	unsigned int clockMhz[numFreq], count = numFreq;
	std::vector<uint32_t>::iterator it;

	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetClockInfo (device, NVML_CLOCK_MEM , &memoryClockMHz);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetAvailableFrequencies: "
			"[GPU-%d] Failed to query the graphic clock inside GetAvailableFrequencies: %s",
			id_num, nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}

	result = nvmlDeviceGetSupportedGraphicsClocks (device, memoryClockMHz , &count,
						clockMhz);
	//result = nvmlDeviceGetSupportedGraphicsClocks(device, NVML_CLOCK_GRAPHICS, &count, clockMhz);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetAvailableFrequencies: "
			"[GPU-%d] Failed to query the supported graphic clock: %s",
			id_num, nvmlErrorString(result));
		//freqs[0] = -1;
		return PMResult::ERR_API_INVALID_VALUE;
	}

	it = freqs.begin();
	for (int i = 0; it != freqs.end(); ++i, it++) {
		freqs.insert(it, (uint32_t)clockMhz[i]);
		logger->Debug("GetAvailableFrequencies: "
			"[GPU-%d] possible clock frequency: %d Mhz", id_num,
			clockMhz[i]);

	}

	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::GetClockFrequency(br::ResourcePathPtr_t const & rp,
				      uint32_t & khz)
{
	nvmlReturn_t result;
	unsigned int var;
	khz = 0;

	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetClockInfo (device, NVML_CLOCK_GRAPHICS , &var);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetClockFrequency: failed to to query the graphic clock: %s",
			nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}

	khz = var;
	logger->Debug("GetClockFrequency: [GPU-%d] clock frequency: %d Mhz", id_num, var);

	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::SetClockFrequency(br::ResourcePathPtr_t const & rp,
				      uint32_t khz)
{
	nvmlReturn_t result;
	unsigned int memClockMHz;
	khz = khz * 1000;

	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetClockInfo (device, NVML_CLOCK_MEM , &memClockMHz);
	if (NVML_SUCCESS != result) {
		logger->Warn("SetClockFrequency: failed to check the memory graphic clock: %s",
			nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}

	result = nvmlDeviceSetApplicationsClocks(device, memClockMHz, khz);
	if (NVML_SUCCESS != result) {
		logger->Warn("SetClockFrequency: failed to set the graphic clock: %s",
			nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}

	logger->Debug("SetClockFrequency: [GPU-%d] clock set at frequency=%d MHz", id_num, khz);

	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::GetClockFrequencyInfo(br::ResourcePathPtr_t const & rp,
					  uint32_t & khz_min,
					  uint32_t & khz_max,
					  uint32_t & khz_step)
{
	nvmlReturn_t result;
	unsigned int var;
	khz_min = khz_max = khz_step = 0;
	br::ResourceType r_type = rp->Type();

	GET_DEVICE_ID(rp, device);

	if (r_type == br::ResourceType::PROC_ELEMENT) {
		result = nvmlDeviceGetDefaultApplicationsClock(device, NVML_CLOCK_GRAPHICS,
							&var);
		if (NVML_SUCCESS != result) {
			logger->Warn("GetClockFrequencyInfo: [GPU-%d] PROC_ELEMENT Failed to to query the default graphic clock: %s",
				id_num, nvmlErrorString(result));
			return PMResult::ERR_API_INVALID_VALUE;
		}
		khz_min  = var;
		logger->Debug("GetClockFrequencyInfo: [GPU-%d] minimum GPU frequency=%d MHz", id_num, khz_min);
		result = nvmlDeviceGetMaxClockInfo(device, NVML_CLOCK_GRAPHICS, &var);
		if (NVML_SUCCESS != result) {
			logger->Warn("GetClockFrequencyInfo: [GPU-%d] failed to get the maximum frequency: %s",
				id_num, nvmlErrorString(result));
			return PMResult::ERR_API_INVALID_VALUE;
		}
		khz_max  = var;
		khz_step = 1;
		logger->Debug("GetClockFrequencyInfo: [GPU-%d] maximum GPU frequency=%d MHz", id_num, khz_max);
		logger->Debug("GetClockFrequencyInfo: [GPU-%d] GPU frequency step=%d MHz", id_num, khz_step);
	}
	else if (r_type == br::ResourceType::MEMORY) {
		result = nvmlDeviceGetDefaultApplicationsClock(device, NVML_CLOCK_MEM, &var);
		if (NVML_SUCCESS != result) {
			logger->Warn("GetClockFrequencyInfo: "
				"[GPU-%d] MEMORY failed to get the default memory frequency: %s",
				id_num, nvmlErrorString(result));
			return PMResult::ERR_API_INVALID_VALUE;
		}
		khz_min  = var;
		logger->Debug("GetClockFrequencyInfo: [GPU-%d] minimum MEMORY frequency=%d MHz", id_num, khz_min);
		result = nvmlDeviceGetMaxClockInfo (device, NVML_CLOCK_MEM , &var);
		if (NVML_SUCCESS != result) {
			logger->Warn("GetClockFrequencyInfo: "
				"[GPU-%d] MEMORY failed to check the maximum memory frequency: %s",
				id_num,
				nvmlErrorString(result));
			return PMResult::ERR_API_INVALID_VALUE;
		}
		khz_max  = var;
		khz_step = 1;
		logger->Debug("GetClockFrequencyInfo: [GPU-%d] maximum MEMORY frequency=%d MHz", id_num, khz_max);
		logger->Debug("GetClockFrequencyInfo: [GPU-%d] MEMORY frequency step=%d MHz", id_num, khz_step);
	}

	return PMResult::OK;
}

/* Fan */

PowerManager::PMResult
NVIDIAPowerManager::GetFanSpeed(br::ResourcePathPtr_t const & rp,
				FanSpeedType fs_type,	uint32_t & value)
{
	nvmlReturn_t result;
	unsigned int var;
	value = 0;

	GET_DEVICE_ID(rp, device);

	// Fan speed type
	if (fs_type == FanSpeedType::PERCENT) {
		result = nvmlDeviceGetFanSpeed (device, &var);
		if (NVML_SUCCESS != result) {
			logger->Warn("GetFanSpeed: [GPU-%d] failed to get the fan speed: %s", id_num,
				nvmlErrorString(result));
			return PMResult::ERR_API_INVALID_VALUE;
		}
		logger->Debug("GetFanSpeed: [GPU-%d] Fan speed=%d%c ", id_num, var, '%');
		value = (uint32_t) var;
	}
	else if (fs_type == FanSpeedType::RPM) {
		logger->Warn("GetFanSpeed: RPM fan speed is not supported for NVIDIA GPUs");
		value = 0;
	}

	return PMResult::OK;
}

/* Power */

PowerManager::PMResult
NVIDIAPowerManager::GetPowerUsage(br::ResourcePathPtr_t const & rp,
				  uint32_t & mwatt)
{
	nvmlReturn_t result;
	unsigned int var;

	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetPowerUsage (device, &var);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetPowerUsage: [GPU-%d] failed to get the power usage: %s", id_num,
			nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}
	logger->Debug("GetPowerUsage: [GPU-%d] power usage value=%d mW [+/-5%c]", id_num,
		var, '%');
	mwatt = var;

	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::GetPowerInfo(br::ResourcePathPtr_t const & rp,
				 uint32_t & mwatt_min, uint32_t & mwatt_max)
{
	nvmlReturn_t result;
	unsigned int min, max;

	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetPowerManagementLimitConstraints(device, &min, &max);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetPowerInfo: [GPU-%d]  failed to get the power information: %s",
			id_num, nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}

	mwatt_min = min;
	mwatt_max = max;

	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::GetPowerState(br::ResourcePathPtr_t const & rp,
				  uint32_t & state)
{
	nvmlPstates_t pState;
	nvmlReturn_t result;
	state = 0;
	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetPerformanceState(device, &pState);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetPowerState: [GPU-%d] failed to get the power state: %s", id_num,
			nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}

	state = (uint32_t) pState;

	return PMResult::OK;
}

/* States */


#define NVIDIA_GPU_PSTATE_MAX    0
#define NVIDIA_GPU_PSTATE_MIN   15

PowerManager::PMResult
NVIDIAPowerManager::GetPowerStatesInfo(br::ResourcePathPtr_t const & rp,
				       uint32_t & min, uint32_t & max, int & step)
{
	(void) rp;
	min  = NVIDIA_GPU_PSTATE_MIN;
	max  = NVIDIA_GPU_PSTATE_MAX;
	step = 1;

	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::GetPerformanceState(br::ResourcePathPtr_t const & rp,
					uint32_t & state)
{
	nvmlPstates_t pState;
	nvmlReturn_t result;
	state = 0;
	GET_DEVICE_ID(rp, device);

	result = nvmlDeviceGetPerformanceState(device, &pState);
	if (NVML_SUCCESS != result) {
		logger->Warn("GetPerformanceState: [GPU-%d] failed to get the performance state: %s",
			id_num, nvmlErrorString(result));
		return PMResult::ERR_API_INVALID_VALUE;
	}

	logger->Debug("GetPerformanceState: valid interval [%d-%d]+{32}:",
		NVIDIA_GPU_PSTATE_MAX, NVIDIA_GPU_PSTATE_MIN);
	logger->Debug("GetPerformanceState:\t *) %d for Maximum Performance", NVIDIA_GPU_PSTATE_MAX);
	logger->Debug("GetPerformanceState:\t *) %d for Minimum Performance", NVIDIA_GPU_PSTATE_MIN);
	logger->Debug("GetPerformanceState:\t *) 32 Unknown performance state");
	logger->Debug("GetPerformanceState:\t [GPU-%d] PerformanceState: %u ", id_num, pState);
	state = (uint32_t) pState;

	return PMResult::OK;
}

PowerManager::PMResult
NVIDIAPowerManager::GetPerformanceStatesCount(br::ResourcePathPtr_t const & rp,
					      uint32_t & count)
{
	(void) rp;

	count = NVIDIA_GPU_PSTATE_MIN - NVIDIA_GPU_PSTATE_MAX;

	return PMResult::OK;
}

int64_t NVIDIAPowerManager::StartEnergyMonitor(br::ResourcePathPtr_t const & rp)
{
	if (!this->power_read_supported) {
		logger->Error("StartEnergyMonitor: power reading not supported");
		return -1;
	}

	nvmlDevice_t device;
	unsigned int id_num = GetDeviceId(rp, device);
	if (device == NULL) {
		logger->Error("StartEnergyMonitor: invalid path <%s>", rp->ToString().c_str());
		return -1;
	}

	if (this->is_sampling[device]) {
		logger->Warn("StartEnergyMonitor: device id=%d already started", id_num);
		return -2;
	}
	this->is_sampling[device] = true;

	if (this->energy_read_supported) {
		unsigned long long curr_energy;
		nvmlReturn_t result = nvmlDeviceGetTotalEnergyConsumption(device, &curr_energy);
		if (NVML_SUCCESS != result) {
			logger->Warn("StartEnergyMonitor: [GPU-%d] failed to start energy sampling: %s",
				id_num, nvmlErrorString(result));
			return -3;
		}
		this->energy_values[device] = curr_energy;
		logger->Debug("StartEnergyMonitor: [GPU-%d] start energy value=%llu",
			id_num, curr_energy);
	}
	else {
		energy_threads.emplace(device, std::thread(
						&NVIDIAPowerManager::ProfileEnergyConsumption,
						this,
						device));
	}

	return 0;
}

uint64_t NVIDIAPowerManager::StopEnergyMonitor(br::ResourcePathPtr_t const & rp)
{
	nvmlDevice_t device;
	unsigned int id_num = GetDeviceId(rp, device);
	if (device == NULL) {
		logger->Error("StopEnergyMonitor: invalid path <%s>", rp->ToString().c_str());
		return 0;
	}

	if (!this->is_sampling[device]) {
		logger->Warn("StopEnergyMonitor: [GPU-%d] energy sampling not started",
			id_num);
		return 0;
	}
	this->is_sampling[device] = false;

	uint64_t energy_cons;
	if (this->energy_read_supported) {
		unsigned long long curr_energy;
		nvmlReturn_t result = nvmlDeviceGetTotalEnergyConsumption(device, &curr_energy);
		if (NVML_SUCCESS != result) {
			logger->Warn("StopEnergyMonitor: [GPU-%d] failed to start energy sampling: %s",
				id_num, nvmlErrorString(result));
			return 0;
		}
		logger->Debug("StopEnergyMonitor: [GPU-%d] stop energy value=%llu [mJ]",
			id_num, curr_energy);

		uint64_t energy_cons = curr_energy - this->energy_values[device];
		energy_cons *= 1e3; // mJ -> uJ
	}
	else {
		logger->Debug("StopEnergyMonitor: <%s> waiting for the profiler termination...",
			id_num);
		this->energy_threads[device].join();
		this->energy_threads.erase(device);
		energy_cons = this->energy_values[device];
	}

	// Reset energy value reading for the next sampling
	this->energy_values[device] = 0;
	logger->Info("StopEnergyMonitor: [GPU-%d] consumption=%llu [uJ]",
		id_num, energy_cons);

	return energy_cons;
}

void NVIDIAPowerManager::ProfileEnergyConsumption(nvmlDevice_t device)
{
	logger->Debug("ProfileEnergyConsumption: started for device %p: ", device);

	unsigned int power1, power2;
	while (this->is_sampling[device]) {

		logger->Debug("ProfileEnergyConsumption: sampling for device %p: ", device);

		nvmlReturn_t result = nvmlDeviceGetPowerUsage(device, &power1);
		if (NVML_SUCCESS != result) {
			logger->Error("ProfileEnergyConsumption: error in power reading #1: %s",
				nvmlErrorString(result));
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(BBQUE_NVIDIA_T_MS));

		result = nvmlDeviceGetPowerUsage(device, &power2);
		if (NVML_SUCCESS != result) {
			logger->Error("ProfileEnergyConsumption: error in power reading #2: %s",
				nvmlErrorString(result));
			power2 = power1;
		}
		logger->Debug("ProfileEnergyConsumption: p1=%lu p2=%lu [mW]", power1, power2);

		// The energy additional contribution is given by the area of the trapezium
		this->energy_values[device] += ((power1 + power2) * BBQUE_NVIDIA_T_MS) / 2;
	}

	logger->Debug("ProfileEnergyConsumption: terminated for device %p: ", device);
}

} // namespace bbque

