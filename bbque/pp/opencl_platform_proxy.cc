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

#include <cstring>

#include "bbque/pp/opencl_platform_proxy.h"

#include "bbque/config.h"

#ifdef CONFIG_BBQUE_PM
#include "bbque/power_monitor.h"
#endif

#include "bbque/resource_accounter.h"
#include "bbque/res/binder.h"
#include "bbque/res/resource_path.h"
#include "bbque/app/working_mode.h"

#define MODULE_NAMESPACE "bq.pp.ocl"

namespace br = bbque::res;
namespace po = boost::program_options;

namespace bbque
{

OpenCLPlatformProxy * OpenCLPlatformProxy::GetInstance()
{
	static OpenCLPlatformProxy * instance;
	if (instance == nullptr)
		instance = new OpenCLPlatformProxy();
	return instance;
}

OpenCLPlatformProxy::OpenCLPlatformProxy():
	cm(ConfigurationManager::GetInstance()),
#ifndef CONFIG_BBQUE_PM
	cmm(CommandManager::GetInstance())
#else
	cmm(CommandManager::GetInstance()),
	pm(PowerManager::GetInstance())
#endif
{
	//---------- Get a logger module
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
}

OpenCLPlatformProxy::~OpenCLPlatformProxy()
{

}

PlatformProxy::ExitCode_t OpenCLPlatformProxy::LoadPlatformData()
{
	cl_int status;
	cl_device_type dev_type = CL_DEVICE_TYPE_ALL;

	// Get platform
	status = clGetPlatformIDs(0, NULL, &num_platforms);
	if (status != CL_SUCCESS) {
		logger->Error("LoadPlatformData: platform error %d", status);
		return PlatformProxy::PLATFORM_ENUMERATION_FAILED;
	}
	logger->Info("LoadPlatformData: number of platform(s) found: %d", num_platforms);
	platforms = new cl_platform_id[num_platforms];
	status = clGetPlatformIDs(num_platforms, platforms, NULL);
	devices.resize(num_platforms);
	device_ids.resize(num_platforms);

	// Local system ID for resource paths construction
	PlatformManager & plm(PlatformManager::GetInstance());
	local_sys_id = plm.GetPlatformDescription().GetLocalSystem().GetId();

//	logger->Info("LoadPlatformData: looking for platform <%s>", TARGET_OPENCL_PLATFORM);
	for (uint32_t id = 0; id < num_platforms; ++id) {
		char platform_name[128];
		status = clGetPlatformInfo(
		                 platforms[id], CL_PLATFORM_NAME, sizeof(platform_name),
		                 platform_name, NULL);
		logger->Info("LoadPlatformData: platform <%d> = %s", id, platform_name);
		cl_platform_id platform = platforms[id];

		// Get devices
		status = clGetDeviceIDs(platform, dev_type, 0, NULL, &num_devices);
		if (status != CL_SUCCESS) {
			logger->Error("LoadPlatformData: device error %d", status);
			return PlatformProxy::PLATFORM_ENUMERATION_FAILED;
		}
		logger->Info("LoadPlatformData: number of device(s) found: %d", num_devices);
		devices[id] = new cl_device_id[num_devices];
		status = clGetDeviceIDs(platform, dev_type, num_devices, devices[id], NULL);

		// Register into Resource Accounter and Power Manager
		RegisterDevices(id);
	}


	// Power management support
#ifdef CONFIG_BBQUE_PM
	PrintDevicesPowerInfo();
#endif
	return PlatformProxy::PLATFORM_OK;
}


PlatformProxy::ExitCode_t OpenCLPlatformProxy::Setup(SchedPtr_t papp)
{
	(void) papp;
	logger->Warn("PLAT OCL: No setup action implemented");
	return PlatformProxy::PLATFORM_OK;
}

PlatformProxy::ExitCode_t OpenCLPlatformProxy::Refresh()
{

	return PlatformProxy::PLATFORM_OK;
}

PlatformProxy::ExitCode_t OpenCLPlatformProxy::Release(SchedPtr_t papp)
{
	(void) papp;
	logger->Warn("PLAT OCL: No release action implemented");
	return PlatformProxy::PLATFORM_OK;
}

PlatformProxy::ExitCode_t OpenCLPlatformProxy::ReclaimResources(SchedPtr_t papp)
{
	(void) papp;
	logger->Warn("PLAT OCL: No reclaiming action implemented");
	return PlatformProxy::PLATFORM_OK;
}

PlatformProxy::ExitCode_t OpenCLPlatformProxy::MapResources(
        ba::SchedPtr_t papp,
        br::ResourceAssignmentMapPtr_t assign_map,
        bool excl)
{
	(void) papp;
	(void) assign_map;
	(void) excl;
	logger->Warn("PLAT OCL: No mapping action implemented");
	return PlatformProxy::PLATFORM_OK;
}


bool OpenCLPlatformProxy::IsHighPerformance(
        bbque::res::ResourcePathPtr_t const & path) const
{
	UNUSED(path)
	return true;
}


#ifdef CONFIG_BBQUE_PM

void OpenCLPlatformProxy::PrintPowerInfo(br::ResourcePathPtr_t r_path)
{
	uint32_t min, max, step, s_min, s_max, ps_count;
	int  s_step;
	auto pm_result = pm.GetFanSpeedInfo(r_path, min, max, step);
	if (pm_result == PowerManager::PMResult::OK)
		logger->Info("PrintPowerInfo: [%s] Fanspeed range: [%4d, %4d, s:%2d] RPM ",
			     r_path->ToString().c_str(), min, max, step);

	pm_result = pm.GetVoltageInfo(r_path, min, max, step);
	if (pm_result == PowerManager::PMResult::OK)
		logger->Info("PrintPowerInfo: [%s] Voltage range:  [%4d, %4d, s:%2d] mV ",
			     r_path->ToString().c_str(), min, max, step);

	pm_result = pm.GetClockFrequencyInfo(r_path, min, max, step);
	if (pm_result == PowerManager::PMResult::OK)
		logger->Info("PrintPowerInfo: [%s] ClkFreq range:  [%4d, %4d, s:%2d] MHz ",
			     r_path->ToString().c_str(),
			     min / 1000, max / 1000, step / 1000);

	std::vector<uint32_t> freqs;
	std::string freqs_str(" ");
	pm_result = pm.GetAvailableFrequencies(r_path, freqs);
	if (pm_result == PowerManager::PMResult::OK) {
		for (auto & f : freqs)
			freqs_str += (std::to_string(f) + " ");
		logger->Info("PrintPowerInfo: [%s] ClkFrequencies:  [%s] MHz ",
			     r_path->ToString().c_str(), freqs_str.c_str());
	}

	pm_result = pm.GetPowerStatesInfo(r_path, s_min, s_max, s_step);
	if (pm_result == PowerManager::PMResult::OK)
		logger->Info("PrintPowerInfo: [%s] Power states:   [%4d, %4d, s:%2d] ",
			     r_path->ToString().c_str(), s_min, s_max, s_step);

	pm_result = pm.GetPerformanceStatesCount(r_path, ps_count);
	if (pm_result == PowerManager::PMResult::OK)
		logger->Info("PrintPowerInfo: [%s] Performance states: %2d",
			     r_path->ToString().c_str(), ps_count);
//		pm.SetFanSpeed(r_path,PowerManager::FanSpeedType::PERCENT, 5);
//		pm.ResetFanSpeed(r_path);
}

void OpenCLPlatformProxy::PrintDevicesPowerInfo()
{
	auto gpus(GetDevicePaths(br::ResourceType::GPU));
	if (gpus == nullptr) {
		logger->Debug("PrintPowerInfo: no GPUs installed on the current system");
		return;
	}

	for (auto gpu_rp : *(gpus.get())) {
		PrintPowerInfo(gpu_rp);
	}
	logger->Info("PrintPowerInfo: number of GPUs %d", gpus->size());

	auto accelerators(GetDevicePaths(br::ResourceType::ACCELERATOR));
	for (auto acc_rp : *(accelerators.get())) {
		PrintPowerInfo(acc_rp);
	}
	logger->Info("PrintPowerInfo: number of accelerators = %d", accelerators->size());
}
#endif // CONFIG_BBQUE_PM


VectorUInt8Ptr_t OpenCLPlatformProxy::GetDeviceIDs(
	uint32_t platform_id, br::ResourceType r_type) const
{
	logger->Debug("GetDeviceIDs: r_type=%s", br::GetResourceTypeString(r_type));
	if (platform_id >= devices.size()) {
		logger->Error("GetDeviceIDs: invalid platform_id=%d",
		              platform_id);
		return nullptr;
	}

	auto curr_dev_ids = device_ids[platform_id];	
	auto const d_it(GetDeviceConstIterator(curr_dev_ids, r_type));
	if (d_it == curr_dev_ids.end()) {
		logger->Warn("GetDeviceIDs: No OpenCL devices of type <%s>",
		              br::GetResourceTypeString(r_type));
		return nullptr;
	}
	return d_it->second;
}


uint8_t OpenCLPlatformProxy::GetDevicesNum(br::ResourceType r_type) const
{
	logger->Debug("GetDevicesNum: r_type=%s", br::GetResourceTypeString(r_type));
	auto devices = GetDevicePaths(r_type);
	if (devices == nullptr) {
		logger->Debug("GetDevicesNum: number of OpenCL <%s> = 0", 
		              br::GetResourceTypeString(r_type));
		return 0;
	}

	return devices->size();
}

ResourcePathListPtr_t OpenCLPlatformProxy::GetDevicePaths(
        br::ResourceType r_type) const
{
	logger->Debug("GetDevicePaths: r_type=%s", br::GetResourceTypeString(r_type));
	auto const p_it(device_paths.find(r_type));
	if (p_it == device_paths.end()) {
		logger->Debug("GetDevicePaths: no OpenCL devices of type <%s>",
		              br::GetResourceTypeString(r_type));
		return nullptr;
	}
	return p_it->second;
}


ResourceTypeIDMap_t::iterator
OpenCLPlatformProxy::GetDeviceIterator(
	ResourceTypeIDMap_t & dev_id_map,
	br::ResourceType r_type)
{
	if (platforms == nullptr) {
		logger->Error("GetDeviceIterator: OpenCL platforms missing");
		return dev_id_map.end();
	}
	return	dev_id_map.find(r_type);
}

ResourceTypeIDMap_t::const_iterator
OpenCLPlatformProxy::GetDeviceConstIterator(
	ResourceTypeIDMap_t const & dev_id_map,
	br::ResourceType r_type) const
{
	if (platforms == nullptr) {
		logger->Error("GetDeviceConstIterator: OpenCL platforms missing");
		return dev_id_map.end();
	}
	return	dev_id_map.find(r_type);
}


PlatformProxy::ExitCode_t OpenCLPlatformProxy::RegisterDevices(uint32_t platform_id)
{
	cl_int status;
	cl_device_type dev_type;
	char dev_name[64];

	// Each OpenCL plaform is under a specific GROUP domain
	std::string sys_path(br::GetResourceTypeString(br::ResourceType::SYSTEM));
	sys_path += std::to_string(local_sys_id) + ".";

	br::ResourceType r_type = br::ResourceType::UNDEFINED;
	for (uint16_t dev_id = 0; dev_id < num_devices; ++dev_id) {
		status = clGetDeviceInfo(
		                 devices[platform_id][dev_id], CL_DEVICE_NAME,
		                 sizeof(dev_name), dev_name, NULL);
		status = clGetDeviceInfo(
		                 devices[platform_id][dev_id], CL_DEVICE_TYPE,
		                 sizeof(dev_type), &dev_type, NULL);
		if (status != CL_SUCCESS) {
			logger->Error("RegisterDevices: device info error %d", status);
			return PLATFORM_ENUMERATION_FAILED;
		}

		std::string r_path(sys_path);

		// Register devices of type GPU or ACCELERATOR
		switch (dev_type) {

		case CL_DEVICE_TYPE_CPU:
			r_type = br::ResourceType::CPU;
			break;

		// GPU or ACCELERATOR -> add a reference to the OpenCL platform (GROUP)
		case CL_DEVICE_TYPE_GPU:
			r_type = br::ResourceType::GPU;
			r_path += br::GetResourceTypeString(br::ResourceType::GROUP)
			          + std::to_string(platform_id);
			break;

		case CL_DEVICE_TYPE_ACCELERATOR:
			r_type = br::ResourceType::ACCELERATOR;
			r_path += br::GetResourceTypeString(br::ResourceType::GROUP)
			          + std::to_string(platform_id);
			break;

		default:
			logger->Warn("RegisterDevices: id=%d is of unexpected type [%d]",
			             dev_id, dev_type);
			continue;
		}

		logger->Info("RegisterDevices: id=%d is of type %s",
			     dev_id, br::GetResourceTypeString(r_type));

		// Resource path string
		r_path += br::GetResourceTypeString(r_type) + std::to_string(dev_id)
		          + std::string(".")
		          + br::GetResourceTypeString(br::ResourceType::PROC_ELEMENT);
		logger->Debug("RegisterDevices: r_path=<%s>", r_path.c_str());

		br::ResourcePathPtr_t r_path_ptr;
		ResourceAccounter &ra(ResourceAccounter::GetInstance());

		// No need to register CPU devices (already done in the host
		// platform proxy)
		if (r_type != br::ResourceType::CPU) {
			r_path += std::string("0");
			auto resource = ra.RegisterResource(r_path, "", 100);
			r_path_ptr = resource->Path();
#ifdef CONFIG_BBQUE_WM
			PowerMonitor & wm(PowerMonitor::GetInstance());
			wm.Register(r_path_ptr);
#endif
		}
		else {
			// Point the first core of the current CPU
			auto cpu_pes = ra.GetResources(r_path);
			bbque_assert(!cpu_pes.empty());
			r_path_ptr = cpu_pes.front()->Path();
		}

		bbque_assert(r_path_ptr);
		logger->Debug("RegisterDevices: r_path_ptr=<%s>", r_path_ptr->ToString().c_str());

		// Keep track of OpenCL device IDs and resource paths
		InsertDeviceID(platform_id, dev_id, r_type);
		InsertDevicePath(r_type, r_path_ptr);

		logger->Info("RegisterDevices: id=%d {%s}, type=[%s], path=<%s>",
		             dev_id, dev_name,
		             br::GetResourceTypeString(r_type),
		             r_path_ptr->ToString().c_str());
	}

	return PlatformProxy::PLATFORM_OK;
}


void OpenCLPlatformProxy::InsertDeviceID(
        uint32_t platform_id,
        uint8_t dev_id,
        br::ResourceType r_type)
{
	logger->Debug("InsertDeviceID: insert device %d of type <%s>",
	              dev_id, br::GetResourceTypeString(r_type));

	auto curr_dev_ids = device_ids[platform_id];	
	auto d_it = GetDeviceIterator(curr_dev_ids, r_type);
	if (d_it == curr_dev_ids.end()) {
		curr_dev_ids.emplace(r_type, std::make_shared<VectorUInt8_t>());
	}

	auto pdev_ids = curr_dev_ids[r_type];
	pdev_ids->push_back(dev_id);
	logger->Debug("InsertDeviceID: type [%s]: count=%d",
	              br::GetResourceTypeString(r_type),
	              curr_dev_ids[r_type]->size());
}

void OpenCLPlatformProxy::InsertDevicePath(
        br::ResourceType r_type,
        br::ResourcePathPtr_t r_path_ptr)
{
	logger->Debug("InsertDevicePath: insert device resource path <%s>",
	              r_path_ptr->ToString().c_str());

	auto p_it = device_paths.find(r_type);
	if (p_it == device_paths.end()) {
		device_paths.emplace(
			r_type,
			std::make_shared<std::list<br::ResourcePathPtr_t>>());
	}

	auto pdev_paths = device_paths[r_type];
	pdev_paths->push_back(r_path_ptr);
	logger->Debug("InsertDevicePath: type [%s]: count=%d",
	              br::GetResourceTypeString(r_type),
	              device_paths[r_type]->size());
}

void OpenCLPlatformProxy::Exit()
{
	logger->Info("Exit: terminating OpenCL Platform Proxy...");
	delete[] platforms;

	for (auto & devs: devices) {
		delete[] devs;
	}
	devices.clear();

	device_ids.clear();
	device_paths.clear();
}

} // namespace bbque
