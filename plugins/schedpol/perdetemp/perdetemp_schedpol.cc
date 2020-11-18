/*
 * Copyright (C) 2016 Politecnico di Milano
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

#include "perdetemp_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <algorithm>

#include "bbque/modules_factory.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "bbque/res/resource_path.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME
#define BD_RESOURCE_PATH_GENERIC "sys.cpu"

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

bool CompareBindingDomainScore(BindingDomain i, BindingDomain j);
bool CompareProcElementScore(ProcElement i, ProcElement j);

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * PerdetempSchedPol::Create(PF_ObjectParams *)
{
	return new PerdetempSchedPol();
}

int32_t PerdetempSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (PerdetempSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * PerdetempSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

PerdetempSchedPol::PerdetempSchedPol():
	conf_manager(ConfigurationManager::GetInstance()),
	res_accounter(ResourceAccounter::GetInstance()),
	power_manager(PowerManager::GetInstance())
{
	// Logger instance
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	logger->Info("Built a new dynamic object[%p]", this);
}

PerdetempSchedPol::~PerdetempSchedPol()
{
}

// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// ::::::::::::::::::::::::::::: Scheduling process :::::::::::::::::::::::::::
// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

SchedulerPolicyIF::ExitCode_t
PerdetempSchedPol::Schedule(System & _system, RViewToken_t & status_view)
{

	logger->Warn(":::::::::: PERDETEMP: Schedule start ::::::::::");
	timer.start();
	float elapsed_time_init = 0.0;
	float elapsed_time_comp = 0.0;
	float elapsed_time_schr = 0.0;

	// Class providing query functions for applications and resources
	system = &_system;

	// Getting a new (empty) system view
	if (Init() != SCHED_OK) {
		logger->Fatal("Schedule: Policy initialization failed");
		return SCHED_ERROR_VIEW;
	}

	elapsed_time_init = timer.getElapsedTimeUs();

	// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
	// :::::::::::::::::::: STEP 1: Make scheduling choice ::::::::::::::::::::
	// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

	// Partition the workload in groups based on priority. Schedule each
	// group separately
	for (AppPrio_t priority = 0;
	     priority <= system->ApplicationLowestPriority();
	     priority ++) {
		// Checking if there are applications at this priority
		if (!system->HasApplications(priority))
			continue;
		/* Checking if there are available resources to be assigned.
		 * This is a fair scheduler; therefore, if I have a workload of
		 * applications @ the same priority, I want to schedule ALL the
		 * applications in the workload. */
		if (available_cpu_bandwidth <=
		    MIN_CPU_PER_APPLICATION * system->ApplicationsCount(priority)) {
			logger->Warn("Resources are not enough to schedule applications"
			             "with prio greater or equal than %d", priority);
			break;
		}

		// Cleaning up previous activities data
		applications.clear();
		// Total CPU quota requested by applications at this priority
		workload_cpu_bandwidth = 0;

		if (ServeApplicationsWithPriority(priority) != SCHED_OK) {
			logger->Fatal("Could not serve applications @ priority %d",
			              priority);
			return SCHED_ERROR;
		}
	}

	elapsed_time_comp = timer.getElapsedTimeUs();

	// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
	// ::::::::::::::::::::::: STEP 2: Apply the choices ::::::::::::::::::::::
	// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

	// At this point, `entities` contains the list of AWMs to be scheduled
	for (SchedEntityPtr_t & sched_entity : entities) {
		// Check application status
		if (SkipScheduling(sched_entity->papp)) {
			logger->Debug("DoScheduling: [%s] skipping status",
			              sched_entity->StrId());
			continue;
		}
		// Scheduling request
		logger->Debug("DoScheduling: [%s] scheduling request",
		              sched_entity->StrId());
		ApplicationManager & am(ApplicationManager::GetInstance());
		auto ret = am.ScheduleRequest(
		                   sched_entity->papp, sched_entity->pawm,
		                   sched_status_view, sched_entity->bind_refn);
		if (ret != ApplicationManager::AM_SUCCESS) {
			logger->Error("DoScheduling: [%s] failed", sched_entity->StrId());
			continue;
		}
		logger->Debug("DoScheduling: [%s] success", sched_entity->StrId());
	}

	// Save the status view
	status_view = sched_status_view;

	elapsed_time_schr = timer.getElapsedTimeUs();

	logger->Warn("Sched time: %.2f us (init %.2f, logic %.2f, "
	             "sched_requests %.2f)",
	             elapsed_time_init + elapsed_time_comp + elapsed_time_schr,
	             elapsed_time_init, elapsed_time_comp, elapsed_time_schr);

	logger->Warn(":::::::::: PERDETEMP: Schedule stop :::::::::::");
	timer.stop();

	return SCHED_DONE;
}

