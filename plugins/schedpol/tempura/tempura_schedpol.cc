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

#include "tempura_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "bbque/modules_factory.h"
#include "bbque/power_monitor.h"
#include "bbque/utils/logging/logger.h"

#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "bbque/res/identifier.h"
#include "bbque/res/resource_assignment.h"
#include "bbque/res/resource_path.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

#define BBQUE_TEMPURA_LITTLECPU_FIXED_BUDGET  50
#define BBQUE_TEMPURA_CPU_LOAD_MARGIN         10

namespace br = bbque::res;
namespace bu = bbque::utils;
namespace bw = bbque::pm;
namespace po = boost::program_options;

namespace bbque {
namespace plugins {


// ================= Metrics collection ********************** //

#ifdef CONFIG_BBQUE_SCHED_PROFILING

#define SAMPLE_METRIC(NAME, DESC)\
	{SCHEDULER_MANAGER_NAMESPACE ".tempura." NAME, DESC, \
		bu::MetricsCollector::SAMPLE, 0, NULL, 0         \
	}


MetricsCollector::MetricsCollection_t
TempuraSchedPol::coll_metrics[TEMPURA_METRICS_COUNT] = {
	SAMPLE_METRIC("init",    "Time to complete initializaton [ms]"),
	SAMPLE_METRIC("budgets", "Time to compute budgets [ms]"),
	SAMPLE_METRIC("assign",  "Time to define resource assignments [ms]"),
	SAMPLE_METRIC("sched",   "Time to send all the scheduling requests [ms]")
};

/** Reset the timer used to evaluate metrics */
#define RESET_TIMING(TIMER) TIMER.start();

/** Acquire a new completion time sample */
#define GET_TIMING(METRICS, INDEX, TIMER) \
	mc.AddSample(METRICS[INDEX].mh, TIMER.getElapsedTimeMs());

/* Get a new sample for the metrics */
#define GET_SAMPLE(METRICS, INDEX, VALUE) \
	mc.AddSample(METRICS[INDEX].mh, VALUE);

#else // Scheduling Profiling support disabled

#define SAMPLE_METRIC(NAME, DESC)
#define RESET_TIMING(TIMER)
#define GET_TIMING(METRICS, INDEX, TIMER)
#define GET_SAMPLE(METRICS, INDEX, VALUE)

#endif

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * TempuraSchedPol::Create(PF_ObjectParams *)
{
	return new TempuraSchedPol();
}

int32_t TempuraSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (TempuraSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * TempuraSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

TempuraSchedPol::TempuraSchedPol() :
    cm(ConfigurationManager::GetInstance()),
    ra(ResourceAccounter::GetInstance()),
    bdm(BindingManager::GetInstance()),
    mm(bw::ModelManager::GetInstance())
#ifdef CONFIG_BBQUE_SCHED_PROFILING
    ,
    mc(bu::MetricsCollector::GetInstance())
#endif
#ifdef CONFIG_BBQUE_PM_BATTERY
    ,
    bm(BatteryManager::GetInstance())
#endif

{
	// Logger instance
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	if (logger)
		logger->Debug("tempura: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
			FI("tempura: Built new dynamic object [%p]\n"), (void *)this);

#ifdef CONFIG_BBQUE_SCHED_PROFILING
	// Register all the metrics to collect
	mc.Register(coll_metrics, TEMPURA_METRICS_COUNT);
#endif

#ifdef CONFIG_BBQUE_PM_BATTERY
	pbatt = bm.GetBattery();
	if (pbatt == nullptr)
		logger->Error("No battery available. Cannot perform energy budgeting");
#endif

	// System power-thermal model
	pmodel_sys = mm.GetSystemModel();
	if (pmodel_sys)
		logger->Debug("Init: System model: [%s]",
			pmodel_sys->GetID().c_str());
	else
		logger->Debug("Init: No system model");
}

TempuraSchedPol::~TempuraSchedPol()
{
	budgets.clear();
	entities.clear();
}

/*************************************************************
 * Initialization
 ************************************************************/

SchedulerPolicyIF::ExitCode_t TempuraSchedPol::_Init()
{
	ExitCode_t result = SCHED_OK;
	PowerMonitor & wm(PowerMonitor::GetInstance());

	// Application slots
	if (sys->HasApplications(ApplicationStatusIF::READY)) {
		slots = GetSlots();
		logger->Debug("Init: slots for partitioning = %ld", slots);
	}

	// Thermal threshold
	if (crit_temp == 0)
		crit_temp = wm.GetThermalThreshold();

	// System power budget
	sys_power_budget = wm.GetSysPowerBudget();
	if (sys_power_budget > 0) {
		tot_resource_power_budget =
			pmodel_sys->GetResourcePowerFromSystem(
							sys_power_budget, cpufreq_gov);
		logger->Debug("Init: power budget [System: %d mW] => "
			"[Resource: %d mW] freqgov: %s",
			sys_power_budget, tot_resource_power_budget,
			cpufreq_gov.c_str());
	}

	// Resource budgets (power and resource amounts)
	if (!budgets.empty())
		logger->Debug("Init: power budgets already initialized");
	else {
		result = InitBudgets();
		if (result != SCHED_OK) {
			logger->Fatal("Init: power budgets initialization failed");
			return result;
		}
	}

	return result;
}

SchedulerPolicyIF::ExitCode_t
TempuraSchedPol::InitBudgets()
{
	// Binding type (CPU, GPU,...))
	BindingMap_t & bindings(bdm.GetBindingDomains());
	for (auto & bd_entry : bindings) {
		BindingInfo_t const & bd_info(*(bd_entry.second));

		// Resource path e.g., "sys0.cpu[0..n].XX"
		for (br::ResourcePtr_t const & rsrc : bd_info.resources) {
			br::ResourcePathPtr_t r_path(rsrc->Path());
			r_path->AppendString("pe");

			// Add a budget info object
			br::ResourcePtrList_t r_list(ra.GetResources(r_path));
			budgets.emplace(r_path, std::make_shared<BudgetInfo>(r_path, r_list));
			logger->Debug("Init: budgeting on '%s' [Model: %s]",
			              r_path->ToString().c_str(),
			              budgets[r_path]->model.c_str());

			// CPU frequency setting
			InitCPUFreqGovernor(r_path);
			logger->Debug("Init: CPU frequency governor set [%s]",
			              cpufreq_gov.c_str());
		}
	}

	return SCHED_OK;
}

void TempuraSchedPol::InitCPUFreqGovernor(br::ResourcePathPtr_t r_path)
{
	PowerManager & pm(PowerManager::GetInstance());
	for (auto & rsrc : budgets[r_path]->r_list) {
		br::ResourcePathPtr_t r_path_exact(rsrc->Path());
		pm.SetClockFrequencyGovernor(r_path_exact, cpufreq_gov);
		//		std::string cpu_gov;
		//		pm.GetClockFrequencyGovernor(r_path_exact, cpu_gov);
		//		logger->Debug("<%s> cpufreq governor: %s",
		//				r_path_exact->ToString().c_str(), cpu_gov.c_str());
	}
}

/*************************************************************
 * Resource allocation
 ************************************************************/

SchedulerPolicyIF::ExitCode_t
TempuraSchedPol::Schedule(
			  System & system,
			  RViewToken_t & status_view)
{
	SchedulerPolicyIF::ExitCode_t result;
	sys = &system;

	RESET_TIMING(timer);
	result = Init();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: policy initialization failed");
		goto error;
	}
	GET_TIMING(coll_metrics, TEMPURA_INIT, timer);

	RESET_TIMING(timer);
	result = ComputeBudgets();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: budgets cannot be computed");
		goto error;
	}
	GET_TIMING(coll_metrics, TEMPURA_COMP_BUDGETS, timer);

