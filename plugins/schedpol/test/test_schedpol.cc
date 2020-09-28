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
#include "bbque/pp/nvml_platform_proxy.h"
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
		logger->Info("test: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
			FI("test: Built new dynamic object [%p]\n"), (void *) this);
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
	if (this->gpu_opencl_list.empty()) {
		this->gpu_opencl_list = sys->GetResources(BBQUE_OPENCL_GPU_PATH);
		logger->Info("Init: %d OpenCL GPU(s) available", gpu_opencl_list.size());
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

	fut_tg.get();

	// AEM integrated applications
	auto assign_awm_app = std::bind(
					static_cast<ExitCode_t (TestSchedPol::*)(ba::AppCPtr_t)>
					(&TestSchedPol::AssignWorkingMode),
					this, _1);

	ForEachApplicationToScheduleDo(assign_awm_app);
	logger->Debug("Schedule: done with applications");

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER

	// Not integrated processes
	auto assign_awm_proc = std::bind(
					static_cast<ExitCode_t (TestSchedPol::*)(ProcPtr_t)>
					(&TestSchedPol::AssignWorkingMode),
					this, _1);

	ForEachProcessToScheduleDo(assign_awm_proc);
	logger->Debug("Schedule: done with processes");

#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

	// Update the resource status view
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
	auto pawm = std::make_shared<ba::WorkingMode>(0, "Run-time", 1, proc);

	// Resource request addition
	AddResourceRequests(proc, pawm);

	// Resource binding
	int32_t ref_num;
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
	uint32_t cpu_quota = proc->GetScheduleRequestInfo()->cpu_cores * 100;
	logger->Debug("AddResourceRequests: [%s] requested cpu_quota = %d",
		proc->StrId(), cpu_quota);
	if (cpu_quota == 0) {
		pawm->AddResourceRequest("sys.cpu.pe", CPU_QUOTA_TO_ALLOCATE,
					br::ResourceAssignment::Policy::BALANCED);
		logger->Debug("AddResourceRequests: [%s] <sys.cpu.pe> = %lu",
			proc->StrId(), CPU_QUOTA_TO_ALLOCATE);
	}
	else {
		pawm->AddResourceRequest("sys.cpu.pe",
					cpu_quota,
					br::ResourceAssignment::Policy::BALANCED);
		logger->Debug("AddResourceRequests: [%s] <sys.cpu.pe> = %lu",
			proc->StrId(), cpu_quota);
	}

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

	// Accelerators
	uint32_t acc_quota = proc->GetScheduleRequestInfo()->acc_cores * 100;
	if (acc_quota != 0) {
		pawm->AddResourceRequest("sys.acc.pe",
					acc_quota,
					br::ResourceAssignment::Policy::BALANCED);
		logger->Debug("AddResourceRequests: [%s] <sys.acc.pe> = %d",
			proc->StrId(), acc_quota);
	}

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
			"cpu_usage=%d c_time=%d, ggap=%d [valid=%d]",
			papp->StrId(),
			prof.cpu_usage,
			prof.ctime_ms,
			prof.ggap_percent,
			prof.is_valid);
	}

	// Create or re-initialize the working mode data structure
	auto pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		logger->Debug("AssignWorkingMode: [%s] "
			"creating a new working mode...",
			papp->StrId());
		pawm = std::make_shared<ba::WorkingMode>(
			papp->WorkingModes().size(),
			"Dynamic",
			1,
			papp);

		AddResourceRequests(papp, pawm);
	}
	else {
		logger->Debug("AssignWorkingMode: [%s] "
			"clearing the bindings of the previous assignment...",
			papp->StrId());
		pawm->ClearResourceBinding();
	}

	ApplicationManager & am(ApplicationManager::GetInstance());

	// Resource binding
	int32_t ref_num = -1;
	logger->Debug("AssignWorkingMode: [%s] performing resource binding...",
		papp->StrId());
	auto ret = DoResourceBinding(pawm, ref_num);
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

#ifdef CONFIG_BBQUE_TG_PROG_MODEL
	// Task level mapping
	if (papp->Language() & RTLIB_LANG_TASKGRAPH)
		MapTaskGraph(papp);