SchedulerPolicyIF::ExitCode_t
PerdetempSchedPol::ServeApplicationsWithPriority(int priority)
{
	logger->Debug("Scheduling applications @ priority %d", priority);
	// Initialize info about both entire workload and single applications
	if (PreProcessQueue(priority) != SCHED_OK)
		return SCHED_ERROR;
	// Schedule applications
	if (BindQueue() != SCHED_OK)
		return SCHED_ERROR;

	return SCHED_OK;
}

void PerdetempSchedPol::UpdateRuntimeProfile(ApplicationInfo &app)
{
	int expected_ggap = 100.0f *
	                    (app.allocated_resources - app.required_resources) /
	                    (float)app.required_resources;

	logger->Warn("[%s] Allocation: [%d]->-[ %d ]<-[%d]. Expected ggap: %d",
	             app.name.c_str(),
	             app.runtime.gap_history.lower_cpu,
	             app.allocated_resources,
	             app.runtime.gap_history.upper_cpu,
	             expected_ggap);

	// Update application info according to allocation
	app.handler->UpdateRuntimePredictions(app.allocated_resources, expected_ggap);
}

SchedulerPolicyIF::ExitCode_t PerdetempSchedPol::BindQueue()
{
	for (ApplicationInfo &app : applications) {
		/* Computing the fair quota of CPU to allocate to the application. If there
		 * is enough bandwidth for the entire workload, required resources can be
		 * assigned to applications. Else, required resources amount is modulated
		 * according to the total CPU demand of the workload */
		if (available_cpu_bandwidth > workload_cpu_bandwidth)
			app.allocated_resources = app.required_resources;
		else
			app.allocated_resources = (int)(
			                                  (float)app.required_resources *
			                                  (float)available_cpu_bandwidth /
			                                  (float)workload_cpu_bandwidth);

		DumpRuntimeProfileStats(app);

		// Update the total required bandwidth according to the allocated one
		workload_cpu_bandwidth  -= app.required_resources;
		available_cpu_bandwidth -= app.allocated_resources;

		// Bind resources to AWM
		if (BindAWM(app) != SCHED_OK) {
			logger->Error("Binding failed! [%s]", app.name.c_str());
			return SCHED_ERROR;
		}

		UpdateRuntimeProfile(app);
	}

	return SCHED_OK;
}

bool PerdetempSchedPol::SkipScheduling(ba::AppCPtr_t const &app)
{
	if (!app->Active() && !app->Blocking()) {
		logger->Debug("Skipping [%s] State = [%s, %s]",
		              app->StrId(),
		              ApplicationStatusIF::StateStr(app->State()),
		              ApplicationStatusIF::SyncStateStr(app->SyncState()));
		return true;
	}

	// Avoid double AWM selection for already RUNNING applications
	if ((app->State() == Application::RUNNING)
	    && app->NextAWM()) {
		logger->Debug("Skipping [%s] AWM %d => No reconfiguration",
		              app->StrId(), app->CurrentAWM()->Id());
		return true;
	}

	// Avoid double AWM selection for already SYNC applications
	if ((app->State() == Application::SYNC) && app->NextAWM()) {
		logger->Debug("Skipping [%s] AWM already assigned [%d]",
		              app->StrId(), app->NextAWM()->Id());
		return true;
	}

	return false;
}

// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// ::::::::::::::::::::: Initialization of schedule process :::::::::::::::::::
// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

SchedulerPolicyIF::ExitCode_t PerdetempSchedPol::_Init()
{
	max_cpu_bandwidth = res_accounter.Total("sys.*.pe");
	available_cpu_bandwidth = max_cpu_bandwidth;
	workload_cpu_bandwidth = 0;

	if (cpus.size() == 0)
		PopulateBindingDomainInfo();

	InitializeBindingDomainInfo();

	entities.clear();

	return SCHED_OK;
}