	RESET_TIMING(timer);
	result = DoResourcePartitioning();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: resource partitioning failed");
		goto error;
	}
	GET_TIMING(coll_metrics, TEMPURA_RESOURCE_PARTITION, timer);

	RESET_TIMING(timer);
	result = DoScheduling();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: scheduling failed");
		goto error;
	}
	GET_TIMING(coll_metrics, TEMPURA_DO_SCHEDULE, timer);

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	return SCHED_DONE;

error:
	ra.PutView(status_view);
	return SCHED_ERROR;

}

SchedulerPolicyIF::ExitCode_t TempuraSchedPol::ComputeBudgets()
{

	for (auto & entry : budgets) {
		br::ResourcePathPtr_t const  & r_path(entry.first);
		std::shared_ptr<BudgetInfo> & budget_ptr(entry.second);
		//	bw::ModelPtr_t pmodel(mm.GetModel("ARM Cortex A15"));
		bw::ModelPtr_t pmodel(mm.GetModel(budget_ptr->model));
		logger->Debug("Budget: <%s> using power-thermal model '%s'",
			r_path->ToString().c_str(), pmodel->GetID().c_str());

		budget_ptr->power = GetPowerBudget(budget_ptr, pmodel);
		budget_ptr->prev  = budget_ptr->curr;
		budget_ptr->curr  = GetResourceBudget(r_path, pmodel);
	}

	return SCHED_OK;
}