#endif // CONFIG_BBQUE_TG_PROG_MODEL

	logger->Info("AssignWorkingMode: [%s] successfully scheduled",
		papp->StrId());

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::AddResourceRequests(bbque::app::AppCPtr_t papp,
				  bbque::app::AwmPtr_t pawm)
{
	logger->Debug("AddResourceRequests: [%s] adding resource request "
		"<sys.cpu.pe>",
		papp->StrId());

	pawm->AddResourceRequest("sys.cpu.pe",
				CPU_QUOTA_TO_ALLOCATE,
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
	if (!gpu_opencl_list.empty() && (papp->Language() & RTLIB_LANG_OPENCL)) {
		logger->Debug("AddResourceRequests: [%s] adding resource request <%s>",
			papp->StrId(), BBQUE_OPENCL_GPU_PATH);
		pawm->AddResourceRequest(BBQUE_OPENCL_GPU_PATH, GPU_QUOTA_TO_ALLOCATE);
	}
#endif

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::DoResourceBinding(bbque::app::AwmPtr_t pawm,
				int32_t & ref_num)
{
	auto ret = this->BindResourceToFirstAvailable(pawm,
						this->cpu_pe_list,
						br::ResourceType::CPU,
						pawm->GetRequestedAmount("sys.cpu.pe"),
						ref_num);
	if (ret != ExitCode_t::SCHED_OK) {
		logger->Debug("DoResourceBinding: [%s] resource binding failed",
			pawm->StrId());
		return ret;
	}

#ifdef CONFIG_TARGET_NVIDIA
	auto gpu_cuda_amount = pawm->GetRequestedAmount(BBQUE_NVIDIA_GPU_PATH);
	if (gpu_cuda_amount > 0) {
		ret = this->BindResourceToFirstAvailable(pawm,
							this->gpu_cuda_list,
							br::ResourceType::GPU,
							gpu_cuda_amount,
							ref_num);
	}
#endif

#ifdef CONFIG_TARGET_OPENCL
	auto gpu_opencl_amount = pawm->GetRequestedAmount(BBQUE_OPENCL_GPU_PATH);
	if (gpu_opencl_amount > 0) {
		ret = this->BindToFirstAvailableOpenCL(pawm,
						br::ResourceType::GPU,
						gpu_opencl_amount,
						ref_num);
	}
#endif

	return ExitCode_t::SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
TestSchedPol::BindResourceToFirstAvailable(bbque::app::AwmPtr_t pawm,
					   br::ResourcePtrList_t const & r_list,
					   br::ResourceType r_type,
					   uint64_t amount,
					   int32_t & ref_num)
{
	bool binding_done = false;

	for (auto const & resource : r_list) {
		auto bind_id = resource->Path()->GetID(r_type);
		std::string resource_path_to_bind("sys"
						+ std::to_string(local_sys_id) + "."
						+ br::GetResourceTypeString(r_type) + std::to_string(bind_id)
						+ ".pe");

		auto curr_quota_available = sys->ResourceAvailable(resource_path_to_bind,
								sched_status_view);
		logger->Debug("DoResourceBinding: <sys.%s%d.pe> available: %lu",
			br::GetResourceTypeString(r_type),
			bind_id,
			curr_quota_available);

		if (curr_quota_available >= amount) {
			if (r_type == br::ResourceType::CPU) {
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
			logger->Debug("DoResourceBinding: <%s> -> <%s> done",
				br::ResourcePathUtils::GetTemplate(resource_path_to_bind).c_str(),
				resource_path_to_bind.c_str());
			break;
		}
	}

	if (!binding_done) {
		logger->Debug("DoResourceBinding: <%s> not available",
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

	for (auto const & ocl_gpu : this->gpu_opencl_list) {
		if (amount > ocl_gpu->Available(nullptr, this->sched_status_view)) {
			continue;
		}

		opencl_platform_id = ocl_gpu->Path()->GetID(br::ResourceType::GROUP);
		opencl_devs_bitset.Set(ocl_gpu->Path()->GetID(dev_type));

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
#ifdef CONFIG_BBQUE_TG_PROG_MODEL

void TestSchedPol::MapTaskGraph(bbque::app::AppCPtr_t papp)
{
	auto task_graph = papp->GetTaskGraph();
	if (task_graph == nullptr) {
		logger->Warn("AssignWorkingMode: [%s] no task - graph to map",
			papp->StrId());
		return;
	}
	logger->Info("MapTaskGraph: [%s] mapping the task graph...", papp->StrId());

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