void PerdetempSchedPol::PopulateBindingDomainInfo()
{
	/* Extracting binding domains information from Resource Accounter
	 * A Binding Domain is a group of PEs that have been grouped together
	 * for a reason: usually, they share a cache, meaning that it would be
	 * good for apps to be isolated in a single domain each if possible
	 *
	 * Note: in the BarbequeRTRM, domains are called "CPUs". Each cpu
	 * is composed by processing elements ("PEs")
	 */
	br::ResourcePtrList_t cpus_list =
	        res_accounter.GetResources(BD_RESOURCE_PATH_GENERIC);

	for (br::ResourcePtr_t &cpu : cpus_list) {
		// The internal CPU representation is called Binding Domain.
		// It will be populated with useful info
		BindingDomain binding_domain;
		// The BarbequeRTRM representation for a CPU -- say, CPU N,
		// is "sys.cpuN". Processing element M of CPU N would be
		// "sys.cpuN.peM". The list of proc elements in CPU N is
		// extracted through the regular expression "sys.CPUN.pe"
		std::string path("sys0.cpu" + std::to_string(cpu->ID()) + ".pe");
		br::ResourcePtrList_t pes_list = res_accounter.GetResources(path);

		// Sanity check: The current CPU must contain at least a PE
		if (pes_list.empty())
			logger->Error("Empty local pes list for node %d",
			              cpu->ID());

		// For each processing element contained in the cpu
		for (br::ResourcePtr_t &proc_element : pes_list) {
			ProcElement pe;
			pe.id = proc_element->ID();
			// e.g. sys.cpu0.pe0
			pe.resource_path = path + std::to_string(pe.id);
			binding_domain.resources.push_back(pe);
		}

		cpus.push_back(binding_domain);
		if (cpus.empty())
			logger->Error("System view initialization error");
	}
}

// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// ::::::::::::::::::::::: Initialization of apps metadata ::::::::::::::::::::
// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

SchedulerPolicyIF::ExitCode_t PerdetempSchedPol::PreProcessQueue(int priority)
{
	ba::AppCPtr_t  papp;
	AppsUidMapIt app_iterator;

	// Init all the applications @ this priority
	papp = system->GetFirstWithPrio(priority, app_iterator);
	for (; papp; papp = system->GetNextWithPrio(priority, app_iterator)) {
		ApplicationInfo app(papp);
		// Computing needed CPU bandwidth based on runtime information
		// collected during runtime (real CPU usage, Goal Gap)
		ComputeRequiredCPU(app);
		applications.push_back(app);
		// Updating workload total bandwidth request
		workload_cpu_bandwidth += app.required_resources;
	}

	logger->Debug("Applications @prio=%d require %d CPU bandwidth in total",
	              priority, workload_cpu_bandwidth);

	return SCHED_OK;
}

void PerdetempSchedPol::DumpRuntimeProfileStats(ApplicationInfo &app)
{
	logger->Debug("[APP %s] Runtime Profile", app.name.c_str());
	logger->Debug("Runtime valid: %s", app.runtime.is_updated ? "yes" : "no");
	logger->Debug("  Goal Gap: %d", app.runtime.ggap_percent);
	logger->Debug("  Lower allocation boundary: [CPU: %d, exp GGAP: %d], ETA %d",
	              app.runtime.gap_history.lower_cpu,
	              app.runtime.gap_history.lower_gap,
	              app.runtime.gap_history.lower_age);
	logger->Debug("  Upper allocation boundary: [CPU: %d, exp GGAP: %d], ETA %d",
	              app.runtime.gap_history.upper_cpu,
	              app.runtime.gap_history.upper_gap,
	              app.runtime.gap_history.upper_age);
	logger->Debug("  Last measured CPU Usage: %d", app.runtime.cpu_usage.curr);
	logger->Debug("  Last allocated CPU Usage: %d", app.runtime.cpu_usage.predicted);
}


void PerdetempSchedPol::ComputeRequiredCPU(ApplicationInfo &app)
{
	// Worst case: have not valid runtime info
	if (app.runtime.is_updated == false) {
		// If application had been already scheduled, for now the allocation
		// will not change
		if (app.runtime.cpu_usage.curr > 0)
			app.required_resources = app.runtime.cpu_usage.curr;
		// Application is totally unknown (maybe, first schedule):
		// assign the application a fair amount of bandwidth
		else
			app.required_resources =
			        available_cpu_bandwidth /
			        system->ApplicationsCount(app.handler->Priority());
		return;
	}

	// Best case: (partially) known application
	// Compute quota according to runtime performance measurements
	logger->Debug("[%s] Valid runtime measurements {GGAP %d, CUSAGE %d}",
	              app.name.c_str(), app.runtime.ggap_percent,
	              app.runtime.cpu_usage.curr);

	float min_gap = (float) app.runtime.gap_history.lower_gap;
	float max_gap = (float) app.runtime.gap_history.upper_gap;
	float min_a = (float) app.runtime.gap_history.lower_cpu;
	float max_a = (float) app.runtime.gap_history.upper_cpu;
	float res_min = 100.0 / (100.0 + min_gap) * min_a;
	float res_max = 100.0 / (100.0 + max_gap) * max_a;
	logger->Debug("Upper Bound: RES %f, GOAL %f", max_a, max_gap);
	logger->Debug("Lower Bound: RES %f, GOAL %f", min_a, min_gap);

	app.required_resources = (int)(std::round(0.5 * (res_min + res_max)));

	/* Guaranteeing a minimal amount of quota to the application. If we get
	 * here, probably the application is trivial
	 * (i.e. it almost doesn't require CPU quota) */
	if (app.required_resources < MIN_CPU_PER_APPLICATION)
		app.required_resources = MIN_CPU_PER_APPLICATION;
}

// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
// ::::::::::::::::::::::::::: Allocation and Binding :::::::::::::::::::::::::
// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

SchedulerPolicyIF::ExitCode_t PerdetempSchedPol::GetBinding(
        ApplicationInfo &app, std::vector<int> &result)
{
	// Sort binding domains score-wise
	if (cpus.size() > 1)
		sort(cpus.begin(), cpus.end(), CompareBindingDomainScore);

	int required_resources = app.allocated_resources;

	for (BindingDomain &binding_domain : cpus) {
		if (GetFreeResources(binding_domain)) {
			if (BookResources(
			            binding_domain,
			            required_resources,
			            app,
			            result) != SCHED_ERROR_INCOMPLETE_ASSIGNMENT)
				return SCHED_OK;
		}
	}

	logger->Error("[%s] Cannot retrieve enough resources [%d percent CPU quota]",
	              app.name.c_str(), required_resources);

	return SCHED_ERROR_INCOMPLETE_ASSIGNMENT;
}

SchedulerPolicyIF::ExitCode_t PerdetempSchedPol::BindAWM(ApplicationInfo &app)
{
	logger->Debug("[%s] required=%d, allocated=%d",
	              app.name.c_str(),
	              app.required_resources,
	              app.allocated_resources);

	// The bitset which will indicate the chosen PEs in the binding domain.
	// PE to assign are computed by GetBinding()
	br::ResourceBitset proc_element_filter;
	std::vector<int> selected_proc_elements;
	if (GetBinding(app, selected_proc_elements) != SCHED_OK) {
		logger->Error("Error: not enough resources to bind");
		return SCHED_ERROR;
	}

	for (const auto &proc_element : selected_proc_elements)
		proc_element_filter.Set(proc_element);

	// Try to bind chosen PEs to application. Note that the binding can happen
	// on multiple binding domains at the same time
	auto resource_path = res_accounter.GetPath("sys.cpu.pe");
	app.bind_reference_number =
	        app.awm->BindResource(
	                br::ResourceType::CPU, R_ID_ANY, R_ID_ANY,
	                app.bind_reference_number,
	                br::ResourceType::PROC_ELEMENT, &proc_element_filter);

	if (app.bind_reference_number < 0) {
		logger->Error("Binding [%s] failed", app.name.c_str());
		return SCHED_ERROR;
	} else
		logger->Info("Binding [%s] succeeded. Ref number is %d",
		             app.name.c_str(), app.bind_reference_number);

	// Enqueue scheduling entity. Apps will be scheduled in bulk later on
	// (see BindQueue)
	SchedEntityPtr_t sched_entity(new SchedEntity_t(app.handler, app.awm, R_ID_ANY, 0));
	sched_entity->bind_refn = app.bind_reference_number;
	entities.push_back(sched_entity);

	return SCHED_OK;
}

// :::::::::::::::::::::: Utility functions to order lists ::::::::::::::::::::
bool CompareBindingDomainScore(BindingDomain i, BindingDomain j)
{
	float total_score_i = 0.0f;
	float total_score_j = 0.0f;

	for (ProcElement &proc_element : i.resources)
		total_score_i += proc_element.status_score;
	for (ProcElement &proc_element : j.resources)
		total_score_j += proc_element.status_score;

	// i will be placed before j if i has an higher score than j
	return (total_score_i > total_score_j);
}

bool CompareProcElementScore (ProcElement i, ProcElement j)
{
	// i will be placed before j if i has higher score than j
	return (i.status_score > j.status_score);
}