inline uint32_t TempuraSchedPol::GetPowerBudget(
						std::shared_ptr<BudgetInfo> budget_ptr,
						ModelPtr_t pmodel)
{
	uint32_t energy_pwr_budget = 0;
	uint32_t temp_pwr_budget   = 0;
	uint32_t curr_power = 0;
	uint32_t curr_temp  = 0;
	uint32_t curr_load  = 0;
	logger->Debug("PowerBudget: resource path=<%s>",
		budget_ptr->r_path->ToString().c_str());

	// Current status
	for (auto & rsrc : budget_ptr->r_list) {
		curr_temp += rsrc->GetPowerInfo(
						PowerManager::InfoType::TEMPERATURE, br::Resource::MEAN);
		curr_load += rsrc->GetPowerInfo(
						PowerManager::InfoType::LOAD, br::Resource::MEAN);
#ifdef CONFIG_TARGET_ODROID_XU
		curr_power = rsrc->GetPowerInfo(PowerManager::InfoType::POWER);
#endif
	}
	curr_load /= budget_ptr->r_list.size();
	curr_temp /= budget_ptr->r_list.size();
	if (curr_temp > 1e3)
		curr_temp /= 1e3;

	logger->Debug("PowerBudget: <%s> TEMP_crit=[%d]C  TEMP_curr=[%3d]C",
		budget_ptr->r_path->ToString().c_str(), crit_temp, curr_temp);
	logger->Debug("PowerBudget: <%s> prev_budget=%d  LOAD_curr=[%3d]",
		budget_ptr->r_path->ToString().c_str(), budget_ptr->curr, curr_load);

	// Thermal threshold correction
	uint32_t new_crit_temp = crit_temp;
	if ((sched_count > 0) &&
	(std::abs<int>(budget_ptr->curr - curr_load) < BBQUE_TEMPURA_CPU_LOAD_MARGIN)) {
		new_crit_temp += (crit_temp - curr_temp);
		logger->Debug("PowerBudget: <%s> critical temperature corrected"
			" to T_crit=[%3d]C",
			budget_ptr->r_path->ToString().c_str(), new_crit_temp);
	}
	logger->Debug("PowerBudget: <%s> T_crit=[%3d]C, T_curr=[%3d]C, P=[%5.0f]mW",
		budget_ptr->r_path->ToString().c_str(),
		new_crit_temp, curr_temp, curr_power);

	// Power budget from thermal constraints
	if (new_crit_temp < 1e3)
		new_crit_temp *= 1e3;


	temp_pwr_budget = pmodel->GetPowerFromTemperature(new_crit_temp, cpufreq_gov);
	if (tot_resource_power_budget < 1) {
		logger->Debug("PowerBudget: <%s> P(T)=[%d]mW, P(E)=[-]",
			budget_ptr->r_path->ToString().c_str(), temp_pwr_budget);
		return temp_pwr_budget;
	}

	// Power budget from energy constraints
#ifdef CONFIG_BBQUE_PM_BATTERY
	if (pbatt && (pbatt->IsDischarging() || pbatt->GetChargePerc() < 100)) {
		logger->Debug("Budget: System battery full charged and power plugged");
		energy_pwr_budget =
			pmodel->GetPowerFromSystemBudget(tot_resource_power_budget);
	}
#endif
	logger->Debug("Budget: <%s> P(T)=[%d]mW, P(E)=[%d]mW",
		budget_ptr->r_path->ToString().c_str(),
		temp_pwr_budget, energy_pwr_budget);

	return std::min<uint32_t>(temp_pwr_budget, energy_pwr_budget);
}

