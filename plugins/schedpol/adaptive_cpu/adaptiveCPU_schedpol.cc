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

#include "adaptiveCPU_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdio.h>
#include <fstream>
#include "bbque/modules_factory.h"
#include "bbque/utils/logging/logger.h"

#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"

#include "bbque/binding_manager.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

#define INITIAL_DEFAULT_QUOTA 150
#define MIN_ASSIGNABLE_QUOTA   10
#define ADMISSIBLE_DELTA       10
#define THRESHOLD               1

using namespace std::placeholders;

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque {
namespace plugins {

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * AdaptiveCPUSchedPol::Create(PF_ObjectParams *)
{
	return new AdaptiveCPUSchedPol();
}

int32_t AdaptiveCPUSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (AdaptiveCPUSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * AdaptiveCPUSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

AdaptiveCPUSchedPol::AdaptiveCPUSchedPol() :
    cm(ConfigurationManager::GetInstance()),
    ra(ResourceAccounter::GetInstance())
{
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	if (logger)
		logger->Info("adaptive_cpu: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
			FI("adaptive_cpu: Built new dynamic object [%p]\n"), (void *)this);

	po::options_description opts_desc("AdaptiveCPUSchedPol Parameters Options");
	opts_desc.add_options()
		("AdaptiveCPUSchedPol.neg_delta",
		po::value<int64_t>(
		&this->neg_delta)->default_value(DEFAULT_NEG_DELTA),
		"Value of neg_delta");

	opts_desc.add_options()
		("AdaptiveCPUSchedPol.kp",
		po::value<float>(
		&this->kp)->default_value(DEFAULT_KP),
		"Value of coefficient kp");

	opts_desc.add_options()
		("AdaptiveCPUSchedPol.ki",
		po::value<float>(
		&this->ki)->default_value(DEFAULT_KI),
		"Value of coefficient ki");

	opts_desc.add_options()
		("AdaptiveCPUSchedPol.kd",
		po::value<float>(
		&this->kd)->default_value(DEFAULT_KD),
		"Value of coefficient kd");
	po::variables_map opts_vm;
	cm.ParseConfigurationFile(opts_desc, opts_vm);

	logger->Info("Running with neg_delta=%d, kp=%f, ki=%f, kd=%f",
		neg_delta, kp, ki, kd);
}

AdaptiveCPUSchedPol::~AdaptiveCPUSchedPol() { }

SchedulerPolicyIF::ExitCode_t AdaptiveCPUSchedPol::_Init()
{
	// Processing elements IDs
	auto & resource_types = sys->ResourceTypes();
	auto const & r_ids_entry = resource_types.find(br::ResourceType::PROC_ELEMENT);
	pe_ids = r_ids_entry->second;
	logger->Info("Init: %d processing elements available", pe_ids.size());
	if (pe_ids.empty()) {
		logger->Crit("Init: not available CPU cores!");
		return SCHED_R_UNAVAILABLE;
	}

	// Not running applications count
	nr_not_run_apps  = sys->SchedulablesCount(ba::Schedulable::READY);
	nr_not_run_apps += sys->SchedulablesCount(ba::Schedulable::THAWED);
	nr_not_run_apps += sys->SchedulablesCount(ba::Schedulable::RESTORING);

	// Running applications count
	nr_run_apps = sys->SchedulablesCount(ba::Schedulable::RUNNING);

	// Total applications count
	nr_apps = nr_not_run_apps + nr_run_apps;
	logger->Info("Init: nr. active applications = %d", nr_apps);

	// Currently available CPU
	available_cpu = ra.Available("sys.cpu.pe", sched_status_view);
	logger->Info("Init: <sys.cpu.pe> available  = %d", available_cpu);

	return SCHED_OK;
}

void AdaptiveCPUSchedPol::ComputeNextCPUQuota(AppInfo_t * ainfo)
{
	logger->Info("ComputeNextCPUQuota: [%s]", ainfo->papp->StrId());

	if (!ainfo->papp->Running()) {

		logger->Info("ComputeNextCPUQuota: [%s] quota_not_run_apps = %ld",
			ainfo->papp->StrId(),
			quota_not_run_apps);
		if (quota_not_run_apps < INITIAL_DEFAULT_QUOTA)
			ainfo->next_quota = quota_not_run_apps;
		else
			ainfo->next_quota = INITIAL_DEFAULT_QUOTA;

		logger->Info("ComputeNextCPUQuota: [%s] next_quota = %ld",
			ainfo->papp->StrId(), ainfo->next_quota);

		ainfo->pawm = std::make_shared<ba::WorkingMode>(
			ainfo->papp->WorkingModes().size(), "Default", 1, ainfo->papp);

		// Set initial integral error
		ainfo->papp->SetAttribute("ierr", std::to_string(0));

		// Set initial error for derivative controller
		ainfo->papp->SetAttribute("derr", std::to_string(0));

		available_cpu -= ainfo->next_quota;

		logger->Info("ComputeNextCPUQuota: [%s] next_quota=%d, prev_quota=%d, "
			"prev_used=%d, delta=%ld, available_cpu=%ld",
			ainfo->papp->StrId(),
			ainfo->next_quota,
			ainfo->prev_quota,
			ainfo->prev_used,
			ainfo->prev_delta,
			available_cpu);

		return;
	}

	// if cpu_usage.curr == quota we push a forfait delta
	if (ainfo->prev_used >= ainfo->prev_quota - THRESHOLD) {
		ainfo->prev_delta = neg_delta;
	}

	int64_t error, ierr, derr;
	int64_t cv, pvar, ivar, dvar;

	// PROPORTIONAL CONTROLLER:
	error = ADMISSIBLE_DELTA / 2 - ainfo->prev_delta;

	// Check we are in the admissible range
	if (abs(error) < ADMISSIBLE_DELTA / 2)
		error = 0;

	pvar = kp * error;

	// INTEGRAL CONTROLLER
	ierr = std::stoll(ainfo->papp->GetAttribute("ierr")) + error;
	ivar = ki * ierr;

	// DERIVATIVE CONTROLLER
	derr = error - std::stoll(ainfo->papp->GetAttribute("derr"));
	dvar = kd * derr;
	logger->Info("ComputeNextCPUQuota: [%s] pvar=%d, ivar=%d, dvar=%d",
		ainfo->papp->StrId(), pvar, ivar, dvar);

	// Compute control variable
	cv = pvar + ivar + dvar;
	logger->Info("ComputeNextCPUQuota: [%s] error=%d, cv=%d",
		ainfo->papp->StrId(), error, cv);

	// Check available CPU
	if (cv > 0)
		cv = ((int64_t) available_cpu > cv) ? cv : available_cpu;
	else
		// Reset in case of fault
		if (std::abs<int>(cv) > ainfo->prev_quota) {
		logger->Error("ComputeNextCPUQuota: [%s] requires quota lower than zero: "
			"resetting to initial default value",
			ainfo->papp->StrId());
		ainfo->next_quota = (available_cpu > INITIAL_DEFAULT_QUOTA) ? INITIAL_DEFAULT_QUOTA : available_cpu;
	}

	ainfo->next_quota = ainfo->prev_quota + cv;

	// Create AWM
	ainfo->pawm = std::make_shared<ba::WorkingMode>(
		ainfo->papp->WorkingModes().size(), "Adaptation", 1, ainfo->papp);

	// Update errors and expected delta
	ainfo->papp->SetAttribute("ierr", std::to_string(ierr));
	ainfo->papp->SetAttribute("derr", std::to_string(error));

	// Update available CPU quota
	if (ainfo->next_quota > ainfo->prev_quota)
		available_cpu -= ainfo->next_quota - ainfo->prev_quota;
	else
		available_cpu += ainfo->prev_quota - ainfo->next_quota;

	logger->Info("ComputeNextCPUQuota: [%s] next_quota=%d, prev_quota=%d, "
		"prev_used=%d, delta=%ld, available_cpu=%ld [UPDATED]",
		ainfo->papp->StrId(),
		ainfo->next_quota,
		ainfo->prev_quota,
		ainfo->prev_used,
		ainfo->prev_delta,
		available_cpu);
}

AppInfo_t AdaptiveCPUSchedPol::InitializeAppInfo(bbque::app::AppCPtr_t papp)
{
	AppInfo_t ainfo;
	ainfo.papp = papp;
	ainfo.pawm = papp->CurrentAWM();
	ainfo.prev_quota = ra.UsedBy("sys.cpu.pe", papp, 0);
	ainfo.next_quota = 0;

	auto prof = papp->GetRuntimeProfile();
	ainfo.prev_used  = prof.cpu_usage.curr;
	ainfo.prev_delta = ainfo.prev_quota - ainfo.prev_used;
	logger->Info("InitializeAppInfo: [%s] next_quota=%ld, prev_quota=%ld, "
		"prev_used=%ld, delta=%ld, available_cpu=%ld",
		papp->StrId(),
		ainfo.next_quota,
		ainfo.prev_quota,
		ainfo.prev_used,
		ainfo.prev_delta,
		available_cpu);

	return ainfo;
}

SchedulerPolicyIF::ExitCode_t
AdaptiveCPUSchedPol::AssignWorkingMode(bbque::app::AppCPtr_t papp)
{
	ApplicationManager & am(ApplicationManager::GetInstance());

	if (papp == nullptr) {
		logger->Error("AssignWorkingMode: null application descriptor!");
		return SCHED_ERROR;
	}

	// Print the run-time profiling info if running
	if (papp->Running()) {
		auto prof = papp->GetRuntimeProfile();
		logger->Info("AssignWorkingMode: [%s] "
			"cpu_usage.curr=%d c_time=%d, ggap=%d [valid=%d]",
			papp->StrId(),
			prof.cpu_usage.curr,
			prof.ctime_ms,
			prof.ggap_percent,
			prof.is_updated);
	}

	// It initializes a structure with all the info about the app,
	// to have it in a compact way
	AppInfo_t ainfo = AdaptiveCPUSchedPol::InitializeAppInfo(papp);

	if (quota_not_run_apps == 0 && !ainfo.papp->Running()) {
		logger->Info("AssignWorkingMode: [%s] not enough available resources",
			papp->StrId());
		return SCHED_SKIP_APP;
	}

	// Compute the next amount of CPU quota
	AdaptiveCPUSchedPol::ComputeNextCPUQuota(&ainfo);

	// Add the request of CPU quota
	auto pawm = ainfo.pawm;
	pawm->AddResourceRequest("sys.cpu.pe",
				ainfo.next_quota,
				br::ResourceAssignment::Policy::SEQUENTIAL);

	// Binding: look for the first available CPU
	BindingManager & bdm(BindingManager::GetInstance());
	BindingMap_t & bindings(bdm.GetBindingDomains());
	auto & cpu_ids(bindings[br::ResourceType::CPU]->r_ids);

	for (BBQUE_RID_TYPE cpu_id : cpu_ids) {
		logger->Info("AssignWorkingMode: [%s] binding attempt CPU id = %d",
			papp->StrId(), cpu_id);

		// CPU binding
		int32_t ref_num = -1;
		ref_num = pawm->BindResource(br::ResourceType::CPU, R_ID_ANY, cpu_id, ref_num);
		if (ref_num < 0) {
			logger->Error("AssignWorkingMode: [%s] CPU binding to < %d > failed",
				papp->StrId(), cpu_id);
			continue;
		}

		// Schedule request
		ApplicationManager::ExitCode_t am_ret;
		am_ret = am.ScheduleRequest(papp, pawm, sched_status_view, ref_num);
		if (am_ret != ApplicationManager::AM_SUCCESS) {
			logger->Error("AssignWorkingMode: [%s] schedule request failed",
				papp->StrId());
			continue;
		}

		return SCHED_OK;
	}

	return SCHED_ERROR;

}

SchedulerPolicyIF::ExitCode_t
AdaptiveCPUSchedPol::ScheduleApplications(std::function <SchedulerPolicyIF::ExitCode_t(bbque::app::AppCPtr_t) > do_func)
{
	AppsUidMapIt app_it;
	ba::AppCPtr_t app_ptr;

	// Fair alternative among not running applications
	app_ptr = sys->GetFirstRunning(app_it);
	for (; app_ptr; app_ptr = sys->GetNextRunning(app_it)) {
		do_func(app_ptr);
	}

	if (nr_not_run_apps != 0)
		quota_not_run_apps = available_cpu / nr_not_run_apps;

	app_ptr = sys->GetFirstReady(app_it);
	for (; app_ptr; app_ptr = sys->GetNextReady(app_it)) {
		do_func(app_ptr);
	}

	app_ptr = sys->GetFirstThawed(app_it);
	for (; app_ptr; app_ptr = sys->GetNextThawed(app_it)) {
		do_func(app_ptr);
	}

	app_ptr = sys->GetFirstRestoring(app_it);
	for (; app_ptr; app_ptr = sys->GetNextRestoring(app_it)) {
		do_func(app_ptr);
	}

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
AdaptiveCPUSchedPol::Schedule(System & system,
			      RViewToken_t & status_view)
{
	// Class providing query functions for applications and resources
	sys = &system;

	// Initialization
	auto result = Init();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: initialization failed");
		return SCHED_ERROR;
	}
	else {
		logger->Debug("Schedule: resource status view = %ld",
			sched_status_view);
	}

	auto assign_awm = std::bind(
				static_cast<ExitCode_t (AdaptiveCPUSchedPol::*)(ba::AppCPtr_t)>
				(&AdaptiveCPUSchedPol::AssignWorkingMode),
				this, _1);

	result = AdaptiveCPUSchedPol::ScheduleApplications(assign_awm);
	if (result != SCHED_OK)
		return result;
	logger->Debug("Schedule: done");

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;

	return SCHED_DONE;
}

} // namespace plugins

} // namespace bbque
