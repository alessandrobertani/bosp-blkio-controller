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

#include "test_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "bbque/config.h"
#include "bbque/modules_factory.h"
#include "bbque/binding_manager.h"
#include "bbque/platform_manager.h"
#include "bbque/utils/logging/logger.h"

#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "bbque/res/resource_path.h"
#include "tg/task_graph.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

#define CPU_QUOTA_TO_ALLOCATE 100

namespace bu = bbque::utils;
namespace po = boost::program_options;

using namespace std::placeholders;

namespace bbque {
namespace plugins {

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * TestSchedPol::Create(PF_ObjectParams *)
{
	return new TestSchedPol();
}

int32_t TestSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (TestSchedPol *) plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * TestSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

TestSchedPol::TestSchedPol() :
	cm(ConfigurationManager::GetInstance()),
	ra(ResourceAccounter::GetInstance())
{
	logger = bu::Logger::GetLogger("bq.sp.test");
	assert(logger);
	if (logger)
		logger->Info("test: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
		FI("test: Built new dynamic object [%p]\n"), (void *) this);
}

TestSchedPol::~TestSchedPol()
{

}

SchedulerPolicyIF::ExitCode_t TestSchedPol::_Init()
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

	// Get all the system nodes
	auto sys_rsrcs = sys->GetResources("sys");
	logger->Info("Init: %d systems node(s) available", sys_rsrcs.size());
	for (auto & sys_rsrc : sys_rsrcs) {
		sys_ids.push_back(sys_rsrc->ID());
	}

	// Load all the applications task graphs
	logger->Info("Init: loading the applications task graphs");
	fut_tg = std::async(std::launch::async, &System::LoadTaskGraphs, sys);

	// Applications count
	nr_apps = sys->SchedulablesCount(ba::Schedulable::READY);
	nr_apps += sys->SchedulablesCount(ba::Schedulable::RUNNING);
	logger->Info("Init: nr. active applications = %d", nr_apps);

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t TestSchedPol::Schedule(
	System & system,
	RViewToken_t & status_view)
{
	SchedulerPolicyIF::ExitCode_t result = SCHED_DONE;

	// Class providing query functions for applications and resources
	sys = &system;
	result = Init();
	if (result != SCHED_OK)
		return result;

	/** INSERT YOUR CODE HERE **/
	fut_tg.get();
	auto assign_awm_app = std::bind(
		static_cast<ExitCode_t (TestSchedPol::*)(ba::AppCPtr_t)>
		(&TestSchedPol::AssignWorkingMode),
		this, _1);

	// Iterate over all the applications to schedule
	ForEachApplicationToScheduleDo(assign_awm_app);
	if (result != SCHED_OK) {
		logger->Debug("Schedule: not SCHED_OK return [result=%d]", result);
		return result;
	}
	logger->Debug("Schedule: done with applications");

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	auto assign_awm_proc = std::bind(
		static_cast<ExitCode_t (TestSchedPol::*)(ProcPtr_t)>
		(&TestSchedPol::AssignWorkingMode),
		this, _1);

	// Iterate over all the processes to schedule
	ForEachProcessToScheduleDo(assign_awm_proc);
	if (result != SCHED_OK)
		return result;
	logger->Debug("Schedule: done with processes");

#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	return SCHED_DONE;
}


#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AssignWorkingMode(ProcPtr_t proc)
{
	if (proc == nullptr) {
		logger->Error("AssignWorkingMode: null process descriptor!");
		return SCHED_ERROR;
	}

	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = proc->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(99, "Run-time", 1, proc);
	}
	else {
		if (proc->State() == Schedulable::RUNNING) {
			proc->CurrentAWM()->ClearResourceRequests();
			logger->Debug("AssignWorkingMode: <%s> RUNNING ->"
				"clear previous resources requests",
				proc->StrId());
		}
	}

	// Resource request addition
	pawm->AddResourceRequest("sys.cpu.pe", CPU_QUOTA_TO_ALLOCATE,
		br::ResourceAssignment::Policy::BALANCED);

	// Look for the first available CPU
	ProcessManager & prm(ProcessManager::GetInstance());
	BindingManager & bdm(BindingManager::GetInstance());
	BindingMap_t & bindings(bdm.GetBindingDomains());
	auto & cpu_ids(bindings[br::ResourceType::CPU]->r_ids);
	for (BBQUE_RID_TYPE cpu_id : cpu_ids) {
		logger->Info("AssingWorkingMode: binding attempt CPU id = <%d>",
			cpu_id);

		// CPU binding
		auto ref_num = DoCPUBinding(pawm, cpu_id);
		if (ref_num < 0) {
			logger->Error("AssingWorkingMode: CPU binding to <%d> failed",
				cpu_id);
			continue;
		}

		// Schedule request
		auto prm_ret = prm.ScheduleRequest(proc, pawm, sched_status_view, ref_num);
		if (prm_ret != ProcessManager::SUCCESS) {
			logger->Error("AssignWorkingMode: schedule request failed for [%s]",
				proc->StrId());
			continue;
		}

		return SCHED_OK;
	}