inline int64_t TempuraSchedPol::GetResourceBudget(
						  br::ResourcePathPtr_t const & r_path,
						  bw::ModelPtr_t pmodel)
{
	uint64_t resource_total  = sys->ResourceTotal(r_path);
	uint64_t resource_budget = 0;

#ifdef CONFIG_TARGET_ODROID_XU
	if (!pmodel->GetID().compare("ARM Cortex A15")) {
		resource_budget = BBQUE_TEMPURA_LITTLECPU_FIXED_BUDGET;
		logger->Warn("ARM Cortex A7: CPU budget = %d",
			BBQUE_TEMPURA_LITTLECPU_FIXED_BUDGET);
	}
	else {
		resource_budget = pmodel->GetResourceFromPower(
							budgets[r_path]->power,
							resource_total);
	}
#else
	resource_budget = pmodel->GetResourceFromPower(
						budgets[r_path]->power, resource_total, cpufreq_gov);
#endif

	budgets[r_path]->curr = std::min<uint32_t>(resource_budget, resource_total);
	logger->Debug("Budget: <%s> P=[%4lu]mW, R=[%lu]",
		r_path->ToString().c_str(),
		budgets[r_path]->power, budgets[r_path]->curr);
	return budgets[r_path]->curr;
}

SchedulerPolicyIF::ExitCode_t TempuraSchedPol::DoResourcePartitioning()
{
	ba::AppCPtr_t papp;
	AppsUidMapIt app_it;

	// Ready applications
	papp = sys->GetFirstReady(app_it);
	for (; papp; papp = sys->GetNextReady(app_it)) {
		AssignWorkingMode(papp);
	}

	// Running applications
	papp = sys->GetFirstRunning(app_it);
	for (; papp; papp = sys->GetNextRunning(app_it)) {
		papp->CurrentAWM()->ClearResourceRequests();
		AssignWorkingMode(papp);
	}

	return SCHED_OK;
}

inline bool TempuraSchedPol::CheckSkip(ba::AppCPtr_t const & papp)
{
	if (!papp->Active() && !papp->Blocking()) {
		logger->Debug("Skipping [%s] State = [%s, %s]",
			papp->StrId(),
			ApplicationStatusIF::StateStr(papp->State()),
			ApplicationStatusIF::SyncStateStr(papp->SyncState()));
		return true;
	}

	// Avoid double AWM selection for already RUNNING applications
	if ((papp->State() == Application::RUNNING) && papp->NextAWM()) {
		logger->Debug("Skipping [%s] AWM %d => No reconfiguration",
			papp->StrId(), papp->CurrentAWM()->Id());
		return true;
	}

	// Avoid double AWM selection for already SYNC applications
	if ((papp->State() == Application::SYNC) && papp->NextAWM()) {
		logger->Debug("Skipping [%s] AWM already assigned [%d]",
			papp->StrId(), papp->NextAWM()->Id());
		return true;
	}

	return false;
}

