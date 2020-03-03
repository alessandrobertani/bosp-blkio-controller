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

#ifndef BBQUE_LINUX_OCLPROXY_H_
#define BBQUE_LINUX_OCLPROXY_H_

#include <fstream>
#include <list>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <CL/cl.h>

#include "bbque/config.h"
#include "bbque/configuration_manager.h"
#include "bbque/command_manager.h"
#include "bbque/modules_factory.h"
#include "bbque/platform_proxy.h"
#include "bbque/app/application.h"
#include "bbque/pm/power_manager.h"
#include "bbque/res/identifier.h"
#include "bbque/utils/worker.h"

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque
{

using DevPathMap_t = std::map<int, ResourcePathPtr_t>;
using PathDevMap_t = std::map<ResourcePathPtr_t, int>;

class OpenCLPlatformProxy: public PlatformProxy
{

public:

	static OpenCLPlatformProxy * GetInstance();

	/**
	 * @brief Destructor
	 */
	virtual ~OpenCLPlatformProxy();

	/**
	 * @brief Return the Platform specific string identifier
	 */
	const char * GetPlatformID(int16_t system_id = -1) const override
	{
		(void) system_id;
		return "org.opencl";
	}

	/**
	 * @brief Return the Hardware identifier string
	 */
	const char * GetHardwareID(int16_t system_id = -1) const override
	{
		(void) system_id;
		return "opencl";
	}

	/**
	 * @brief Platform specific resource setup interface.
	 */
	ExitCode_t Setup(SchedPtr_t papp) override;

	/**
	 * @brief Load OpenCL platform data function
	 */
	ExitCode_t LoadPlatformData() override;

	/**
	 * @brief OpenCL specific resources refresh function
	 */
	ExitCode_t Refresh() override;
	/**
	 * @brief OpenCL specific resources release function
	 */
	ExitCode_t Release(SchedPtr_t papp) override;

	/**
	 * @brief OpenCL specific resource claiming function
	 */
	ExitCode_t ReclaimResources(SchedPtr_t papp) override;

	/**
	 * @brief OpenCL resource assignment mapping
	 */
	ExitCode_t MapResources(
	        ba::SchedPtr_t papp, br::ResourceAssignmentMapPtr_t assign_map,
	        bool excl) override;

	/**
	 * OpenCL specific termination
	 */
	void Exit() override;


	// Class specific member functions

	/**
	 * @brief OpenCL device id associated to a resource path
	 * @param platform_id the OpenCL platform
	 * @param r_path the resource path
	 * @return the device id as integer value
	 */
	int GetDeviceID(uint32_t platform_id, ResourcePathPtr_t r_path) const;

	/**
	 * @brief First OpenCL device id pf a given type (CPU, GPU, ...)
	 * @param platform_id the OpenCL platform
	 * @param r_type the resource path
	 * @return the device id as integer value
	 */
	int GetFirstDeviceID(uint32_t platform_id, ResourceType r_type) const;

	/**
	 * @brief Resource path associated to the OpenCL device id
	 * @param platform_id the OpenCL platform
	 * @param device_id the device id as integer value
	 * @return r_path the resource path
	 */
	ResourcePathPtr_t GetDevicePath(uint32_t platform_id, int device_id) const;

protected:

	/*** Configuration manager instance */
	ConfigurationManager & cm;

	/*** Command manager instance */
	CommandManager & cmm;

	/**
	 * @brief The logger
	 */
	std::unique_ptr<bu::Logger> logger;


	/*** Number of platforms */
	cl_uint num_platforms;

	/*** Number of devices   */
	cl_uint num_devices;

	/*** Platform descriptors */
	cl_platform_id * platforms;

	/*** Device descriptors   */
	std::vector<cl_device_id *> devices;

	/*** Map with all the device IDs for each type available   */
	//std::vector<ResourceTypeIDMap_t> device_ids;
	std::vector<DevPathMap_t> device_paths;

	/*** Map with all the device paths for each type available */
	std::vector<PathDevMap_t> device_ids;

	/*** Map with the resource paths of GPUs memory */
	std::vector<PathDevMap_t> mem_paths;

	/*** Local ID of the system resource */
	uint16_t local_sys_id;

	/*** Constructor */
	OpenCLPlatformProxy();


#ifdef CONFIG_BBQUE_PM

	/*** Power Manager instance */
	PowerManager & pm;

	void PrintPowerInfo(br::ResourcePathPtr_t r_path_ptr) const;

	void PrintDevicesPowerInfo() const;

#endif // CONFIG_BBQUE_PM

	/**
	 * @brief Register device resources
	 * @param platform_id The OpenCL platform ID
	 */
	ExitCode_t RegisterDevices(uint32_t platform_id);

	/**
	 * @brief Keep track of the device id -> resource path relationship
	 *
	 * @param platform_id The OpenCL platform ID
	 * @param r_path the registered resource path of the device
	 * @param dev_id The OpenCL device ID
	 */
	void InsertDeviceID(uint32_t platform_id, ResourcePathPtr_t r_path, int dev_id);

	/**
	 * @brief Keep track of the resource path -> device id relationship
	 *
	 * @param platform_id The OpenCL platform ID
	 * @param dev_id The OpenCL device ID
	 * @param r_path the registered resource path of the device
	 */
	void InsertDevicePath(uint32_t platform_id, int dev_id, ResourcePathPtr_t r_path);

};

} // namespace bbque

#endif
