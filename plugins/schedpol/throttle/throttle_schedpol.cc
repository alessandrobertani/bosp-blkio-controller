/*
 * Copyright (C) 2018  Politecnico di Milano
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

#include "throttle_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "bbque/config.h"
#include "bbque/modules_factory.h"
#include "bbque/binding_manager.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/platform_manager.h"
#include "bbque/pm/power_manager.h"

#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "bbque/res/resource_path.h"
#include "tg/task_graph.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

#define CPU_QUOTA_TO_ALLOCATE 100
#define MAX_CPU_QUOTA		  400

#define MIN_GAP_CPU_CHANGE	   50
#define GAP_THRESHOLD	        5

#define BBQUE_RP_TYPE_LP	false
#define BBQUE_RP_TYPE_HP	 true

#define CPU_REQUEST "sys.cpu.pe"
#define RUNTIME_AWM_NAME "Run-time"

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque { namespace plugins {

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * ThrottleSchedPol::Create(PF_ObjectParams *) {
	return new ThrottleSchedPol();
}

int32_t ThrottleSchedPol::Destroy(void * plugin) {
	if (!plugin)
		return -1;
	delete (ThrottleSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * ThrottleSchedPol::Name() {
	return SCHEDULER_POLICY_NAME;
}

ThrottleSchedPol::ThrottleSchedPol():
		cm(ConfigurationManager::GetInstance()),
		ra(ResourceAccounter::GetInstance()),
		wm(PowerManager::GetInstance()),
		plm(PlatformManager::GetInstance()) {
	logger = bu::Logger::GetLogger("bq.sp.throttle");
	assert(logger);
	if (logger)
		logger->Info("test: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
			FI("test: Built new dynamic object [%p]\n"), (void *)this);
}


ThrottleSchedPol::~ThrottleSchedPol() {

}


SchedulerPolicyIF::ExitCode_t ThrottleSchedPol::Init() {
	// Build a string path for the resource state view
	std::string token_path(MODULE_NAMESPACE);
	++status_view_count;
	token_path.append(std::to_string(status_view_count));
	logger->Debug("Init: Require a new resource state view [%s]",
		token_path.c_str());

	// Get a fresh resource status view
	ResourceAccounterStatusIF::ExitCode_t ra_result =
		ra.GetView(token_path, sched_status_view);
	if (ra_result != ResourceAccounterStatusIF::RA_SUCCESS) {
		logger->Fatal("Init: cannot get a resource state view");
		return SCHED_ERROR_VIEW;
	}
	logger->Debug("Init: resources state view token: %ld", sched_status_view);

#ifdef CONFIG_TARGET_ARM_BIG_LITTLE
	// Retrieving High Performance CPUs type
	auto & resource_types = sys->ResourceTypes();
	auto const & r_cpu_ids_entry = resource_types.find(br::ResourceType::CPU);
	cpu_ids = r_cpu_ids_entry->second;

	//PlatformManager & plm(PlatformManager::GetInstance());
	//PowerManager & wm(PowerManager::GetInstance());

	for(auto cpu_id : cpu_ids){
		auto cpu_path = ra.GetPath("sys.cpu"+std::to_string(cpu_id)+".pe");
		auto pe_list = ra.GetResources(cpu_path);
		uint32_t pe_count = 0;
		uint32_t perf_states_count = 0;
		for(auto pe : pe_list){
			auto pe_path = pe->Path();//ra.GetPath(pe->Path());
			if(plm.IsHighPerformance(pe_path)){
				pe_count++;
				wm.GetPerformanceStatesCount(pe_path, perf_states_count);
			}
		}
		// If all the pes are HP then the CPU is HP
		if (pe_count == pe_list.size()){
			logger->Debug("Init: %s is High Performance\n", cpu_path->ToString().c_str());
			high_perf_cpus[cpu_id] = true;
			high_perf_states_count = perf_states_count;
		} else {
			high_perf_cpus[cpu_id] = false;
			low_perf_states_count = perf_states_count;
		}

		//perf_states_count = wm.GetPerformanceStatesCount(pe_path);
		//InitPerfState(perf_states_count, high_perf_cpus[cpu_id]);
	}

	//logger->Info("Total performance states: %d", perf_states.size());
#endif // CONFIG_TARGET_ARM_BIG_LITTLE

	return SCHED_OK;
}

void ThrottleSchedPol::InitPerfState(
	uint32_t perf_states_count, 
	BBQUE_HP_TYPE IsHighPerformance){
	for (uint32_t i = 0; i<perf_states_count; i++){
		perf_states.push_back(IsHighPerformance);
	}
}

SchedulerPolicyIF::ExitCode_t ThrottleSchedPol::Schedule(
		System & system,
		RViewToken_t & status_view) {
	SchedulerPolicyIF::ExitCode_t result = SCHED_DONE;

	// Class providing query functions for applications and resources
	sys = &system;
	result = Init();
	if (result != SCHED_OK)
		return result;

	/** INSERT YOUR CODE HERE **/

	result = ScheduleApplications();
	if (result != SCHED_OK){
		logger->Debug("Schedule: error in application scheduling");
		return result;
	}

	logger->Debug("Schedule: done with applications");

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	result = ScheduleProcesses();
	if (result != SCHED_OK){
		logger->Debug("Schedule: error in processes scheduling");
		return result;
	}

	logger->Debug("Schedule: done with processes");
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	return SCHED_DONE;
}


