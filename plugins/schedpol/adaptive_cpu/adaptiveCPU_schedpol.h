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

#ifndef BBQUE_ADAPTIVE_CPUSCHEDPOL_H_
#define BBQUE_ADAPTIVE_CPUSCHEDPOL_H_

#include <cstdint>
#include <list>
#include <memory>

#include "bbque/configuration_manager.h"
#include "bbque/plugins/plugin.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/scheduler_manager.h"

#define SCHEDULER_POLICY_NAME "adaptiveCPU"

#define MODULE_NAMESPACE SCHEDULER_POLICY_NAMESPACE "." SCHEDULER_POLICY_NAME
#define DEFAULT_NEG_DELTA -5
#define DEFAULT_KP 0.6
#define DEFAULT_KI 0.3
#define DEFAULT_KD 0.1

using bbque::res::RViewToken_t;
using bbque::utils::MetricsCollector;
using bbque::utils::Timer;

// These are the parameters received by the PluginManager on create calls
struct PF_ObjectParams;

namespace bbque {
namespace plugins {

struct AppInfo_t
{
	bbque::app::AppCPtr_t papp;
	bbque::app::AwmPtr_t pawm;
	uint64_t prev_quota;
	uint64_t prev_used;
	int64_t prev_delta;
	uint64_t next_quota;
};

class LoggerIF;

/**
 * @class AdaptiveCPUSchedPol
 *
 * AdaptiveCPU scheduler policy registered as a dynamic C++ plugin.
 */
class AdaptiveCPUSchedPol : public SchedulerPolicyIF
{
public:

	// :::::::::::::::::::::: Static plugin interface :::::::::::::::::::::::::

	/**
	 * @brief Create the adaptive_cpu plugin
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 * @brief Destroy the adaptive_cpu plugin
	 */
	static int32_t Destroy(void *);


	// :::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

	/**
	 * @brief Destructor
	 */
	virtual ~AdaptiveCPUSchedPol();

	/**
	 * @brief Return the name of the policy plugin
	 */
	char const * Name();

	/**
	 * @brief The member function called by the SchedulerManager to perform a
	 * new scheduling / resource allocation
	 */
	ExitCode_t Schedule(System & system, RViewToken_t & status_view);

	/**
	 * @brief Build the resource assignment
	 * @param papp Application pointer
	 */
	ExitCode_t AssignWorkingMode(bbque::app::AppCPtr_t papp);

	/**
	 * @brief Get the profiling information on the CPU usage
	 * @param papp Application pointer
	 * @return A AppInfo_t profiling data structure
	 */
	AppInfo_t InitializeAppInfo(bbque::app::AppCPtr_t papp);

	/**
	 * @brief Entry point of the scheduling function
	 * @param do_func Function object for resource assignment
	 */
	ExitCode_t ScheduleApplications(std::function <ExitCode_t(bbque::app::AppCPtr_t) > do_func);

	/**
	 * @brief Compute the next CPU quota
	 * @param ainfo Profiling information regarding the CPU usage
	 */
	void ComputeNextCPUQuota(AppInfo_t * ainfo);

private:

	/** Configuration manager instance */
	ConfigurationManager & cm;

	/** Resource Accounter instance */
	ResourceAccounter & ra;

	/** System logger instance */
	std::unique_ptr<bu::Logger> logger;

	std::set<BBQUE_RID_TYPE> pe_ids;

	std::vector<uint32_t> sys_ids;

	uint64_t available_cpu;
	uint64_t quota_not_run_apps;

	int64_t neg_delta;
	float kp;
	float ki;
	float kd;

	uint32_t nr_apps;
	uint32_t nr_run_apps;
	uint32_t nr_not_run_apps;

	/**
	 * @brief Constructor
	 *
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 */
	AdaptiveCPUSchedPol();

	/**
	 * @brief Optional initialization member function
	 */
	ExitCode_t _Init();

};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_ADAPTIVE_CPU_SCHEDPOL_H_