	return SCHED_ERROR;
}

#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AssignWorkingMode(bbque::app::AppCPtr_t papp)
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
			"cpu_usage=%d c_time=%d, ggap=%d [valid=%d]",
			papp->StrId(),
			prof.cpu_usage,
			prof.ctime_ms,
			prof.ggap_percent,
			prof.is_valid);
	}

	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(
			papp->WorkingModes().size(), "Run - time", 1, papp);
	}

	// Resource request addition
	std::string pe_request("sys.cpu.pe");
	logger->Debug("AssignWorkingMode: [%s] adding resource request <%s>",
		      papp->StrId(), pe_request.c_str());
	pawm->AddResourceRequest(
		pe_request,
		CPU_QUOTA_TO_ALLOCATE,
		br::ResourceAssignment::Policy::BALANCED);

	// Look for the first available CPU
	BindingManager & bdm(BindingManager::GetInstance());
	BindingMap_t & bindings(bdm.GetBindingDomains());
	auto & cpu_ids(bindings[br::ResourceType::CPU]->r_ids);
	logger->Debug("AssignWorkingMode: [%s] nr. CPU ids: %d",
		      papp->StrId(), cpu_ids.size());

	for (BBQUE_RID_TYPE cpu_id : cpu_ids) {
		logger->Info("AssingWorkingMode: [%s] binding attempt CPU id = %d",
			papp->StrId(), cpu_id);

		// CPU binding
		auto ref_num = DoCPUBinding(pawm, cpu_id);
		if (ref_num < 0) {
			logger->Error("AssingWorkingMode: [%s] CPU binding to < %d > failed",
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

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

		MapTaskGraph(papp); // Task level mapping

#endif // CONFIG_BBQUE_TG_PROG_MODEL
		return SCHED_OK;
	}

	return SCHED_ERROR;
}

int32_t TestSchedPol::DoCPUBinding(
	bbque::app::AwmPtr_t pawm,
	BBQUE_RID_TYPE cpu_id)
{
	// CPU-level binding: the processing elements are in the scope of the CPU 'cpu_id'
	int32_t ref_num = -1;
	ref_num = pawm->BindResource(br::ResourceType::CPU, R_ID_ANY, cpu_id, ref_num);

	// The ResourceBitset object is used for the processing elements binding
	// (CPU core mapping)
	/*
	auto resource_path = ra.GetPath("sys.cpu" + std::to_string(cpu_id) + ".pe");
	br::ResourceBitset pes;
	uint16_t pe_count = 0;
	for (auto & pe_id : pe_ids) {
		pes.Set(pe_id);
		ref_num = pawm->BindResource(resource_path, pes, ref_num);
		logger->Info("DoCPUBinding : [ % s] binding refn : % d",
			     pawm->StrId(), ref_num);
		++pe_count;
		if (pe_count == CPU_QUOTA_TO_ALLOCATE / 100) break;
	}
	 */
	return ref_num;
}

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

void TestSchedPol::MapTaskGraph(bbque::app::AppCPtr_t papp)
{
	logger->Info("MapTaskGraph: [%s] mapping the task graph...", papp->StrId());

	auto task_graph = papp->GetTaskGraph();
	if (task_graph == nullptr) {
		logger->Warn("AssignWorkingMode: [%s] no task - graph to map",
			papp->StrId());
		return;
	}

	int unit_id = 3;          // An arbitrary processing unit number
	unsigned short int i = 0; // task index
	unsigned short int j = 0; // system index

	PlatformManager & plm(PlatformManager::GetInstance());
	const auto systems = plm.GetPlatformDescription().GetSystemsAll();

	uint16_t throughput;
	uint32_t c_time;
	for (auto t_entry : task_graph->Tasks()) {
		unit_id++;
		auto & task(t_entry.second);
		auto const & tr(papp->GetTaskRequirements(task->Id()));

		task->SetAssignedProcessor(unit_id);
		task->SetAssignedSystem(sys_ids[j]);
		auto const & ip_addr = systems.at(sys_ids[j]).GetNetAddress();
		task->SetAssignedSystemIp(ip_addr);

		task->GetProfiling(throughput, c_time);
		logger->Info("[%s] < T %d > throughput : %.2f / %.2f  ctime : %d / %d [ms]",
			papp->StrId(), t_entry.first,
			static_cast<float> (throughput) / 100.0, tr.Throughput(),
			c_time, tr.CompletionTime());
		++i;

		// Increment the system id index to dispatch the next task on a
		// different system node
		if (i > ceil( i / sys_ids.size() ))
			++j;
	}

	for (auto & b_entry : task_graph->Buffers()) {
		auto & buffer(b_entry.second);
		buffer->SetMemoryBank(0);
	}

	task_graph->GetProfiling(throughput, c_time);
	logger->Info("[%s] Task - graph throughput : %d  ctime : %d [app = %p]",
		papp->StrId(), throughput, c_time, papp.get());

	papp->SetTaskGraph(task_graph);
	logger->Info("[%s] Task - graph updated", papp->StrId());
}

#endif // CONFIG_BBQUE_TG_PROG_MODEL

} // namespace plugins

} // namespace bbque