SchedulerPolicyIF::ExitCode_t ThrottleSchedPol::ScheduleApplications() {
	SchedulerPolicyIF::ExitCode_t ret;
	bbque::app::AppCPtr_t papp;
	AppsUidMapIt app_it;

	// Ready applications
	papp = sys->GetFirstReady(app_it);
	for (; papp; papp = sys->GetNextReady(app_it)) {
		ApplicationInfo app_info(papp);
		DumpRuntimeProfileStats(app_info);

		ret = AssignWorkingMode(papp);
		if (ret != SCHED_OK) {
			logger->Error("ScheduleApplications: error in READY");
			return ret;
		}
	}

	// Running applications
	papp = sys->GetFirstRunning(app_it);
	for (; papp; papp = sys->GetNextRunning(app_it)) {
		//papp->CurrentAWM()->ClearResourceRequests();
		ret = AssignWorkingMode(papp);
		ApplicationInfo app_info(papp);
		DumpRuntimeProfileStats(app_info);
		if (ret != SCHED_OK) {
			logger->Error("ScheduleApplications: error in RUNNING");
			return ret;
		}
	}

	return SCHED_OK;
}


#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER

SchedulerPolicyIF::ExitCode_t ThrottleSchedPol::ScheduleProcesses() {
	SchedulerPolicyIF::ExitCode_t ret;
	ProcessManager & prm(ProcessManager::GetInstance());
	ProcessMapIterator proc_it;

	// Ready applications
	ProcPtr_t proc = prm.GetFirst(ba::Schedulable::READY, proc_it);
	for (; proc; proc = prm.GetNext(ba::Schedulable::READY, proc_it)) {
		ret = AssignWorkingMode(proc);
		if (ret != SCHED_OK) {
			logger->Error("ScheduleProcesses: error in READY");
			return ret;
		}
	}

	// Running applications
	proc = prm.GetFirst(ba::Schedulable::RUNNING, proc_it);
	for (; proc; proc = prm.GetNext(ba::Schedulable::RUNNING, proc_it)) {
		ret = AssignWorkingMode(proc);
		if (ret != SCHED_OK) {
			logger->Error("ScheduleProcesses: error in RUNNING");
			return ret;
		}
	}

	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t
ThrottleSchedPol::AssignWorkingMode(ProcPtr_t proc) {
	ProcessManager & prm(ProcessManager::GetInstance());
	if (proc == nullptr) {
		logger->Error("AssignWorkingMode: null process descriptor!");
		return SCHED_ERROR;
	}

	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = proc->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(99, RUNTIME_AWM_NAME, 1, proc);
	}

	// Resource request addition
	auto res_ass; 
	res_ass = pawm->AddResourceRequest(
				CPU_REQUEST, MAX_CPU_QUOTA,
				br::ResourceAssignment::Policy::BALANCED);

	// Look for the first available CPU
	BindingManager & bdm(BindingManager::GetInstance());
	BindingMap_t & bindings(bdm.GetBindingDomains());
	auto & cpu_ids(bindings[br::ResourceType::CPU]->r_ids);
	for (BBQUE_RID_TYPE cpu_id: cpu_ids) {
		logger->Info("AssingWorkingMode: binding attempt CPU id = <%d>", cpu_id);

		// CPU binding
		auto ref_num = DoCPUBinding(pawm, cpu_id);
		if (ref_num < 0) {
			logger->Error("AssingWorkingMode: CPU binding to [%d] failed", cpu_id);
			continue;
		}

		// Schedule request
		auto prm_ret = prm.ScheduleRequest(proc, pawm, sched_status_view, ref_num);
		if (prm_ret != ProcessManager::SUCCESS) {
			logger->Error("AssignWorkingMode: schedule request failed for [%d]",
				proc->StrId());
			continue;
		}

		return SCHED_OK;
	}

	return SCHED_ERROR;
}

