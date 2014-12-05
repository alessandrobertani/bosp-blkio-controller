/*
 * Copyright (C) 2012  Politecnico di Milano
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
#include "bbque/app/application.h"
#include "bbque/pm/power_manager.h"
#include "bbque/res/identifier.h"
#include "bbque/utils/worker.h"

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque {

typedef std::list<ResourcePathPtr_t> ResourcePathList_t;
typedef std::shared_ptr<ResourcePathList_t> ResourcePathListPtr_t;
typedef std::vector<uint8_t> VectorUInt8_t;
typedef std::shared_ptr<VectorUInt8_t> VectorUInt8Ptr_t;
typedef std::map<br::ResourceIdentifier::Type_t, VectorUInt8Ptr_t> ResourceTypeIDMap_t;
typedef std::map<br::ResourceIdentifier::Type_t, ResourcePathListPtr_t> ResourceTypePathMap_t;
typedef std::map<int, std::ofstream *> DevFileMap_t;
typedef std::map<int, ResourcePathPtr_t> DevPathMap_t;

class OpenCLProxy: public Worker, public CommandHandler {

public:

	enum ExitCode_t {
		SUCCESS,
		PLATFORM_ERROR,
		DEVICE_ERROR
	};

	/**
	 * @brief Constructor
	 */
	static OpenCLProxy & GetInstance();

	~OpenCLProxy();

	/**
	 * @brief Load OpenCL platform data
	 */
	ExitCode_t LoadPlatformData();

	/**
	 * @brief OpenCL resource assignment mapping
	 */
	ExitCode_t MapResources(ba::AppPtr_t papp, br::UsagesMapPtr_t pum, br::RViewToken_t rvt);

	/**
	 * @brief Number of OpenCL devices of a given resource type
	 */
	uint8_t GetDevicesNum(br::ResourceIdentifier::Type_t r_type);

	/**
	 * @brief Set of OpenCL device IDs for a given resource type
	 */
	VectorUInt8Ptr_t GetDeviceIDs(br::ResourceIdentifier::Type_t r_type);

	/**
	 * @brief Set of OpenCL device resource path for a given type
	 */
	ResourcePathListPtr_t GetDevicePaths(br::ResourceIdentifier::Type_t r_type);

private:

	/*** Constructor */
	OpenCLProxy();

	/*** Configuration manager instance */
	ConfigurationManager & cm;

	/*** Command manager instance */
	CommandManager & cmm;

	/**
	 * @brief The logger used by the power manager.
	 */
	std::unique_ptr<bu::Logger> logger;

	/*** Number of platforms */
	cl_uint num_platforms = 0;

	/*** Number of devices   */
	cl_uint num_devices   = 0;

	/*** Platform descriptors */
	cl_platform_id * platforms = nullptr;

	/*** Device descriptors   */
	cl_device_id   * devices   = nullptr;

	/*** Map with all the device IDs for each type available   */
	ResourceTypeIDMap_t   device_ids;

	/*** Map with all the device paths for each type available */
	ResourceTypePathMap_t device_paths;

	/*** Map with the resource paths of GPUs memory */
	DevPathMap_t gpu_mem_paths;

	/** Retrieve the iterator for the vector of device IDs, given a type */
	ResourceTypeIDMap_t::iterator GetDeviceIterator(
		br::ResourceIdentifier::Type_t r_type);

#ifdef CONFIG_BBQUE_PM

	/*** Power Manager instance */
	PowerManager & pm;

	/*** Print the GPU power information */
	void PrintGPUPowerInfo();

#endif // CONFIG_BBQUE_PM

	/**
	 * @brief Append device ID per device type
	 *
	 * @param r_type The resource type (usually Resource::CPU or Resource::GPU)
	 * @param dev_id The OpenCL device ID
	 */
	void InsertDeviceID(br::ResourceIdentifier::Type_t r_type, uint8_t dev_id);

	/**
	 * @brief Append resource path per device type
	 *
	 * @param r_type The resource type (usually Resource::CPU or Resource::GPU)
	 * @param dev_p_str A resource path referencing a device of the type in the key
	 */
	void InsertDevicePath(
		br::ResourceIdentifier::Type_t r_type, std::string const & dev_rp_str);

	/**
	 * @brief Register device resources
	 */
	ExitCode_t RegisterDevices();

	/**
	 * @brief The OpenCL platform monitoring thread
	 *
	 * Peridic calls to the platform-specific power management support can
	 * be done (if enabled)
	 */
	void Task();

	/**
	 * @brief Commands handler
	 */
	int CommandsCb(int argc, char *argv[]);

};

} // namespace bbque

#endif
