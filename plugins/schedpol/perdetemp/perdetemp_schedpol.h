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

#ifndef BBQUE_PERDETEMP_SCHEDPOL_H_
#define BBQUE_PERDETEMP_SCHEDPOL_H_

#include <cstdint>
#include <list>
#include <memory>

#include "bbque/configuration_manager.h"
#include "bbque/plugins/plugin.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/scheduler_manager.h"
#include <algorithm>

#define SCHEDULER_POLICY_NAME "perdetemp"
#define MIN_CPU_PER_APPLICATION 5
#define PERDETEMP_GGAP_MAX 300
#define PERDETEMP_GGAP_MIN -PERDETEMP_GGAP_MAX
#define PERDETEMP_MAX_SAMPLE_AGE 1

#define DEGR_SCORE_WEIGHT 0.2
#define TEMP_SCORE_WEIGHT 0.5
#define AVAI_SCORE_WEIGHT 0.3

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
 * @brief Local storage for application information
 */
struct ApplicationInfo {
	/* Name of the application*/
	std::string name;
	/* Reference to the application*/
	ba::AppCPtr_t handler;
	/* The AWM that will be scheduled*/
	ba::AwmPtr_t awm;
	/* CPU bandwidth required by the application */
	int required_resources = -1;
	/* CPU bandwidth that WILL be allocated to the application. In fact, the
	 * total available bandwidth could be not enough to serve each app */
	int allocated_resources = 0;
	/* Runtime Profiling data*/
	app::RuntimeProfiling_t runtime;
	/* ID of the chosen binding */
	int32_t bind_reference_number = -1;

	ApplicationInfo(ba::AppCPtr_t papp)
	{

		std::string app_name = "S-runtime::" + std::to_string(papp->Pid());
		// Steaks does not need recipes; instead, it builds AWMs on the fly
		// The ID is progressive; hance, the new ID will be equal to the size
		// of AMWs list. The value (1) will not be used.
		ba::AwmPtr_t _awm( // WorkingMode(ID, Name, Value));
		        new ba::WorkingMode(papp->WorkingModes().size(), app_name, 1));

		// Handler to the application object in bbque app manager
		handler = papp;
		name = app_name;
		awm = _awm;
		awm->SetOwner(papp);
		runtime = papp->GetRuntimeProfile(true);

		if (runtime.is_updated == true) {
			runtime.ggap_percent =
			        std::max(PERDETEMP_GGAP_MIN,
			                 std::min(runtime.ggap_percent, PERDETEMP_GGAP_MAX));

			bool lower_outdated = (runtime.gap_history.lower_age == -1 ||
			                       runtime.gap_history.lower_age > PERDETEMP_MAX_SAMPLE_AGE);
			bool upper_outdated = (runtime.gap_history.upper_age == -1 ||
			                       runtime.gap_history.upper_age > PERDETEMP_MAX_SAMPLE_AGE);
			// Check if allocation boundaries are invalidated.
			if (lower_outdated) {
				// In this case, the lower boundary was invalidated. Let's
				// compute a brand new bound starting from the current ggap
				runtime.gap_history.lower_gap = -1;
				runtime.gap_history.lower_cpu =
				        (float)runtime.gap_history.upper_cpu
				        / (1.0 + (float)runtime.gap_history.upper_gap / 100.0);
			} else if (upper_outdated) {
				// In this case, the upper boundary was invalidated. Let's
				// compute a brand new bound starting from the current ggap
				runtime.gap_history.upper_gap = -1;
				runtime.gap_history.upper_cpu =
				        (float)runtime.gap_history.lower_cpu
				        / (1.0 + (float)runtime.gap_history.lower_gap / 100.0);
			}
		}
	}
};

/**
 * @brief Processing element descriptor
 *
 * A processing element is denoted by an ID and the CPU quota that
 * can be allocated to the applications
 */
struct ProcElement {
	// Proc element id
	int id;
	// CPU bandwidth availability
	int available_quota;
	// Score : f(availability, temperature, degradation)
	float status_score;
	// Resource path handler (string version)
	std::string resource_path;
};

/**
 * @brief Local node descriptor
 *
 * A local node is a group of processing elements that share a portion
 * of the cache hierarchy. For example, the PEs of a single core
 */
struct BindingDomain {
	std::vector<ProcElement> resources;
	ba::AppCPtr_t host_app;
	int guest_apps = 0;
};

/**
 * @class PerdetempSchedPol
 *
 * Steaks scheduler policy registered as a dynamic C++ plugin.
 */
class PerdetempSchedPol: public SchedulerPolicyIF
{

public:

	// :::::::::::::::::::::: Static plugin interface :::::::::::::::::::::::::

	/**
	 * @brief Create the steaks plugin
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 * @brief Destroy the steaks plugin
	 */
	static int32_t Destroy(void *);

	// :::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

	/**
	 * @brief Destructor
	 */
	virtual ~PerdetempSchedPol();

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

	/* Total PEs in the managed device, each providing 100% CPU bandwidth */
	int max_cpu_bandwidth = 0;

	/* Total cores required by the current workload in the ideal case. It could
	 * be even greater than `max_cpu_bandwidth` */
	int workload_cpu_bandwidth = 0;

	/* Unallocated bandwidth at a given time */
	int available_cpu_bandwidth = 0;

	/* Representation of the Managed Device. It is a list of binding domains */
	std::vector<BindingDomain> cpus;

	/* List of application to be scheduled */
	std::vector<ApplicationInfo> applications;


	ConfigurationManager & conf_manager;

	ResourceAccounter & res_accounter;

	PowerManager & power_manager;

	std::unique_ptr<bu::Logger> logger;

	System * system;

	/**
	 * @brief Constructor
	 *
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 */
	PerdetempSchedPol();

	/**
	 * @brief Optional initialization member function
	 */
	SchedulerPolicyIF::ExitCode_t _Init();

	ExitCode_t ServeApplicationsWithPriority(int priority);

	ExitCode_t PreProcessQueue(int priority);

	ExitCode_t BindQueue();

	ExitCode_t GetBinding(
	        ApplicationInfo &app, std::vector<int> &result);

	ExitCode_t BindAWM(ApplicationInfo &app);

	void ComputeRequiredCPU(ApplicationInfo &app);

	void PopulateBindingDomainInfo();

	void UpdateRuntimeProfile(ApplicationInfo &app);

	void SetAllocation(ApplicationInfo &app);

	void InitializeBindingDomainInfo();

	bool SkipScheduling(ba::AppCPtr_t const &app);

	void DumpRuntimeProfileStats(ApplicationInfo &app);

	int GetFreeResources(BindingDomain &bd);

	ExitCode_t BookResources(
	        BindingDomain &bd,
	        int &amount,
	        ApplicationInfo &app,
	        std::vector<int> &result);
};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_PERDETEMP_SCHEDPOL_H_