#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

SchedulerPolicyIF::ExitCode_t
ThrottleSchedPol::AssignWorkingMode(bbque::app::AppCPtr_t papp) {

	if (papp == nullptr) {
		logger->Error("AssignWorkingMode: null application descriptor!");
		return SCHED_ERROR;
	}

	std::set<BBQUE_RID_TYPE> available_cpu_ids;
	ApplicationManager & am(ApplicationManager::GetInstance());
	ApplicationManager::ExitCode_t am_ret;
	bool new_application = false;

	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	ba::AwmPtr_t next_pawm;
	uint32_t next_ps;
				//papp->WorkingModes().size()+1,"Run-time", 1, papp);
	//assert(next_pawm);
	/* 1 case: new Application */
	if (pawm == nullptr) {
		//AssignWorkingModeCase1(papp, next_pawm);
		logger->Info("AssignWorkingMode: Case 1: [%d] is a new application...", papp->StrId());
		//pawm = std::make_shared<ba::WorkingMode>(
		//		papp->WorkingModes().size(),"Run-time", 1, papp);

		app_awm_map[papp->Pid()] = 0;
		next_pawm  = std::make_shared<ba::WorkingMode>(
				app_awm_map[papp->Pid()], RUNTIME_AWM_NAME, 1, papp);
		assert(next_pawm);
		logger->Error("AssignWorkingMode: Added to map [%d:%d]",
			papp->Pid(), app_awm_map[papp->Pid()]);
		// Resource request addition
		auto res_ass = next_pawm->AddResourceRequest(
					CPU_REQUEST, CPU_QUOTA_TO_ALLOCATE,
					br::ResourceAssignment::Policy::SEQUENTIAL);

		available_cpu_ids = TakeCPUsType(BBQUE_RP_TYPE_LP);
		logger->Debug("AssignWorkingMode: available cpus size <%d>", available_cpu_ids.size());

		//br::Resource::PowerSettings new_settings;
		//new_settings.perf_state = low_perf_states_count / 2;
		//next_pawm->GetResourceRequest(CPU_REQUEST)->SetPowerSettings(new_settings);
		next_ps = low_perf_states_count / 2;
		logger->Debug("AssignWorkingMode: resource request added for [%d]", papp->StrId());
		new_application = true;
	}

	ApplicationInfo app_info(papp);

	if(app_info.runtime.is_valid){ 
		logger->Debug("AssignWorkingMode: app info is valid");
		/* 3 case: the ggap is negligible */
		if (abs(app_info.runtime.ggap_percent) < GAP_THRESHOLD){
			logger->Info("AssignWorkingMode: Case 3: Ggap negligible -> Scheduled as previous");
			br::RViewToken_t ref_num;

			am_ret = am.ScheduleRequestAsPrev(papp, sched_status_view);
			if (am_ret != ApplicationManager::AM_SUCCESS) {
				logger->Error("AssignWorkingMode: schedule request failed for [%d]",
					papp->StrId());
				return SCHED_ERROR;
			}

			return SCHED_OK;

		} else { /* 4 case: the ggap triggers a performance change */
			//AssignWorkingModeCase4(papp, next_pawm, pawm, app_info, available_cpu_ids);
			logger->Info("AssignWorkingMode: Case 4: Ggap correction");
			// Computing CPU type and boost value
			float boost;
			bool CPUChange = false;

			// Creating the new AWM
			app_awm_map[papp->Pid()] = app_awm_map[papp->Pid()] + 1;
			next_pawm  = std::make_shared<ba::WorkingMode>(
				app_awm_map[papp->Pid()],"Run-time", 1, papp);
			assert(next_pawm);
			logger->Error("AssignWorkingMode: Updated map [%d:%d]",
				papp->Pid(), app_awm_map[papp->Pid()]);

			logger->Debug("AssignWorkingMode: papp [%s] current AWM [%s] next AWM [%s]", 
				papp->StrId(), papp->CurrentAWM()->StrId(), next_pawm->StrId());
			//auto prev_binding = pawm->GetResourceRequest("sys.cpu.pe");
			res::ResourceAssignmentMapPtr_t prev_binding_ptr = pawm->GetResourceBinding();
			auto & prev_binding = *(prev_binding_ptr.get());
			logger->Debug("AssignWorkingMode: prev_binding res map size = <%d>",
				prev_binding.size());
			//papp->CurrentAWM()->ClearResourceRequests()
			for(auto bind : prev_binding){
				for(auto prev_res : bind.second->GetResourcesList()){

					auto prev_res_path = prev_res->Path();
					logger->Debug("AssignWorkingMode: Computing PS for res: [%s]", 
						prev_res_path->ToString().c_str());
					uint32_t ps_count;
					
					uint32_t current_ps;
					int ggap_percent = 0-(app_info.runtime.ggap_percent);
					

					// Checking if the ggap required a change in the CPU type (LP-->HP)
					if(ggap_percent >= MIN_GAP_CPU_CHANGE && 
						!plm.IsHighPerformance(prev_res_path)){
							available_cpu_ids = TakeCPUsType(BBQUE_RP_TYPE_HP);
							next_ps = high_perf_states_count / 2;
							CPUChange = true;
							logger->Debug("AssingWorkingMode: changing CPU type to HP");
							logger->Debug("AssignWorkingMode: Computed next PS = <%d>", next_ps);
					}

					// Checking if the ggap required a change in the CPU type (HP-->LP)
					if(ggap_percent <= (0-MIN_GAP_CPU_CHANGE) && 
						plm.IsHighPerformance(prev_res_path)){
							available_cpu_ids = TakeCPUsType(BBQUE_RP_TYPE_LP);
							next_ps = low_perf_states_count / 2;
							CPUChange = true;
							logger->Debug("AssingWorkingMode: changing CPU type to LP");
					}

					if(!CPUChange){
						wm.GetPerformanceStatesCount(prev_res_path, ps_count);
						logger->Debug("AssingWorkingMode: Res [%s] has <%d> perf states", 
							prev_res_path->ToString().c_str(), ps_count);
						wm.GetPerformanceState(prev_res_path, current_ps);
						logger->Debug("AssingWorkingMode: Current PS = <%d>", current_ps);

						// Avoiding division by zero
						current_ps++;

						// Computing the boost value
						boost = ComputeBoost(ggap_percent, 
							ps_count, 
							current_ps);
						logger->Error("AssingWorkingMode: Computed boost for [%s] = <%.2f>",
							prev_res_path->ToString().c_str(), boost);

						// Computing the next perf state
						next_ps = current_ps * (1 + boost);
						next_ps--;
						if(next_ps > ps_count - 1)
							next_ps = ps_count - 1;
						if(next_ps < 0)
							next_ps = 0;
						logger->Debug("AssingWorkingMode: Computed next PS for [%s] = [%d -> %d]",
							prev_res_path->ToString().c_str(), current_ps-1, next_ps);

						// Select the available cpu type cluster
						available_cpu_ids = TakeCPUsType(plm.IsHighPerformance(prev_res_path));
					}

					next_pawm->AddResourceRequest(
							CPU_REQUEST,
							CPU_QUOTA_TO_ALLOCATE,
							br::ResourceAssignment::Policy::SEQUENTIAL);
				}
			}
		}
	} else if (!new_application) { /* 2 case: No RT profile */
		logger->Info("AssignWorkingMode: Case 2: No RT profile -> Scheduled as previous");
		am_ret = am.ScheduleRequestAsPrev(papp, sched_status_view);
		if (am_ret != ApplicationManager::AM_SUCCESS) {
			logger->Error("AssignWorkingMode: schedule request failed for [%d]",
				papp->StrId());
			return SCHED_ERROR;
		}

		return SCHED_OK;
	}
	

	// Look for the first available CPU
	BindingManager & bdm(BindingManager::GetInstance());
	BindingMap_t & bindings(bdm.GetBindingDomains());

	for (BBQUE_RID_TYPE cpu_id: available_cpu_ids) {
		logger->Info("AssingWorkingMode: binding attempt CPU id = <%d>", cpu_id);

		// CPU binding
		auto ref_num = DoCPUBinding(next_pawm, cpu_id);
		if (ref_num < 0) {
			logger->Error("AssingWorkingMode: CPU binding to [%d] failed", cpu_id);
			continue;
		}

		// Power setting
		br::Resource::PowerSettings new_settings;
		new_settings.SetPerformanceState(next_ps);
		logger->Error("AssignWorkingMode: Updated PowerSettings "
								"[GOV = [%s], FREQ = [%d], PERF_STATE = [%d]",
								new_settings.FrequencyGovernor().c_str(),
								new_settings.ClockFrequency(),
								new_settings.PerformanceState());

		auto binding_map = next_pawm->GetSchedResourceBinding(ref_num);
		auto & binding = *(binding_map.get());
		for(auto bind : binding){
			for(auto res : bind.second->GetResourcesList()){

			logger->Error("AssignWorkingMode: %s",res->Path()->ToString().c_str());
			ra.EnqueueResourceToPowerManage(res, new_settings);
			}

		}
		// Assign 

		// Schedule request
		am_ret = am.ScheduleRequest(papp, next_pawm, sched_status_view, ref_num);
		if (am_ret != ApplicationManager::AM_SUCCESS) {
			logger->Error("AssignWorkingMode: schedule request failed for [%d]",
				papp->StrId());
			continue;
		}

		return SCHED_OK;
	}

	return SCHED_ERROR;
}

