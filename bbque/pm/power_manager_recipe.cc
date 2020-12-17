/*
 * Copyright (C) 2018  Politecnico di Milano
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

/**
 * Launch the HN daemon with the mmi64 communication enabled:
 * $ hn_daemon -P <port> -R
 */

#include <array>
#include <cstring>
#include <string>

#include "bbque/pm/power_manager_recipe.h"
#include "CL/cl.h"

using namespace bbque::res;

namespace bbque {

RecipePowerManager::RecipePowerManager()
{
	logger->Info("RecipePowerManager initialization...");

	cl_uint num_platforms;
	auto status = clGetPlatformIDs(0, NULL, &num_platforms);
	if (status != CL_SUCCESS) {
		logger->Error("RecipePowerManager: platform error %d", status);
		return;
	}


	logger->Info("RecipePowerManager: nr. platform(s) found: %d", num_platforms);
	std::array<fpgamon_platform_ids_t, 4> platform_ids;

	this->ocl_platforms = new cl_platform_id[num_platforms];
	status = clGetPlatformIDs(num_platforms, ocl_platforms, NULL);

	for (cl_uint id = 0; id < num_platforms; ++id) {
		char platform_name[50];
		status = clGetPlatformInfo(
					ocl_platforms[id],
					CL_PLATFORM_NAME,
					sizeof(platform_name),
					platform_name,
					NULL);

		// Initialization data for libfpgamon
		std::string pn(platform_name);
		if (pn.find("Intel(R) FPGA") != std::string::npos) {
			logger->Info("RecipePowerManager: platform [%s] -> PROFPGA monitoring",
				platform_name);
			platform_ids[id] = FPGAMON_PLATFORM_PROFPGA;
		}
		else {
			platform_ids[id] = FPGAMON_PLATFORM_DUMMY;
		}

		// Get devices
		cl_uint num_devices;
		status = clGetDeviceIDs(this->ocl_platforms[id], CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
		if (status != CL_SUCCESS) {
			logger->Error("RecipePowerManager: device ids error %d", status);
			return;
		}
		logger->Info("RecipePowerManager: platform [%s] includes %d device(s)",
			platform_name, num_devices);

		this->ocl_devices = new cl_device_id[num_devices];
		status = clGetDeviceIDs(this->ocl_platforms[id],
					CL_DEVICE_TYPE_ALL,
					num_devices,
					ocl_devices,
					NULL);

		for (uint16_t dev_id = 0; dev_id < num_devices; ++dev_id) {
			char dev_name[64];
			status = clGetDeviceInfo(
						this->ocl_devices[dev_id],
						CL_DEVICE_NAME,
						sizeof(dev_name),
						dev_name,
						NULL);
			if (status == CL_SUCCESS) {
				logger->Info("RecipePowerManager: device %d:%s",
					dev_id, dev_name);
			}
		}



	}

	// FPGA Monitoring library initialization
	fpgamon_init(&this->ctx, num_platforms, platform_ids.data());

}

RecipePowerManager::~RecipePowerManager()
{
	fpgamon_shutdown(&this->ctx);
	delete ocl_platforms;
	delete ocl_devices;
}

PowerManager::PMResult RecipePowerManager::ErrorHandler(int acc_id, int err)
{
	switch (err) {
	case CL_INVALID_DEVICE:
		logger->Error("ErrorHandler: accelerator=<%d> invalid device",
			acc_id);
		return PMResult::ERR_RSRC_INVALID_PATH;
	case CL_INVALID_VALUE:
		logger->Error("ErrorHandler: accelerator=<%d> invalid value returned",
			acc_id);
		return PMResult::ERR_SENSORS_ERROR;
	default:
		logger->Error("ErrorHandler: accelerator=<%d> [error=%d]",
			acc_id, err);
		return PMResult::ERR_UNKNOWN;
	}
}

PowerManager::PMResult
RecipePowerManager::GetLoad(ResourcePathPtr_t const & rp, uint32_t &perc)
{
	int plat_id, dev_id;
	auto ret = GetPlatformAndDeviceIds(rp, plat_id, dev_id);
	if (ret != PMResult::OK) {
		logger->Error("GetLoad: invalid resource path");
		return ret;
	}

	perc = fpgamon_get_load(&this->ctx, plat_id, dev_id);
	return PMResult::OK;
}

PowerManager::PMResult
RecipePowerManager::GetTemperature(ResourcePathPtr_t const & rp, uint32_t &celsius)
{
	int plat_id, dev_id;
	auto ret = GetPlatformAndDeviceIds(rp, plat_id, dev_id);
	if (ret != PMResult::OK) {
		logger->Error("GetTemperature: invalid resource path");
		return ret;
	}

	celsius = fpgamon_get_temperature(&this->ctx, plat_id, dev_id);
	return PMResult::OK;
}

PowerManager::PMResult
RecipePowerManager::GetPowerUsage(ResourcePathPtr_t const & rp, uint32_t &mwatt)
{
	int plat_id, dev_id;
	auto ret = GetPlatformAndDeviceIds(rp, plat_id, dev_id);
	if (ret != PMResult::OK) {
		logger->Error("GetPowerUsage: invalid resource path");
		return ret;
	}

	mwatt = fpgamon_get_power(&this->ctx, plat_id, dev_id);
	return PMResult::OK;
}

PowerManager::PMResult
RecipePowerManager::GetPlatformAndDeviceIds(br::ResourcePathPtr_t const & rp,
					    int & plat_id,
					    int & dev_id) const
{
	plat_id = rp->GetID(br::ResourceType::GROUP);
	if (plat_id < 0) {
		logger->Error("GetPlatformAndDeviceIds: <%s> -> platform id = %d",
			rp->ToString().c_str(), plat_id);
		return PMResult::ERR_RSRC_INVALID_PATH;
	}

	dev_id = rp->GetID(br::ResourceType::ACCELERATOR);
	if (dev_id) {
		logger->Error("GetPlatformAndDeviceIds: <%s> -> device id = %d",
			rp->ToString().c_str(), dev_id);
		return PMResult::ERR_RSRC_INVALID_PATH;
	}

	return PMResult::OK;
}


} // namespace bbque

