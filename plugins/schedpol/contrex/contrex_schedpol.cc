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

#include "contrex_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "bbque/binding_manager.h"
#include "bbque/modules_factory.h"
#include "bbque/app/working_mode.h"
#include "bbque/pm/power_manager.h"
#include "bbque/res/binder.h"
#include "bbque/utils/logging/logger.h"


#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * ContrexSchedPol::Create(PF_ObjectParams *)
{
	return new ContrexSchedPol();
}

int32_t ContrexSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (ContrexSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * ContrexSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

ContrexSchedPol::ContrexSchedPol():
	cm(ConfigurationManager::GetInstance()),
	ra(ResourceAccounter::GetInstance())
{

	// Logger instance
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);

	if (logger)
		logger->Info("contrex: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
		        FI("contrex: Built new dynamic object [%p]\n"), (void *)this);
}


ContrexSchedPol::ExitCode_t ContrexSchedPol::_Init()
{
	proc_total  = sys->ResourceTotal("sys.cpu.pe");
	logger->Info("Init: <sys.cpu.pe> total = %d", proc_total);

	nr_critical = sys->ApplicationsCount(0);
	logger->Info("Init: critical applications = %d", nr_critical);

	nr_ready   = sys->ApplicationsCount(bbque::app::Application::READY);
	nr_running = sys->ApplicationsCount(bbque::app::Application::RUNNING);
	nr_non_critical = (nr_ready + nr_running) - nr_critical;
	logger->Info("Init: applications: READY=%d, RUNNING=%d",
	             nr_ready, nr_running);

	// Initialize all the CPUs governor
	if (pe_paths.empty())
		InitPowerConfiguration("userspace");

	return SCHED_OK;
}


void ContrexSchedPol::InitProcessorsPath()
{
	br::ResourcePathPtr_t r_path;
	br::ResourcePtrList_t r_list(ra.GetResources("sys.cpu.pe"));
	for (auto & resource_ptr : r_list) {
		if (r_path != nullptr) {
			logger->Debug("Got the resource path object for %s ",
			              resource_ptr->Path()->ToString().c_str());
			pe_paths.push_back(resource_ptr->Path());
		}
	}
}

void ContrexSchedPol::InitPowerConfiguration(std::string const & cpufreq_gov)
{
	InitProcessorsPath();
	PowerManager & pm(PowerManager::GetInstance());
	for (auto & pe_path : pe_paths) {
		logger->Debug("SetPowerConfig: setting governor: %s", cpufreq_gov.c_str());
		pm.SetClockFrequencyGovernor(pe_path, cpufreq_gov);
	}
}


SchedulerPolicyIF::ExitCode_t
ContrexSchedPol::Schedule(
        System & system,
        RViewToken_t & status_view)
{
	SchedulerPolicyIF::ExitCode_t result;

	// Class providing query functions for applications and resources
	sys = &system;
	result = Init();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: initialization failed");
		return result;
	} else
		logger->Debug("Schedule: resource state view = %ld",
		              this->sched_status_view);

	SetPowerConfiguration(0);

	uint32_t proc_left = proc_total, proc_quota = 0;

	if (nr_critical > 0)
		proc_left = ScheduleCritical();

	if (nr_non_critical > 0) {
		logger->Debug("Schedule <sys.cpu.pe>: %d left for %d non-criticals",
		              proc_left, nr_non_critical);
		proc_quota = proc_left / nr_non_critical;
		ScheduleNonCritical(proc_left, proc_quota);
	}

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;

	return SCHED_DONE;
}

void ContrexSchedPol::SetPowerConfiguration(uint32_t perf_state)
{
	PowerManager & pm(PowerManager::GetInstance());
	for (auto & pe_path : pe_paths) {
		logger->Debug("SetPowerConfig: setting pstate: %d", perf_state);
		pm.SetPerformanceState(pe_path, perf_state);
	}
}


inline uint32_t ContrexSchedPol::ScheduleCritical()
{
	uint32_t proc_quota = 0;

	if ((((proc_total / 100) <= nr_critical) &&
	     ((proc_total % 100) == 0)) || (nr_non_critical == 0))
		proc_quota = proc_total / nr_critical;
	else
		proc_quota = 100;

	logger->Debug("Critical: <sys.cpu.pe> assigning: %d ", proc_quota);
	return SchedulePriority(0, proc_total, proc_quota);
}


inline uint32_t ContrexSchedPol::ScheduleNonCritical(
        uint32_t proc_available,
        uint32_t proc_quota)
{
	uint32_t proc_left = proc_available;

	for (bbque::app::AppPrio_t prio = 1;
	     prio <= sys->ApplicationLowestPriority(); ++prio) {
		if (sys->HasApplications(prio)) {
			logger->Debug("Non critical: priority %d ", prio);
			proc_left = SchedulePriority(
			                    prio, proc_available, proc_quota);
		}
	}

	return proc_left;
}

inline uint32_t ContrexSchedPol::SchedulePriority(
        bbque::app::AppPrio_t prio,
        uint32_t proc_available,
        uint32_t proc_quota)
{
	uint32_t proc_left  = proc_available;
	AppsUidMapIt app_it;
	bbque::app::AppCPtr_t papp = sys->GetFirstWithPrio(prio, app_it);
	for (; papp; papp = sys->GetNextWithPrio(prio, app_it)) {
		logger->Debug("Scheduling [%s] with <sys.cpu.pe>: %d",
		              papp->StrId(), proc_quota);
		if (ScheduleApplication(papp, proc_quota) != SCHED_OK)
			continue;
		proc_left = proc_left - proc_quota;
	}
	logger->Debug("SchedulePriority [%d]: <sys.cpu.pe>: %d left",
	              prio, proc_left);
	return proc_left;
}


SchedulerPolicyIF::ExitCode_t ContrexSchedPol::ScheduleApplication(
        bbque::app::AppCPtr_t papp,
        uint32_t proc_quota)
{
	// AWM: build a new working mode
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(
		               papp->WorkingModes().size(), "Run-time", 1, papp);
	} else
		pawm->ClearResourceRequests();

	// AWM: adding resource request
	logger->Debug("Schedule: [%s] adding the processing resource request...",
	              papp->StrId());
	pawm->AddResourceRequest(
	        "sys0.cpu.pe", proc_quota, br::ResourceAssignment::Policy::SEQUENTIAL);

	// Resource (CPU) binding
	logger->Debug("Schedule: [%s] CPU binding...", papp->StrId());
	BindingManager & bdm(BindingManager::GetInstance());
	BindingMap_t & bindings(bdm.GetBindingDomains());
	BBQUE_RID_TYPE bind_cpu_id = 0;
	// Resource binding: getting the first CPU ID option
	if (bindings.find(br::ResourceType::CPU) != bindings.end())
		bind_cpu_id = *(bindings[br::ResourceType::CPU]->r_ids.begin());
	else
		logger->Warn("Schedule: missing CPU binding options (setting to 0)");

	int32_t ref_num = -1;
	ref_num = pawm->BindResource(br::ResourceType::CPU, R_ID_ANY, bind_cpu_id, ref_num);

	// Schedule request
	ApplicationManager & am(ApplicationManager::GetInstance());
	auto ret = am.ScheduleRequest(papp, pawm, sched_status_view, ref_num);
	if (ret != ApplicationManager::AM_SUCCESS) {
		logger->Error("Schedule: scheduling of [%s] failed", papp->StrId());
		return SCHED_ERROR;
	}

	return SCHED_OK;
}


} // namespace plugins

} // namespace bbque