float ThrottleSchedPol::ComputeBoost(int ggap_percent, 
	uint32_t ps_count, 
	uint32_t current_ps){
	float boost = 0.0;

	logger->Debug("ComputeBoost: ggap_percent = <%d>, ps_count = <%d>, current_ps = <%d>",
		ggap_percent, ps_count, current_ps); 

	int pgap = ps_count - current_ps;
	if (ggap_percent > 0){
		if (pgap != 0){
			boost = ((ggap_percent / 100.0 ) * ps_count) / pgap; 
			logger->Debug("ComputeBoost:: ggap = <positive>, pgap = <%d>, boost = <%.2f>",
				pgap, boost);
			return boost;
		} else {
			return 0.0;
		}
	} else {
		if (ps_count - pgap){
			boost = ((ggap_percent / 100.0) * ps_count) / (ps_count - pgap);
			logger->Debug("ComputeBoost:: ggap = <negative>, pgap = <%d>, boost = <%.2f>", 
				pgap, boost);
			return boost;
		} else {
			return 0.0;
		}
	} 
}

std::set<BBQUE_RID_TYPE> ThrottleSchedPol::TakeCPUsType(bool IsHighPerformance){
	std::set<BBQUE_RID_TYPE> res_set;

	for (auto cpu_info : high_perf_cpus){
		if (!(cpu_info.second ^ IsHighPerformance)){
			logger->Debug("TakeCPUsType: adding available CPU [%d]", cpu_info.first);
			res_set.insert(cpu_info.first);
		}
	}

	return res_set;
}

