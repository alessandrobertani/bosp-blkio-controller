/*
 * Copyright (C) 2017  Politecnico di Milano
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


#ifndef BBQUE_LOCAL_PLATFORM_PROXY_H_
#define BBQUE_LOCAL_PLATFORM_PROXY_H_

#include "bbque/platform_proxy.h"

#include <memory>
#include <vector>


namespace bbque
{
namespace pp
{
class LocalPlatformProxy : public PlatformProxy
{
public:

	LocalPlatformProxy();

	virtual ~LocalPlatformProxy() { }

	/**
	 * @brief Return the Platform specific string identifier
	 */
	const char* GetPlatformID(int16_t system_id = -1) const;

	/**
	 * @brief Return the Hardware identifier string
	 */
	const char* GetHardwareID(int16_t system_id = -1) const;
	/**
	 * @brief Platform specific resource setup interface.
	 */
	ExitCode_t Setup(SchedPtr_t papp);

	/**
	 * @brief Register/enumerate local node resources
	 *
	 * The default implementation of this method loads the TPD, if such a
	 * options has been enabled at compile-time.
	 */
	ExitCode_t LoadPlatformData();

	/**
	 * @brief Refresh local node resources
	 */
	ExitCode_t Refresh();

	/**
	 * @brief Release resources from the local node.
	 */
	ExitCode_t Release(SchedPtr_t papp);

	/**
	 * @brief Reclaim resources from the local node.
	 */
	ExitCode_t ReclaimResources(SchedPtr_t papp);

	/**
	 * @brief Map the local resource assignments.
	 */
	ExitCode_t MapResources(
	        SchedPtr_t papp,
	        ResourceAssignmentMapPtr_t pres,
	        bool excl = true);

	/**
	 * @brief Actuate power management for local node resources and
	 * system-specific settings.
	 */
	ExitCode_t ActuatePowerManagement() override;

	/**
	 * @brief Local termination.
	 */
	void Exit();

	/**
	 * @brief Check if a local resource is "high-performance" qualified.
	 */
	bool IsHighPerformance(
	        bbque::res::ResourcePathPtr_t const & path) const override;

private:

	std::unique_ptr<bu::Logger> logger;

	/**
	 * @brief The host platform proxy, e.g. Linux or Android, managing the low-level
	 * access to the local CPUs and OS services.
	 */
	std::unique_ptr<PlatformProxy> host;

	/**
	 * @brief The list of auxiliary platform proxy, used for managing additional
	 * computing resources (like OpenCL, HW accelerators, Process Listener, etc...)
	 */
	std::vector<std::unique_ptr<PlatformProxy>> aux;

};

}   // namespace pp
}   // namespace bbque

#endif // LOCALPLATFORMPROXY_H
