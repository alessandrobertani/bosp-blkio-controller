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

#include <string>

#include "bbque/pp/nvidia_platform_proxy.h"
#include "bbque/platform_manager.h"
#include "bbque/power_monitor.h"
#include "bbque/resource_accounter.h"
#include "bbque/res/resource_path.h"

#ifdef CONFIG_BBQUE_ENERGY_MONITOR
#include "bbque/energy_monitor.h"
#endif

#define MODULE_NAMESPACE "bq.pp.nvidia"

namespace br = bbque::res;
namespace po = boost::program_options;

namespace bbque {

namespace pp {

NVIDIAPlatformProxy * NVIDIAPlatformProxy::GetInstance()
{
	static NVIDIAPlatformProxy * instance;
	if (instance == nullptr)
		instance = new NVIDIAPlatformProxy();
	return instance;
}

NVIDIAPlatformProxy::NVIDIAPlatformProxy()
{
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	this->platform_id = BBQUE_PP_NVML_PLATFORM_ID;
	this->hardware_id = BBQUE_PP_NVML_HARDWARE_ID;
}

NVIDIAPlatformProxy::~NVIDIAPlatformProxy() { }

PlatformProxy::ExitCode_t NVIDIAPlatformProxy::LoadPlatformData()
{
	nvmlReturn_t result;

	// For NVIDIA GPUs we may have only 1 platform
	device_ids.resize(1);
	device_paths.resize(1);

	// Initialization
	result = nvmlInit();
	if (NVML_SUCCESS != result) {
		logger->Error("NVIDIA: initialization error %s", nvmlErrorString(result));
		return PlatformProxy::PLATFORM_ENUMERATION_FAILED;
	}
	logger->Info("NVIDIA: NVML initialized correctly");

	// Get devices
	result = nvmlDeviceGetCount(&device_count);
	if (NVML_SUCCESS != result) {
		logger->Error("NVIDIA: Device error %s", nvmlErrorString(result));
		return PlatformProxy::PLATFORM_ENUMERATION_FAILED;
	}
	logger->Info("NVIDIA: Number of device(s) found: %d", device_count);
	nv_devices = new nvmlDevice_t[device_count];

	for (uint i = 0; i < device_count; ++i) {
		result = nvmlDeviceGetHandleByIndex(i, &nv_devices[i]);
		if (NVML_SUCCESS != result) {
			logger->Debug("Skipping '%d' [Err:&d] ", i, nvmlErrorString(result));
			continue;
		}
	}

	// Local system ID for resource paths construction
	PlatformManager & plm(PlatformManager::GetInstance());
	local_sys_id = plm.GetPlatformDescription().GetLocalSystem().GetId();

	// Register into Resource Accounter and Power Manager
	RegisterDevices();

	// Power management support
#ifdef CONFIG_BBQUE_PM_NVIDIA
	PrintDevicesPowerInfo();
#endif
	return PlatformProxy::PLATFORM_OK;
}

PlatformProxy::ExitCode_t NVIDIAPlatformProxy::MapResources(ba::SchedPtr_t papp,
							    br::ResourceAssignmentMapPtr_t assign_map,
							    bool excl)
{
	(void) papp;
	(void) assign_map;
	(void) excl;
	logger->Warn("NVIDIA: No mapping action implemented");
	return PlatformProxy::PLATFORM_OK;
}

PlatformProxy::ExitCode_t NVIDIAPlatformProxy::RegisterDevices()
{
	nvmlReturn_t result;
	char dev_name[NVML_DEVICE_NAME_BUFFER_SIZE];

	std::string sys_path(br::GetResourceTypeString(br::ResourceType::SYSTEM));
	sys_path += std::to_string(local_sys_id) + ".";

	for (uint16_t dev_id = 0; dev_id < device_count; ++dev_id) {
		logger->Debug("RegisterDevices: looping over devices...");

		result = nvmlDeviceGetName (nv_devices[dev_id], dev_name, NVML_DEVICE_NAME_BUFFER_SIZE);
		if (NVML_SUCCESS != result) {
			logger->Warn("RegisterDevices: failed to get name of device %d: %s",
				dev_id, nvmlErrorString(result));
		}
		else {
			logger->Warn("RegisterDevices:  device id=%d name=%s",
				dev_id, dev_name);
		}

		// Build the resource path
		std::string r_path(sys_path);
		r_path += GetResourceTypeString(br::ResourceType::GPU)
			+ std::to_string(dev_id)
			+ std::string(".pe0");
		logger->Debug("RegisterDevices: r_path=<%s>", r_path.c_str());

		// Add to resource accounter
		ResourceAccounter & ra(ResourceAccounter::GetInstance());
		auto resource = ra.RegisterResource(r_path, "", 100, "NVIDIA");
		auto resource_path = resource->Path();
		bbque_assert(resource_path);
		logger->Debug("RegisterDevices: resource path = <%s>",
			resource_path->ToString().c_str());

#ifdef CONFIG_BBQUE_WM
		PowerMonitor & wm(PowerMonitor::GetInstance());
		wm.Register(resource_path);
#endif

#ifdef CONFIG_BBQUE_ENERGY_MONITOR
		EnergyMonitor & eym(EnergyMonitor::GetInstance());
		eym.RegisterResource(resource_path);
#endif
		// Keep track of device IDs and resource paths relationship
		InsertDeviceID(0, resource_path, dev_id);
		InsertDevicePath(0, dev_id, resource_path);
		logger->Info("RegisterDevices: id=%d type=<%s> model=%s",
			dev_id,
			GetResourceTypeString(br::ResourceType::GPU),
			resource->Model().c_str());
	}

	return PlatformProxy::PLATFORM_OK;
}

void NVIDIAPlatformProxy::Exit()
{
	logger->Debug("Exiting the NVIDIA Platform Proxy...");

	nvmlReturn_t result = nvmlShutdown();
	if (NVML_SUCCESS != result) {
		logger->Warn("NVIDIA: Failed to shutdown NVML [Err:%s]",
			nvmlErrorString(result));
	}
	logger->Notice("NVIDIA Platform Proxy.shutdownend correctly");

	delete nv_devices;
	device_ids.clear();
	device_paths.clear();
}

} // namespace pp
} // namespace bbque