int32_t ThrottleSchedPol::DoCPUBinding(
		bbque::app::AwmPtr_t pawm,
		BBQUE_RID_TYPE cpu_id) {
	// CPU-level binding: the processing elements are in the scope of the CPU 'cpu_id'
	int32_t ref_num = -1;
	ref_num = pawm->BindResource(br::ResourceType::CPU, R_ID_ANY, cpu_id, ref_num);
	//auto resource_path = ra.GetPath("sys0.cpu" + std::to_string(cpu_id) + ".pe");

	// The ResourceBitset object is used for the processing elements binding
	// (CPU core mapping)
/*	br::ResourceBitset pes;
	uint16_t pe_count = 0;
	for (auto & pe_id: pe_ids) {
		pes.Set(pe_id);
		ref_num = pawm->BindResource(resource_path, pes, ref_num);
		logger->Info("AssignWorkingMode: binding refn: %d", ref_num);
		++pe_count;
		if (pe_count == CPU_QUOTA_TO_ALLOCATE / 100) break;
	}
*/
	return ref_num;
}

void ThrottleSchedPol::DumpRuntimeProfileStats(ApplicationInfo &app){
	logger->Debug("[APP %s] Runtime Profile", app.name.c_str());
	logger->Debug("Runtime valid: %s", app.runtime.is_valid ? "yes" : "no");
	logger->Debug("  Goal Gap: %d", app.runtime.ggap_percent);
	logger->Debug("  Lower allocation boundary: [CPU: <%d>, exp GGAP: <%d>], ETA <%d>",
				app.runtime.gap_history.lower_cpu,
				app.runtime.gap_history.lower_gap,
				app.runtime.gap_history.lower_age);
	logger->Debug("  Upper allocation boundary: [CPU: <%d>, exp GGAP: <%d>], ETA <%d>",
				app.runtime.gap_history.upper_cpu,
				app.runtime.gap_history.upper_gap,
				app.runtime.gap_history.upper_age);
	logger->Debug("  Last measured CPU Usage: <%d>", app.runtime.cpu_usage.curr);
	logger->Debug("  Last allocated CPU Usage: <%d>", app.runtime.cpu_usage.predicted);
}

} // namespace plugins

} // namespace bbque
