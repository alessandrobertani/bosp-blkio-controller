/*
 * Copyright (C) 2014  Politecnico di Milano
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

#ifndef BBQUE_CLOVES_SCHEDPOL_H_
#define BBQUE_CLOVES_SCHEDPOL_H_

#include <cstdint>
#include <list>
#include <memory>
#include <map>
#include <queue>

#include "bbque/configuration_manager.h"
#include "bbque/binding_manager.h"
#include "bbque/power_monitor.h"
#include "bbque/plugins/plugin.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/scheduler_manager.h"

#define SCHEDULER_POLICY_NAME "cloves"

#define MODULE_NAMESPACE SCHEDULER_POLICY_NAMESPACE "." SCHEDULER_POLICY_NAME

#define BBQUE_SP_CLOVES_SAMPLING_TIME 200

using bbque::res::RViewToken_t;
using bbque::utils::MetricsCollector;
using bbque::utils::Timer;

// These are the parameters received by the PluginManager on create calls
struct PF_ObjectParams;

namespace bbque
{
namespace plugins
{

class LoggerIF;

/**
 * @class ClovesSchedPol
 *
 * This is the core of a scheduling policy plugin mainly targeting
 * heterogeneous workloads, i.e. applications (like OpenCL ones) that can be
 * executed on different computing devices
 */
class ClovesSchedPol: public SchedulerPolicyIF
{

public:

	// :::::::::::::::::::::: Static plugin interface :::::::::::::::::::::::::

	/**
	 * @brief Create the cloves plugin
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 * @brief Destroy the cloves plugin
	 */
	static int32_t Destroy(void *);


	// :::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

	/**
	 * @brief Destructor
	 */
	virtual ~ClovesSchedPol();

	/**
	 * @brief Return the name of the policy plugin
	 */
	char const * Name();


	/**
	 * @brief The member function called by the SchedulerManager to perform a
	 * new scheduling / resource allocation
	 */
	ExitCode_t Schedule(System & system, RViewToken_t & status_view);

private:

	/** Queue of entity to schedule on the device */
	typedef std::priority_queue<SchedEntityPtr_t> DeviceQueue_t;
	typedef std::shared_ptr<DeviceQueue_t> DeviceQueuePtr_t;

	/** Map of device queues with the resource path as key */
	typedef std::map<br::ResourcePathPtr_t, DeviceQueuePtr_t> DeviceQueueMap_t;
	typedef std::shared_ptr<DeviceQueueMap_t> DeviceQueueMapPtr_t;

	/** Set of device queues for device type (e.g., CPU, GPU,..) */
	typedef std::map<br::ResourceType, DeviceQueueMapPtr_t> DeviceTypeQueueMap_t;


	ConfigurationManager & cm;

	ResourceAccounter & ra;

	BindingManager & bdm;

	PowerMonitor & wm;

	std::unique_ptr<bu::Logger> logger;


	/** Map of queues, one for device type */
	DeviceTypeQueueMap_t queues;

	bool queues_ready = false;

	/**
	 * @brief Constructor
	 *
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 */
	ClovesSchedPol();

	/**
	 * @brief Optional initialization member function
	 */
	ExitCode_t _Init();

	/**
	 * @brief Initialize the device queuesw
	 */
	ExitCode_t InitDeviceQueues();

	/**
	 * @brief Device queues allocation
	 *
	 * @param pdev_queue_map The map of resource path/device queue
	 * @param bd_info Support information for the resource binding
	 */
	void CreateDeviceQueues(
	        DeviceQueueMapPtr_t & pdev_queue_map,
	        BindingInfo_t const & bd_info);

	/**
	 * @brief Scheduling of the applications/EXCs with a given priority
	 * value
	 *
	 * @param prio The priority value
	 * @return
	 */
	ExitCode_t SchedulePriority(ba::AppPrio_t prio);

	/**
	 * @brief Evaluate the scheduling of the applications/EXCs
	 *
	 * @param papp The application/EXC to schedule
	 * @return
	 */
	ExitCode_t EnqueueIntoDevice(ba::AppCPtr_t papp);

	/**
	 * @brief Enqueue a scheduling entity into a device of the given
	 * resource type
	 *
	 * @param psched The schedule entity to enqueue
	 * @param dev_type The device type (e.g. CPU, GPU)
	 *
	 * @return
	 */
	ExitCode_t Enqueue(
	        SchedEntityPtr_t psched,
	        br::ResourceType dev_type);

	/**
	 * @brief Compute the metrics used for the ordering of the scheduling
	 * entity into the device queue resource type
	 *
	 * @param psched The schedule entity to enqueue
	 */
	void ComputeOrderingMetrics(SchedEntityPtr_t psched);

	/**
	 * @brief Select the best device queue of the given resource type, for
	 * the scheduling entity provided entity into the device queue resource
	 *
	 * @param dev_type The device type (e.g. CPU, GPU)
	 * @param psched The schedule entity to enqueue
	 *
	 * @return A pointer to the device queue
	 */
	DeviceQueuePtr_t SelectDeviceQueue(
	        SchedEntityPtr_t psched,
	        br::ResourceType dev_type);

	/**
	 * @brief Bind the resource path of the AWM, coming from the recipe
	 *
	 * @param psched The schedule entity to enqueue
	 */
	ExitCode_t BindResources(SchedEntityPtr_t psched);

	/**
	 * @brief Flush the device queues, proceeding with the scheduling
	 * requests
	 */
	ExitCode_t Flush();

	/**
	 * @brief Send the scheduling requests for each of the application/EXC
	 * in the queue requests
	 *
	 * @param dev_queue The device queue
	 *
	 * @return The number of successful requests
	 */
	uint32_t SendScheduleRequests(DeviceQueue_t & dev_queue);
};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_CLOVES_SCHEDPOL_H_