SchedulerPolicyIF::ExitCode_t
TempuraSchedPol::AssignWorkingMode(ba::AppCPtr_t papp)
{
	uint64_t resource_slot;
	uint64_t resource_amount;

	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(
			papp->WorkingModes().size(), "Run-time", 1, papp);
	}

	// Resource assignment (from each binding domain)
	for (auto & entry : budgets) {
		br::ResourcePathPtr_t const  & r_path(entry.first);
		std::shared_ptr<BudgetInfo> & budget(entry.second);
		logger->Debug("Assign: <%s> R_budget = %lu",
			r_path->ToString().c_str(), budget->curr);

		// Slots to allocate for this resource binding domain
		resource_slot  = budget->curr / slots;
		logger->Debug("Assign: [%s] rslots of [%s] assigned = %4d",
			papp->StrId(), r_path->ToString().c_str(), resource_slot);

		// Resource amount to allocate
		resource_amount =
			(sys->ApplicationLowestPriority() - papp->Priority() + 1) *
			resource_slot;
		logger->Info("Assign: [%s] amount of [%s] assigned = %4d",
			papp->StrId(), r_path->ToString().c_str(), resource_amount);
		if (resource_amount > 0)
			pawm->AddResourceRequest(
						r_path->ToString(), resource_amount,
						br::ResourceAssignment::Policy::BALANCED);
	}

	// Enqueue scheduling entity
	SchedEntityPtr_t psched = std::make_shared<SchedEntity_t>(
		papp, pawm, R_ID_ANY, 0);
	entities.push_back(psched);

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t TempuraSchedPol::DoScheduling()
{
	SchedulerPolicyIF::ExitCode_t result;
	logger->Debug("DoScheduling: START");

	for (SchedEntityPtr_t & psched : entities) {
		// Bind the assigned resources and try to schedule
		result = DoBinding(psched);
		if (result != SCHED_OK) {
			logger->Error("DoScheduling: [%s] skipping scheduling",
				psched->StrId());
			continue;
		}

		// Check application status
		if (CheckSkip(psched->papp)) {
			logger->Debug("DoScheduling: [%s] skipping status",
				psched->StrId());
			continue;
		}

		// Scheduling request
		logger->Debug("DoScheduling: [%s] scheduling request...",
			psched->StrId());
		ApplicationManager & am(ApplicationManager::GetInstance());
		auto ret = am.ScheduleRequest(
					psched->papp, psched->pawm,
					sched_status_view, psched->bind_refn);
		if (ret != ApplicationManager::AM_SUCCESS) {
			logger->Error("DoScheduling: [%s] failed", psched->StrId());
			continue;
		}
		psched->papp->SetValue(1.0);
		logger->Debug("DoScheduling: [%s] success", psched->StrId());
	}
	logger->Debug("DoScheduling: STOP");
	entities.clear();

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t TempuraSchedPol::DoBinding(
							 SchedEntityPtr_t psched)
{
	logger->Debug("DoBinding: START");
	size_t ref_n = -1;

	BindingMap_t & bindings(bdm.GetBindingDomains());
	for (auto & bd_entry : bindings) {
		BindingInfo_t const & bd_info(*(bd_entry.second));
		br::ResourceType bd_type = bd_entry.first;
		// CPU, GPU level binding
		// Resource path e.g., "sys0.cpu[0..n].XX"
		if (bd_info.resources.empty()) continue;
		for (br::ResourcePtr_t const & rsrc : bd_info.resources) {
			BBQUE_RID_TYPE bd_id = rsrc->ID();
			logger->Debug("DoBinding: [%s] binding to %s%d",
				psched->StrId(),
				br::GetResourceTypeString(bd_type),
				bd_id);

			ref_n = psched->pawm->BindResource(bd_type, bd_id, bd_id, ref_n);
			logger->Debug("DoBinding: [%s] reference number %ld",
				psched->StrId(), ref_n);
		}

		// Set scheduled binding reference number
		psched->bind_refn = ref_n;
	}

	logger->Debug("DoBinding: STOP");
	return SCHED_OK;
}


} // namespace plugins

} // namespace bbque
