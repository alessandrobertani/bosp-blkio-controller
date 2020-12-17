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

#ifdef CONFIG_TARGET_OPENCL
#include "bbque/pp/opencl_platform_proxy.h"
#endif

#ifdef CONFIG_TARGET_NVIDIA
#include "bbque/pp/nvidia_platform_proxy.h"
#endif

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

#define CPU_QUOTA_TO_ALLOCATE 100
#define GPU_QUOTA_TO_ALLOCATE 10  // more than 1 app could run on GPU

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
		logger->Debug("TestSchedPol: built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
			FD("TestSchedPol: built new dynamic object [%p]\n"), (void *) this);
}

TestSchedPol::~TestSchedPol() { }

SchedulerPolicyIF::ExitCode_t TestSchedPol::_Init()
{
	PlatformManager & plm(PlatformManager::GetInstance());
	local_sys_id = plm.GetPlatformDescription().GetLocalSystem().GetId();

	if (this->sys_list.empty()) {
		auto sys_list = sys->GetResources("sys");
		for (auto const & sys_rsrc : sys_list) {
			sys_ids.push_back(sys_rsrc->ID());
		}
		logger->Info("Init: %d systems node(s) available", sys_list.size());
	}

	if (this->cpu_pe_list.empty()) {
		this->cpu_pe_list = sys->GetResources("sys.cpu.pe");
		logger->Info("Init: %d CPU core(s) available", cpu_pe_list.size());
	}

#ifdef CONFIG_TARGET_NVIDIA
	if (this->gpu_cuda_list.empty()) {
		this->gpu_cuda_list = sys->GetResources(BBQUE_NVIDIA_GPU_PATH);
		logger->Info("Init: %d NVIDIA GPU(s) available", gpu_cuda_list.size());
	}
#endif

#ifdef CONFIG_TARGET_OPENCL
	if (this->acc_opencl_list.empty()) {
		this->acc_opencl_list = sys->GetResources(BBQUE_OPENCL_ACC_PATH);
		logger->Info("Init: %d OpenCL GPU(s) available", acc_opencl_list.size());
	}
#endif

	// Load all the applications task graphs
	logger->Info("Init: loading the applications task graphs");
	fut_tg = std::async(std::launch::async, &System::LoadTaskGraphs, sys);

	// Applications count
	nr_apps = sys->SchedulablesCount(ba::Schedulable::READY);
	nr_apps += sys->SchedulablesCount(ba::Schedulable::RUNNING);
	logger->Info("Init: nr. active applications = %d", nr_apps);

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t TestSchedPol::Schedule(System & system,
						     RViewToken_t & status_view)
{
	SchedulerPolicyIF::ExitCode_t result = SCHED_DONE;

	// Class providing query functions for applications and resources
	sys = &system;
	result = Init();
	if (result != SCHED_OK)
		return result;

	fut_tg.get();

	// AEM integrated applications
	auto assign_awm_app = std::bind(static_cast<ExitCode_t (TestSchedPol::*)(ba::AppCPtr_t)>
					(&TestSchedPol::AssignWorkingMode),
					this, _1);

	ForEachApplicationToScheduleDo(assign_awm_app);
	logger->Debug("Schedule: done with applications");

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER

	// Not integrated processes
	auto assign_awm_proc = std::bind(static_cast<ExitCode_t (TestSchedPol::*)(ProcPtr_t)>
					(&TestSchedPol::AssignWorkingMode),
					this, _1);

	ForEachProcessToScheduleDo(assign_awm_proc);
	logger->Debug("Schedule: done with processes");

#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

	// Update the resource status view
	status_view = sched_status_view;
	logger->Debug("Schedule: status view id=%ld", status_view);
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
	auto pawm = std::make_shared<ba::WorkingMode>(0, "Run-time", 1, proc);

	// Resource request addition
	AddResourceRequests(proc, pawm);

	// Resource binding
	int32_t ref_num = -1;
	auto ret = DoResourceBinding(pawm, ref_num);
	if (ret != SCHED_OK) {
		logger->Warn("AssignWorkingMode: [%s] resource binding failed",
			proc->StrId());
		return ret;
	}

	// Schedule request
	ProcessManager & prm(ProcessManager::GetInstance());
	auto prm_ret = prm.ScheduleRequest(proc, pawm, sched_status_view, ref_num);
	if (prm_ret != ProcessManager::SUCCESS) {
		logger->Error("AssignWorkingMode: schedule request failed for [%s]",
			proc->StrId());
		return ExitCode_t::SCHED_SKIP_APP;
	}

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AddResourceRequests(ProcPtr_t proc, ba::AwmPtr_t pawm)
{
	// CPU quota
	// TODO: Consider the multi-socket
	std::string cpu_pe_path_str("sys.cpu0.pe");
	uint64_t cpu_pe_quota = proc->GetScheduleRequestInfo()->cpu_cores * 100;
	logger->Debug("AddResourceRequests: [%s] requested cpu_quota = %lu",
		proc->StrId(), cpu_pe_quota);

	if (cpu_pe_quota == 0) {
		// Priority-proportional CPU assignment (by default)
		cpu_pe_quota = GetPrioProportionalResourceQuota(
								cpu_pe_path_str,
								proc->Priority());
		logger->Info("AddResourceRequests: [%s] prio=%d amount of <%s> assigned = %lu",
			proc->StrId(),
			proc->Priority(),
			cpu_pe_path_str.c_str(),
			cpu_pe_quota);
		pawm->AddResourceRequest(cpu_pe_path_str,
					cpu_pe_quota,
					br::ResourceAssignment::Policy::BALANCED);
	}
	else {
		pawm->AddResourceRequest(cpu_pe_path_str,
					cpu_pe_quota,
					br::ResourceAssignment::Policy::BALANCED);
	}
	logger->Debug("AddResourceRequests: [%s] <%s> = %lu",
		proc->StrId(), cpu_pe_path_str, cpu_pe_quota);

	// GPUs
#ifdef CONFIG_TARGET_NVIDIA
	uint32_t gpu_quota = proc->GetScheduleRequestInfo()->gpu_units * GPU_QUOTA_TO_ALLOCATE;
	if (gpu_quota != 0) {
		pawm->AddResourceRequest("sys.gpu.pe",
					gpu_quota,
					br::ResourceAssignment::Policy::BALANCED);
		logger->Debug("AddResourceRequests: [%s] <%s> = %lu",
			proc->StrId(), BBQUE_NVIDIA_GPU_PATH, gpu_quota);
	}
#endif

#ifdef CONFIG_TARGET_OPENCL
	// Accelerators
	uint32_t acc_quota = proc->GetScheduleRequestInfo()->acc_cores * 100;
	if (acc_quota != 0) {
		pawm->AddResourceRequest(BBQUE_OPENCL_ACC_PATH,
					acc_quota,
					br::ResourceAssignment::Policy::BALANCED);
		logger->Debug("AddResourceRequests: [%s] <sys.grp0.acc.pe> = %d",
			proc->StrId(), acc_quota);
	}
#endif

	return SCHED_OK;
}

#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AssignWorkingMode(bbque::app::AppCPtr_t papp)
{
	if (papp == nullptr) {
		logger->Error("AssignWorkingMode: null application descriptor!");
		return SCHED_ERROR;
	}

	// Print the run-time profiling info if running
	if (papp->Running()) {
		auto prof = papp->GetRuntimeProfile();
		logger->Info("AssignWorkingMode: [%s] "
			"cpu_usage=%.2f, "
			"cycle_time=%dms, cycle_count=%d, "
			"cps_goal_gap=%d",
			papp->StrId(),
			prof.cpu_usage.curr,
			prof.cycle_time_ms,
			prof.cycle_count,
			prof.ggap_percent);
#ifdef CONFIG_BBQUE_ENERGY_MONITOR
		logger->Info("AssignWorkingMode: [%s] E=%luuJ EpC=%.2fJ",
			papp->StrId(), prof.energy.last_uj, prof.energy.epc / 1e6);
#endif
	}

	// Create or re-initialize the working mode data structure
	auto pawm = papp->CurrentAWM();
	if (pawm != nullptr) {
		logger->Debug("AssignWorkingMode: [%s] "
			"clearing the bindings of the previous assignment...",
			papp->StrId());
		pawm->ClearResourceBinding();
	}
	else {
		logger->Debug("AssignWorkingMode: [%s] "
			"creating a new working mode...",
			papp->StrId());
		pawm = std::make_shared<ba::WorkingMode>(
			papp->WorkingModes().size(),
			"Dynamic",
			1,
			papp);
	}

	ApplicationManager & am(ApplicationManager::GetInstance());

	int32_t ref_num = -1;
	auto ret = AssignResources(papp, pawm, ref_num);
	if (ret != SCHED_OK) {
		logger->Debug("AssignWorkingMode: [%s] resource binding failed",
			papp->StrId());
		am.NoSchedule(papp);
		return ret;
	}

	// Schedule request
	auto am_ret = am.ScheduleRequest(papp, pawm, sched_status_view, ref_num);
	if (am_ret != ApplicationManager::AM_SUCCESS) {
		logger->Error("AssignWorkingMode: [%s] schedule request failed",
			papp->StrId());
		return ExitCode_t::SCHED_SKIP_APP;
	}

	logger->Info("AssignWorkingMode: [%s] successfully scheduled", papp->StrId());
	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AssignResources(bbque::app::AppCPtr_t papp,
			      bbque::app::AwmPtr_t pawm,
			      int32_t & ref_num)
{

#ifdef CONFIG_BBQUE_TG_PROG_MODEL
	if (papp->Language() & RTLIB_LANG_TASKGRAPH) {
		return AssignResourcesWithTaskGraphMapping(papp, pawm, ref_num);
	}
#endif // CONFIG_BBQUE_TG_PROG_MODEL

	auto ret = AddResourceRequests(papp, pawm);
	if (ret != ExitCode_t::SCHED_OK) {
		return ret;
	}

	logger->Debug("AssignResources: [%s] performing resource binding...",
		papp->StrId());
	return DoResourceBinding(pawm, ref_num);
}


#ifdef CONFIG_BBQUE_TG_PROG_MODEL

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AssignResourcesWithTaskGraphMapping(bbque::app::AppCPtr_t papp,
						  bbque::app::AwmPtr_t pawm,
						  int32_t & ref_num)
{
	auto task_graph = papp->GetTaskGraph();
	if (task_graph == nullptr) {
		logger->Warn("AssignResourcesWithTaskGraphMapping: [%s] no task-graph to map",
			papp->StrId());
		return ExitCode_t::SCHED_ERROR_TASKGRAPH_MISSING;
	}
	logger->Debug("AssignResourcesWithTaskGraphMapping [%s] mapping the task graph...",
		papp->StrId());

//	unsigned short int task_index = 0; // task index
	unsigned short int sys_index  = 0; // system index

	PlatformManager & plm(PlatformManager::GetInstance());
	const auto systems = plm.GetPlatformDescription().GetSystemsAll();

	// Application-level runtime profiling information
	uint16_t throughput;
	uint32_t c_time;
	task_graph->GetProfiling(throughput, c_time);
	logger->Info("[%s] Task-graph throughput: %d [CPS],  ctime: %d [ms]",
		papp->StrId(), throughput, c_time);

	// Task mapping
	for (auto t_entry : task_graph->Tasks()) {
		auto & task(t_entry.second);
		auto const & task_reqs(papp->GetTaskRequirements(task->Id()));

		// Task-level runtime profiling information
		task->GetProfiling(throughput, c_time);
		logger->Info("[%s] <task: %d> throughput: %.2f / %.2f [CPS],  ctime: %d / %d [ms]",
			papp->StrId(), t_entry.first,
			static_cast<float> (throughput) / 100.0, task_reqs.Throughput(),
			c_time, task_reqs.CompletionTime());

		// System node
		task->SetAssignedSystem(sys_ids[sys_index]);
		auto const & ip_addr = systems.at(sys_ids[sys_index]).GetNetAddress();
		task->SetAssignedSystemIp(ip_addr);

		// Processing unit (CPU, GPU or ACCELERATOR)
		MapTaskToFirstPrefProcessor(task, task_reqs);

/*
		// Increment the system id index to dispatch the next task on a
		// different system node (for multi-system platform testing)
		++task_index;
		if (task_index > ceil( task_index / sys_ids.size() ))
			++sys_index;
*/
	}

	// Buffer allocation
	for (auto & b_entry : task_graph->Buffers()) {
		auto & buffer(b_entry.second);
		buffer->SetAssignedSystem(0);
		//buffer->SetiAssignedMemoryGroup(0);
		buffer->SetMemoryBank(0);
	}

	// Working mode filling
	pawm->AddResourcesFromTaskGraph(task_graph, ref_num);

	// Update task-graph on disk
	papp->SetTaskGraph(task_graph);
	logger->Info("[%s] Task-graph updated", papp->StrId());

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::MapTaskToFirstPrefProcessor(TaskPtr_t task, TaskRequirements const & task_reqs)
{
	for (auto const & arch : task_reqs.ArchPreferences()) {

		logger->Debug("MapTaskToFirstPrefProcessor: task=%d target preference=%s",
			task->Id(), GetStringFromArchType(arch));

		if (task->HasTarget(arch)) {
			logger->Info("MapTaskToFirstPrefProcessor: task=%d target=%s loaded",
				task->Id(), GetStringFromArchType(arch));

			// Transitional mapping of GN to CPU
			ArchType trans_arch;
			if (arch == ArchType::GN) {
				trans_arch = ArchType::CPU;
				logger->Debug("MapTaskToFirstPrefProcessor: task=%d target=%s (using CPU)",
					task->Id(), GetStringFromArchType(arch));
			}
			else {
				trans_arch = arch;
			}
			logger->Debug("MapTaskToFirstPrefProcessor: task=%d target=%s",
				task->Id(), GetStringFromArchType(trans_arch));

			if (!sys->ExistResourcePathsOfArch(trans_arch)) {
				logger->Debug("MapTaskToFirstPrefProcessor: task=%d target=%s not available",
					task->Id(), GetStringFromArchType(trans_arch));
				continue;
			}

			auto resource_path_list = sys->GetResourcePathListByArch(trans_arch);
			logger->Debug("MapTaskToFirstPrefProcessor: task=%d target=%s: %d options",
				task->Id(), GetStringFromArchType(trans_arch), resource_path_list.size());

			auto resource_path = GetFirstAvailable(resource_path_list, 100);

			br::ResourceType processor_type = br::GetResourceTypeFromArchitecture(trans_arch);
			logger->Debug("MapTaskToFirstPrefProcessor: task=%d target=%s is a <%s> resource",
				task->Id(),
				GetStringFromArchType(arch),
				br::GetResourceTypeString(processor_type));

			int processor_id = resource_path->GetID(processor_type);
			task->SetAssignedProcessor(processor_id);
			task->SetAssignedProcessingQuota(100);
			task->SetAssignedArch(arch);

			int processor_group = resource_path->GetID(br::ResourceType::GROUP);
			if (processor_group >= 0)
				task->SetAssignedProcessorGroup(processor_group);

			logger->Debug("MapTaskToFirstPrefProcessor: task=%d assigned target=%s id=%d "
				"group=%d ",
				task->Id(), GetStringFromArchType(arch), processor_id, processor_group);

			return SCHED_OK;
		}
		else {
			logger->Debug("MapTaskToFirstPrefProcessor: task=%d kernel for %s not loaded",
				task->Id(), GetStringFromArchType(arch));
		}
	}

	return SCHED_R_UNAVAILABLE;
}

#endif // CONFIG_BBQUE_TG_PROG_MODEL

br::ResourcePathPtr_t
TestSchedPol::GetFirstAvailable(std::list<br::ResourcePathPtr_t> const & resource_path_list,
				uint32_t amount) const
{
	for (auto const & rp : resource_path_list) {
		if (sys->ResourceAvailable(rp, sched_status_view) >= amount)
			return rp;
	}
	logger->Debug("GetFirstAvailable: resources in the list are fully allocated");
	return nullptr;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AddResourceRequests(bbque::app::AppCPtr_t papp,
				  bbque::app::AwmPtr_t pawm)
{
	logger->Debug("AddResourceRequests: [%s] adding resource request "
		"<sys.cpu.pe>",
		papp->StrId());

	// TODO: Consider the multi-socket
	std::string cpu_pe_path_str("sys.cpu0.pe");
	uint64_t cpu_pe_quota = GetPrioProportionalResourceQuota(cpu_pe_path_str,
								papp->Priority());

	logger->Info("AddResourceRequests: [%s] prio=%d amount of <%s> assigned = %4d",
		papp->StrId(),
		papp->Priority(),
		cpu_pe_path_str.c_str(),
		cpu_pe_quota);

	pawm->AddResourceRequest(cpu_pe_path_str,
				cpu_pe_quota,
				br::ResourceAssignment::Policy::BALANCED);

	logger->Debug("AddResourceRequests: [%s] language = %d",
		papp->StrId(), papp->Language());

#ifdef CONFIG_TARGET_NVIDIA
	// NVIDIA CUDA devices (GPUs)
	if (!gpu_cuda_list.empty() && (papp->Language() & RTLIB_LANG_CUDA)) {
		logger->Debug("AddResourceRequests: [%s] adding resource request <%s>",
			papp->StrId(), BBQUE_NVIDIA_GPU_PATH);
		pawm->AddResourceRequest(BBQUE_NVIDIA_GPU_PATH, GPU_QUOTA_TO_ALLOCATE);
	}
#endif

#ifdef CONFIG_TARGET_OPENCL
	// OpenCL applications are under a different resource path
	if (!acc_opencl_list.empty() && (papp->Language() & RTLIB_LANG_OPENCL)) {
		logger->Debug("AddResourceRequests: [%s] adding resource request <%s>",
			papp->StrId(), BBQUE_OPENCL_ACC_PATH);
		pawm->AddResourceRequest("sys.grp0.acc.pe", 100);
	}
#endif

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::DoResourceBinding(bbque::app::AwmPtr_t pawm,
				int32_t & ref_num)
{
	SchedulerPolicyIF::ExitCode_t ret;

	for (auto const & request_entry: pawm->GetResourceRequests()) {
		auto const & request_path(request_entry.first);
		auto const & request_amount(request_entry.second->GetAmount());

		auto resource_type = request_path->ParentType();
		switch (resource_type) {

		case br::ResourceType::CPU:
			ret = this->BindResourceToFirstAvailable(pawm,
								 this->cpu_pe_list,
								 br::ResourceType::CPU,
								 request_amount,
								 ref_num);
			if (ret != ExitCode_t::SCHED_OK) {
				logger->Debug("DoResourceBinding: [%s] resource binding failed",
					pawm->StrId());
				return ret;
			}
			break;

		case br::ResourceType::GPU:
			ret = this->BindResourceToFirstAvailable(pawm,
								 this->gpu_cuda_list,
								 br::ResourceType::GPU,
								 request_amount,
								 ref_num);
			break;

		case br::ResourceType::ACCELERATOR:
			ret = this->BindToFirstAvailableOpenCL(pawm,
							       br::ResourceType::ACCELERATOR,
							       request_amount,
							       ref_num);

		}
	}

	return ExitCode_t::SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::BindResourceToFirstAvailable(bbque::app::AwmPtr_t pawm,
					   br::ResourcePtrList_t const & bind_list,
					   br::ResourceType r_type,
					   uint64_t amount,
					   int32_t & ref_num)
{
	bool binding_done = false;

	for (auto const & resource : bind_list) {
		auto bind_id = resource->Path()->GetID(r_type);
		std::string resource_path_to_bind("sys"
						+ std::to_string(local_sys_id) + "."
						+ br::GetResourceTypeString(r_type)
						+ std::to_string(bind_id)
						+ ".pe");

		auto curr_quota_available = sys->ResourceAvailable(resource_path_to_bind,
								sched_status_view);
		logger->Debug("BindResourceToFirstAvailable: <sys.%s%d.pe> amount=%lu (available=%lu)",
			br::GetResourceTypeString(r_type),
			bind_id,
			amount,
			curr_quota_available);

		if (curr_quota_available >= amount) {
			if ((r_type == br::ResourceType::CPU) && (amount <= 100)) {
				BindToFirstAvailableProcessingElements(
								pawm,
								r_type,
								amount,
								bind_id,
								ref_num);
			}
			else {
				ref_num = pawm->BindResource(
							r_type,
							R_ID_ANY,
							bind_id,
							ref_num);
			}
			binding_done = true;
			logger->Debug("BindResourceToFirstAvailable: <%s> -> <%s> done",
				br::ResourcePathUtils::GetTemplate(resource_path_to_bind).c_str(),
				resource_path_to_bind.c_str());
			break;
		}
	}

	if (!binding_done) {
		logger->Debug("BindResourceToFirstAvailable: <%s> not available",
			br::GetResourceTypeString(r_type));
		return ExitCode::SCHED_R_UNAVAILABLE;
	}

	return ExitCode_t::SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::BindToFirstAvailableProcessingElements(bbque::app::AwmPtr_t pawm,
						     br::ResourceType r_type,
						     uint64_t amount,
						     int32_t r_bind_id,
						     int32_t & ref_num)
{
	br::ResourceBitset cpu_pes_bitset;
	auto amount_to_assign = amount;
	logger->Debug("BindToFirstAvailableProcessingElements: <%s> ...", pawm->StrId());

	for (auto & pe_rsrc : this->cpu_pe_list) {
		auto avail_amount = pe_rsrc->Available(nullptr, sched_status_view);
		if (avail_amount == 0)
			continue;
		amount_to_assign -= std::min(amount_to_assign, avail_amount);
		cpu_pes_bitset.Set(pe_rsrc->ID());
		if (amount_to_assign == 0) break;
	}

	ref_num = pawm->BindResource(
				r_type,
				R_ID_ANY,
				r_bind_id,
				ref_num,
				br::ResourceType::PROC_ELEMENT,
				&cpu_pes_bitset);

	if (ref_num > 0) return ExitCode_t::SCHED_OK;

	return ExitCode_t::SCHED_OK;

}

TestSchedPol::ExitCode_t
TestSchedPol::BindToFirstAvailableOpenCL(bbque::app::AwmPtr_t pawm,
					 br::ResourceType dev_type,
					 uint64_t amount,
					 int32_t & ref_num)
{
	uint32_t opencl_platform_id;
	br::ResourceBitset opencl_devs_bitset;
	logger->Debug("BindToFirstAvailableOpenCL: looking for an available OpenCL accelerator...");

	for (auto const & acc : this->acc_opencl_list) {
		auto acc_avail_amount = acc->Available(nullptr, this->sched_status_view);
		logger->Debug("BindToFirstAvailableOpenCL: group=%d acc_id=%d available=%lu",
			acc->Path()->GetID(br::ResourceType::GROUP),
			acc->Path()->GetID(br::ResourceType::ACCELERATOR),
			acc_avail_amount);
		if (amount > acc_avail_amount) {
			continue;
		}

		opencl_platform_id = acc->Path()->GetID(br::ResourceType::GROUP);
		opencl_devs_bitset.Set(acc->Path()->GetID(dev_type));
		logger->Debug("BindToFirstAvailableOpenCL: accelerator(s) bitset=%s",
			opencl_devs_bitset.ToString().c_str());

		ref_num = pawm->BindResource(br::ResourceType::GROUP,
					R_ID_ANY,
					opencl_platform_id,
					ref_num,
					dev_type,
					&opencl_devs_bitset);

		if (ref_num > 0) return ExitCode_t::SCHED_OK;
	}

	return ExitCode_t::SCHED_R_UNAVAILABLE;
}

} // namespace plugins

} // namespace bbque
