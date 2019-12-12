/*
 * Copyright (C) 2015  Politecnico di Milano
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

#ifndef BBQUE_TEMPURA_SCHEDPOL_H_
#define BBQUE_TEMPURA_SCHEDPOL_H_

#include <cstdint>
#include <list>
#include <map>
#include <memory>

#include "bbque/configuration_manager.h"
#include "bbque/plugins/plugin.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/pm/battery_manager.h"
#include "bbque/pm/model_manager.h"
#include "bbque/scheduler_manager.h"
#include "bbque/resource_manager.h"
#include "bbque/utils/metrics_collector.h"

#define SCHEDULER_POLICY_NAME "tempura"

#define MODULE_NAMESPACE SCHEDULER_POLICY_NAMESPACE "." SCHEDULER_POLICY_NAME

using bbque::pm::ModelManager;
using bbque::pm::ModelPtr_t;
using bbque::res::RViewToken_t;
using bbque::utils::MetricsCollector;
using bbque::utils::Timer;

namespace br = bbque::res;

// These are the parameters received by the PluginManager on create calls
struct PF_ObjectParams;

namespace bbque
{
namespace plugins
{

class LoggerIF;

/**
 * @class TempuraSchedPol
 *
 * Tempura scheduler policy registered as a dynamic C++ plugin.
 */
class TempuraSchedPol: public SchedulerPolicyIF
{

public:

	// :::::::::::::::::::::: Static plugin interface :::::::::::::::::::::::::

	/**
	 * @brief Create the tempura plugin
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 * @brief Destroy the tempura plugin
	 */
	static int32_t Destroy(void *);


	// :::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

	/**
	 * @brief Destructor
	 */
	virtual ~TempuraSchedPol();

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

	BindingManager & bdm;

	/** P/T model manager */
	ModelManager & mm;


#ifdef CONFIG_BBQUE_SCHED_PROFILING
	enum SchedPolMetrics_t {
		// :::: Timing metrics
		TEMPURA_INIT,
		TEMPURA_COMP_BUDGETS,
		TEMPURA_RESOURCE_PARTITION,
		TEMPURA_DO_SCHEDULE,

		TEMPURA_METRICS_COUNT
	};

	static bbque::utils::MetricsCollector::MetricsCollection_t
	coll_metrics[TEMPURA_METRICS_COUNT];

	bu::MetricsCollector & mc;
#endif

#ifdef CONFIG_BBQUE_PM_BATTERY
	/** Battery manager instance  */
	BatteryManager & bm;

	/** Battery object instance	 */
	BatteryPtr_t pbatt;
#endif

	/** System logger instance */
	std::unique_ptr<bu::Logger> logger;


	/** String for requiring new resource status views */

	uint32_t sched_count = 0;

	uint32_t slots;

	uint32_t crit_temp = 0;

	/** System power budget */
	int32_t sys_power_budget = 0;

	/** Resource power consumption derived from the system power budget */
	uint32_t tot_resource_power_budget = 0;


	/** Power-thermal model for the entire system  */
	bw::SystemModelPtr_t pmodel_sys;


	class BudgetInfo
	{
	public:
		BudgetInfo(br::ResourcePathPtr_t _path, br::ResourcePtrList_t _resources):
			r_path(_path), r_list(_resources)
		{
			if (!r_list.empty())
				model = r_list.front()->Model();
		}
		br::ResourcePathPtr_t r_path;
		br::ResourcePtrList_t r_list;
		std::string model;
		uint32_t prev;
		uint32_t curr;
		uint32_t power;
	};

	std::map<br::ResourcePathPtr_t, std::shared_ptr<BudgetInfo>> budgets;


	/** Default CPU frequency governor that the policy set */
	std::string cpufreq_gov = BBQUE_PM_DEFAULT_CPUFREQ_GOVERNOR;


	/** An High-Resolution timer */
	Timer timer;

	/**
	 * @brief Constructor
	 *
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 */
	TempuraSchedPol();

	/**
	 * @brief Policy initialization
	 */
	ExitCode_t _Init();

	/**
	 * @brief Initialize the power and resource budgets
	 */
	ExitCode_t InitBudgets();

	/**
	 * @brief CPU frequency governors setting
	 */
	void InitCPUFreqGovernor(br::ResourcePathPtr_t r_pat);

	/**
	 * @brief Compute power and resource budgets
	 *
	 * This step is performed according to the constraints on the critical
	 * termal threshold and (optionally) the energy budget
	 */
	ExitCode_t ComputeBudgets();

	/**
	 * @brief Define the power budget of a specific resource to allocate
	 * (power capping)
	 *
	 * The function computes the power budgets coming from themal and energy
	 * budget constraints.
	 *
	 * @param rp The resource path
	 * @param pmodel The resource power-thermal model
	 *
	 * @return The power value to cap (in milliwatts)
	 */
	uint32_t GetPowerBudget(
	        std::shared_ptr<BudgetInfo> budget_ptr,
	        ModelPtr_t pmodel);

	/**
	 * @brief Define the resource budget to allocate according to the power
	 * budget
	 *
	 * The function is in charge of computing the amount of resource to
	 * allocate, for instance by capping the CPU total bandwith and/or setting
	 * the CPU cores frequencies.
	 *
	 * @param rp The resource path
	 * @param pmodel The resource powert-thermal model
	 *
	 * @return The total amount of allocatable resource
	 */
	int64_t GetResourceBudget(
	        br::ResourcePathPtr_t const & rp, ModelPtr_t pmodel);

	/**
	 * @brief Perform the resource partitioning among active applications
	 *
	 * @return SCHED_OK for success
	 */
	ExitCode_t DoResourcePartitioning();

	/**
	 * @brief Build the working mode of assigned resources
	 *
	 * @param papp Pointer to the application to schedule
	 *
	 * @return SCHED_OK for success
	 */
	ExitCode_t AssignWorkingMode(ba::AppCPtr_t papp);

	/**
	 * @brief Check if the application does not need to be re-scheduled
	 *
	 * @param papp The pointer to the application descriptor
	 *
	 * @return true if the application must be skipped, false otherwise
	 */
	bool CheckSkip(ba::AppCPtr_t const & papp);

	/**
	 * @brief Do resource binding and send scheduling requests
	 *
	 * @return SCHED_OK for success
	 */
	ExitCode_t DoScheduling();

	/**
	 * @brief Bind the working mode to platform resources
	 *
	 * @param papp Pointer to the scheduling entity (application and working
	 * mode)
	 *
	 * @return SCHED_OK for success
	 */
	ExitCode_t DoBinding(SchedEntityPtr_t psched);
};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_TEMPURA_SCHEDPOL_H_
