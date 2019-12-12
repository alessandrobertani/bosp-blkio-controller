/*
 * Copyright (C) 2019  Politecnico di Milano
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

#ifndef BBQUE_MANGAV2_SCHEDPOL_H_
#define BBQUE_MANGAV2_SCHEDPOL_H_

#include <cstdint>
#include <list>
#include <future>
#include <memory>

#include "bbque/configuration_manager.h"
#include "bbque/plugins/plugin.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/scheduler_manager.h"
#include "tg/hw.h"

#define SCHEDULER_POLICY_NAME "mangav2"

#define MODULE_NAMESPACE SCHEDULER_POLICY_NAMESPACE "." SCHEDULER_POLICY_NAME

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
 * @class ManGAv2SchedPol
 *
 * ManGAv2 scheduler policy registered as a dynamic C++ plugin.
 */
class ManGAv2SchedPol: public SchedulerPolicyIF
{

public:

	// :::::::::::::::::::::: Static plugin interface :::::::::::::::::::::::::

	/**
	 * @brief Create the mangav2 plugin
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 * @brief Destroy the mangav2 plugin
	 */
	static int32_t Destroy(void *);


	// :::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

	/**
	 * @brief Destructor
	 */
	virtual ~ManGAv2SchedPol();

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

	/** Configuration manager instance */
	ConfigurationManager & cm;

	/** Resource accounter instance */
	ResourceAccounter & ra;

	/** System logger instance */
	std::unique_ptr<bu::Logger> logger;

	/** Future for task-graph loading synchronization */
	std::future<void> fut_tg;

	/** Number of cores per accelerator (and group/cluster) */
	std::map<uint32_t, std::vector<uint32_t>> pe_per_acc;

	/** Type of architecture per accelerator */
	std::map<uint32_t, std::vector<ArchType>> arch_per_acc;

	/** Forced mapping ids (one per ready application) */
	std::vector<int> forced_mapping_ids;

	/** Keep the count of scheduled applications */
	uint32_t apps_count = 0;

	/**
	 * @brief Constructor
	 *
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 */
	ManGAv2SchedPol();

	/**
	 * @brief Optional initialization member function
	 */
	ExitCode_t _Init();

	ExitCode_t SchedulePriority(bbque::app::AppPrio_t priority);

	ExitCode_t ReassignWorkingMode(bbque::app::AppCPtr_t papp);

	ExitCode_t EvalMappingAlternatives(bbque::app::AppCPtr_t papp);

	ExitCode_t CheckMappingFeasibility(bbque::app::AppCPtr_t papp);

	bbque::app::AwmPtr_t SelectWorkingMode(
	        bbque::app::AppCPtr_t papp,
	        int & ref_num);

	ExitCode_t ScheduleApplication(
	        bbque::app::AppCPtr_t papp, bbque::app::AwmPtr_t pawm, int ref_num);

};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_MANGAV2_SCHEDPOL_H_
