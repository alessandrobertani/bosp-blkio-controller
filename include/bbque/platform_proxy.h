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

#ifndef BBQUE_PLATFORM_PROXY_H_
#define BBQUE_PLATFORM_PROXY_H_

#include "bbque/app/application.h"
#include "bbque/config.h"
#include "bbque/modules_factory.h"
#include "bbque/res/resource_assignment.h"
#include "bbque/pp/platform_description.h"
#include "bbque/pp/cr/reliability_actions_if.h"

#include <cstdint>

#define PLATFORM_PROXY_NAMESPACE "bq.pp"

using bbque::res::ResourceAssignmentMapPtr_t;
using bbque::res::RViewToken_t;
using bbque::app::SchedPtr_t;
using bbque::pp::ReliabilityActionsIF;

namespace bbque
{

/**
 * @class PlatformProxy
 * @brief The PlatformProxy class is the interface for all PlatformProxy
 * classes. The access to these classes is provided by the PlatformManager
 */
class PlatformProxy: public ReliabilityActionsIF
{

public:

	PlatformProxy(): ReliabilityActionsIF("/tmp") { }

	virtual ~PlatformProxy() {}

	/**
	 * @brief Exit codes returned by methods of this class
	 */
	typedef enum ExitCode {
	        PLATFORM_OK = 0,
	        PLATFORM_GENERIC_ERROR,
	        PLATFORM_INIT_FAILED,
	        PLATFORM_ENUMERATION_FAILED,
	        PLATFORM_LOADING_FAILED,
	        PLATFORM_NODE_PARSING_FAILED,
	        PLATFORM_DATA_NOT_FOUND,
	        PLATFORM_DATA_PARSING_ERROR,
	        PLATFORM_COMM_ERROR,
	        PLATFORM_MAPPING_FAILED,
	        PLATFORM_PWR_MONITOR_ERROR,
	        PLATFORM_PWR_SETTING_ERROR,
	        PLATFORM_AGENT_PROXY_ERROR
	} ExitCode_t;

	/**
	 * @brief Return the Platform specific string identifier
	 * @param system_id It specifies from which system take the
	 *                  platform identifier. If not specified or equal
	 *                  to "-1", the platform id of the local system is returned.
	 */
	virtual const char* GetPlatformID(int16_t system_id = -1) const = 0;

	/**
	 * @brief Return the Hardware identifier string
	 * @param system_id It specifies from which system take the
	 *                  hardware identifier. If not specified or equal
	 *                  to "-1", the hw id of the local system is returned.
	 */
	virtual const char* GetHardwareID(int16_t system_id = -1) const = 0;

	/**
	 * @brief Platform specific resource setup interface.
	 */
	virtual ExitCode_t Setup(SchedPtr_t papp) = 0;

	/**
	 * @brief Platform specific resources enumeration
	 *
	 * The default implementation of this method loads the TPD, is such a
	 * function has been enabled
	 */
	virtual ExitCode_t LoadPlatformData() = 0;

	/**
	 * @brief Platform specific resources refresh
	 */
	virtual ExitCode_t Refresh() = 0;

	/**
	 * @brief Platform specific resources release interface.
	 */
	virtual ExitCode_t Release(SchedPtr_t papp) = 0;

	/**
	 * @brief Platform specific resource claiming interface.
	 */
	virtual ExitCode_t ReclaimResources(SchedPtr_t papp) = 0;

	/**
	 * @brief Bind the specified resources to the specified application.
	 *
	 * @param papp The application which resources are assigned
	 * @param pres The resources to be assigned
	 * @param excl If true the specified resources are assigned for exclusive
	 * usage to the application
	 */
	virtual ExitCode_t MapResources(
	        SchedPtr_t papp, ResourceAssignmentMapPtr_t pres, bool excl = true) = 0;

	/**
	 * @brief Set the power management configuration if set by the policy
	 * @return PLATFORM_OK for success
	 */
	virtual ExitCode_t ActuatePowerManagement();

	/**
	 * @brief Set the power management configuration of a specific resource
	 * if set by the policy
	 */
	virtual ExitCode_t ActuatePowerManagement(
	        bbque::res::ResourcePtr_t resource);

	/**
	 * @brief Graceful closure of the platform proxy
	 */
	virtual void Exit() = 0;

	/**
	 * @brief Check if a resource is flagged as "high-performance".
	 * This is useful in case of heterogeneous architecture as
	 * ARM big.LITTLE CPUs.
	 *
	 * @param path Path to the resource
	 * @return true or false
	 */
	virtual bool IsHighPerformance(
	        bbque::res::ResourcePathPtr_t const & path) const = 0;


#ifndef CONFIG_BBQUE_PIL_LEGACY
	/**
	* @brief Return the platform description loaded by the relative plugin.
	*        This method is not available with legacy parser.
	*/
	static const pp::PlatformDescription & GetPlatformDescription();
#endif

	virtual ReliabilityActionsIF::ExitCode_t Dump(uint32_t exe_id) override;

	virtual ReliabilityActionsIF::ExitCode_t Dump(app::SchedPtr_t psched) override;


	virtual ReliabilityActionsIF::ExitCode_t Restore(uint32_t exe_id) override;

	virtual ReliabilityActionsIF::ExitCode_t Restore(app::SchedPtr_t psched) override;


	virtual ReliabilityActionsIF::ExitCode_t Freeze(uint32_t exe_id) override;

	virtual ReliabilityActionsIF::ExitCode_t Freeze(app::SchedPtr_t psched) override;


	virtual ReliabilityActionsIF::ExitCode_t Thaw(uint32_t exe_id) override;

	virtual ReliabilityActionsIF::ExitCode_t Thaw(app::SchedPtr_t papp) override;


protected:

	static plugins::PlatformLoaderIF* pli;
};

} // namespace bbque

#endif // BBQUE_PLATFORM_PROXY_H_