void PerdetempSchedPol::InitializeBindingDomainInfo()
{

	/* Here each processing element record will be enriched with additional
	 * information: amount of available resources (i.e. percent CPU quota,
	 * which, usually, is always '100%'), current percent performance
	 * degradation if any, and current percent temperature. Percent temp
	 * is calculated according to the minimum and maximum current
	 * temperatures; therefore, I have to cycle the processing elements
	 * twice: the first time I check for the max and min temperature,
	 * the second time I can compute the percent temperatures.
	 *
	 * Complexity: 2 * O(#proc_elements)
	 */
	uint32_t min_temperature = 0;
	uint32_t max_temperature = 0;
	uint32_t cur_temperature;

	// For each processing element of the system
	for (BindingDomain &binding_domain : cpus) {
		for (ProcElement &proc_element : binding_domain.resources) {
			/* To get the temperature I need the resource path,
			 * which I obtain through the resource accounter by
			 * providing it with the resource path string descriptor
			 * ("sys.cpuN.peM")
			 */
			br::ResourcePathPtr_t resource_path =
			        res_accounter.GetPath(proc_element.resource_path);

			power_manager.GetTemperature(resource_path, cur_temperature);

			// Track min and max temperature
			if (cur_temperature < min_temperature || min_temperature == 0)
				min_temperature = cur_temperature;
			if (cur_temperature > max_temperature)
				max_temperature = cur_temperature;

		}
	}

	// Percent temperature is obtained by linearization, i.e.
	// 100 * (target_temp - min_temp) / (max_temp - min_temp)
	// This way, any target temperature is always in [0-100]
	// Here I prevent null denominators. It does not affect the result.
	if (min_temperature == max_temperature)
		max_temperature ++;

	// This time, I am able to fill all the required info for each PE
	for (BindingDomain &binding_domain : cpus) {
		for (ProcElement &proc_element : binding_domain.resources) {
			br::ResourcePathPtr_t resource_path =
			        res_accounter.GetPath(proc_element.resource_path);

			br::ResourcePtr_t pe_handler =
			        res_accounter.GetResource(resource_path);

			proc_element.available_quota =
			        pe_handler->Total() - pe_handler->Reserved();

			power_manager.GetTemperature(resource_path, cur_temperature);
			// Although it's unlikely, in the last few nanoseconds
			// the recorded temperature may have changed. Checking
			// that min_temperature is still the minimum one, thus
			// avoiding negative temp percentages
			if (cur_temperature < min_temperature)
				min_temperature = cur_temperature;

			// Percent temperature. It is calculated as 100 - the
			// percent temperature, cause the cooler is the better
			float temperature_score = 100.0f -
			                          (100.0f * (cur_temperature - min_temperature)
			                           / (max_temperature - min_temperature));

			// Degradation. It is calculated as 100 - the percent
			// degradation, cause the less degradated is the better
			float degradation_score = 100.0f -
			                          res_accounter.GetResource(
			                                  resource_path)->CurrentDegradationPerc();

			// Availability. The higher the better
			float availability_score = proc_element.available_quota;

			// Status score: f(avail, temp, degradation)
			proc_element.status_score =
			        temperature_score  * TEMP_SCORE_WEIGHT +
			        degradation_score  * DEGR_SCORE_WEIGHT +
			        availability_score * AVAI_SCORE_WEIGHT;
		}

		binding_domain.guest_apps = 0;
		binding_domain.host_app = nullptr;
	}
}

int PerdetempSchedPol::GetFreeResources(BindingDomain &bd)
{
	int amount = 0;
	for (ProcElement &pe : bd.resources)
		amount += pe.available_quota;
	return amount;
}

SchedulerPolicyIF::ExitCode_t PerdetempSchedPol::BookResources(
        BindingDomain &bd, int &amount,
        ApplicationInfo &app, std::vector<int> &result)
{
	int availability = 0;

	// Sort proc elements in the binding domain from emptiest to fullest
	sort(bd.resources.begin(), bd.resources.end(), CompareProcElementScore);

	for (ProcElement &proc_element : bd.resources) {

		availability = std::min(amount, proc_element.available_quota);
		if (availability == 0)
			continue;

		// Registering availability
		proc_element.available_quota -= availability;
		amount -= availability;
		proc_element.status_score -= (availability * AVAI_SCORE_WEIGHT);

		result.push_back(proc_element.id);
		app.awm->AddResourceRequest(proc_element.resource_path, availability);

		if (amount == 0)
			return SCHED_OK;
	}

	return SCHED_ERROR_INCOMPLETE_ASSIGNMENT;
}

} // namespace plugins

} // namespace bbque
