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

#include "manga_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "bbque/binding_manager.h"
#include "bbque/modules_factory.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/assert.h"
#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "tg/task_graph.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

#ifndef CONFIG_TARGET_LINUX_MANGO
#error "MangA policy must be compiled only for Linux Mango target"
#endif

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * MangASchedPol::Create(PF_ObjectParams *)
{
	return new MangASchedPol();
}

int32_t MangASchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (MangASchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * MangASchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

MangASchedPol::MangASchedPol():
	cm(ConfigurationManager::GetInstance()),
	ra(ResourceAccounter::GetInstance()),
	rmv(ResourcePartitionValidator::GetInstance())
{
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	if (logger)
		logger->Info("manga: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
		        FI("manga: Built new dynamic object [%p]\n"), (void *)this);
}


MangASchedPol::~MangASchedPol()
{

}


SchedulerPolicyIF::ExitCode_t MangASchedPol::Init()
{
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


	// Get the amount of processing elements from each accelerator
	if (pe_per_acc.empty()) {

		// Get the number of HN clusters (groups)
		auto groups = sys->GetResources("sys.grp");
		logger->Debug("Init: # clusters: %d", groups.size());
		for (auto & hn_cluster: groups) {
			// Cluster info
			auto cluster_id = hn_cluster->ID();
			std::string cluster_path(
			    std::string("sys.grp") + std::to_string(cluster_id));

			// Nr. of accelerators
			std::string acc_path(cluster_path + std::string(".acc"));
			logger->Debug("Init: accelerators path: <%s>", acc_path.c_str());
			auto accs = sys->GetResources(acc_path);
			pe_per_acc.emplace(cluster_id, std::vector<uint32_t>(accs.size()));
			logger->Debug("Init: cluster=<%d>: # accelerators %d",
			              cluster_id, accs.size());

			// Nr. of cores per accelerator
			for (br::ResourcePtr_t const & acc: accs) {
				std::string pe_binding_path(
				    acc_path + std::to_string(acc->ID()) + std::string(".pe"));
				logger->Debug("Init: binding resource path: <%s>",
				              pe_binding_path.c_str());

				pe_per_acc[cluster_id][acc->ID()] = sys->ResourceTotal(pe_binding_path) / 100 ;
				logger->Debug("Init: <%s> #proc_element=%d",
				              pe_binding_path.c_str(), pe_per_acc[cluster_id][acc->ID()]);
			}
		}
	}

	// Load the application task graphs
	logger->Debug("Init: loading the applications task graphs");
	fut_tg = std::async(std::launch::async, &System::LoadTaskGraphs, sys);

	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t
MangASchedPol::Schedule(
    System & system,
    RViewToken_t & status_view)
{
	SchedulerPolicyIF::ExitCode_t err, result = SCHED_DONE;

	// Class providing query functions for applications and resources
	sys = &system;
	Init();
	fut_tg.get();

	uint32_t nr_clusters = pe_per_acc.size();
	logger->Debug("Schedule: HN clusters available = %d", nr_clusters);
	assert (nr_clusters > 0);
	if (nr_clusters <= 0) {
		logger->Fatal("Schedule: no HW clusters available: platform not initialized?");
		return SCHED_ERROR;
	}

	// Re-assing resources to running applications: no reconfiguration
	// supported by MANGO platform
	bbque::app::AppCPtr_t papp;
	AppsUidMapIt app_it;
	papp = sys->GetFirstRunning(app_it);
	for (; papp; papp = sys->GetNextReady(app_it)) {
		logger->Debug("Schedule: [%s] is RUNNING -> rescheduling", papp->StrId());
		err = ServeApp(papp);
		if (err == SCHED_ERROR) {
			logger->Crit("Schedule: [%s] unexpected error [err=%d]",
			             papp->StrId(), err);
			return SCHED_ERROR;
		}
	}

	for (AppPrio_t priority = 0; priority <= sys->ApplicationLowestPriority(); priority++) {
		// Checking if there are applications at this priority
		if (!sys->HasApplications(priority))
			continue;

		logger->Debug("Schedule: serving applications with priority %d", priority);
		err = ServeApplicationsWithPriority(priority);
		if (err == SCHED_R_UNAVAILABLE) {
			// We have finished the resources, suspend all other
			// apps and returns gracefully
			result = SCHED_DONE;
			break;
		} else if (err != SCHED_OK) {
			logger->Error("Schedule: unexpected error: %d", err);
			result = err;
			break;
		}
	}

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	return result;
}

SchedulerPolicyIF::ExitCode_t
MangASchedPol::ServeApplicationsWithPriority(int priority) noexcept {
	ExitCode_t err = SCHED_OK;
	ApplicationManager & am(ApplicationManager::GetInstance());
	ba::AppCPtr_t  papp;
	AppsUidMapIt app_iterator;

	// Get all the applications @ this priority
	papp = sys->GetFirstWithPrio(priority, app_iterator);
	for (; papp; papp = sys->GetNextWithPrio(priority, app_iterator)) {
		logger->Debug("ServeApplicationsWithPriority: [%s] looking for resources...",
		papp->StrId());

		// Skip disabled
		if (papp->Disabled()) {
			logger->Debug("ServeApplicationsWithPriority: [%s] DISABLED: skip...",
			papp->StrId());
			continue;
		}

		// Skip already scheduled running applications
		if (papp->Running()) {
			logger->Debug("ServeApplicationsWithPriority: [%s] RUNNING: skip...",
			              papp->StrId());
			continue;
		}

		// Assign working mode and set platform partition
		err = ServeApp(papp);
		if (err == SCHED_OK) {
			// Everything OK
			logger->Info("ServeApplicationsWithPriority: [%s] successfully scheduled",
			             papp->StrId());
		} else if (err == SCHED_SKIP_APP) {
			// In this case we have no sufficient memory to start it, the only
			// one thing to do is to ignore it
			logger->Warn("ServeApplicationsWithPriority: [%s] missing information",
			             papp->StrId());
			am.NoSchedule(papp);
			err = SCHED_OK;
		} else if (err == SCHED_R_UNAVAILABLE) {
			// In this case we have no bandwidth feasibility, so we can try to
			// fairly reduce the bandwidth for non strict applications.
			logger->Warn("ServeApplicationsWithPriority: [%s] not enough resources",
			             papp->StrId());
			am.NoSchedule(papp);
			err = SCHED_OK;
		} else if (err != SCHED_OK) {
			// It returns  error exit code in case of error.
			logger->Error("ServeApplicationsWithPriority: [%s] unexpected error [err=%d]",
			              papp->StrId(), err);
			return err;
		}
	}

	return err;
}


SchedulerPolicyIF::ExitCode_t MangASchedPol::ServeApp(ba::AppCPtr_t papp) noexcept {
	ExitCode_t err = SCHED_OK;

	// Running applications must be rescheduled as is
	if (papp->Running()) {
		logger->Debug("ServeApp: [%s] is RUNNING -> re-assign working mode",
		papp->StrId());
		return ReassignWorkingMode(papp);
	}

	uint32_t curr_cluster_id = 0;
	uint32_t nr_clusters = pe_per_acc.size();

	for (; curr_cluster_id < nr_clusters; curr_cluster_id++) {
		logger->Debug("ServeApp: [%s] looking for a partition in HN cluster %d",
		papp->StrId(), curr_cluster_id);

		// First of all we have to decide which processor type to assign to each task
		err = InitTaskGraphMappingOptions(papp);
		if (err != SCHED_OK) {
			logger->Warn("ServeApp: [%s] not schedulable", papp->StrId());
			return err;
		}

		// Initialize vector for task mapping exploration
		uint32_t first_task_id = papp->GetTaskGraph()->Tasks().begin()->first;
		std::vector<int> arch_index(papp->GetTaskGraph()->TaskCount()+1, 0);

		// HN clusters
		std::list<Partition> partitions;

		// Iterate over the resource mapping options
		for (; err != SCHED_OPT_OVER; err = NextTaskGraphMappingOption(papp, arch_index, first_task_id)) {

			// Look for resource allocation partitions
			auto rmv_err = rmv.LoadPartitions(*papp->GetTaskGraph(), partitions, curr_cluster_id);
			if (rmv_err == ResourcePartitionValidator::PMV_OK) {
				logger->Debug("ServeApp: [%s] HW partitions found: %d  [HN cluster=%d]",
				              papp->StrId(), partitions.size(), curr_cluster_id);

				// Try to schedule the application, by allocating one of them
				err = ScheduleApplication(papp, partitions);
				if (err == SCHED_R_UNAVAILABLE) {
					logger->Debug("ServeApp: [%s] trying another mapping [HN cluster=%d]",
					              papp->StrId(), curr_cluster_id);
					continue;
				}
				else {
					logger->Debug("ServeApp: [%s] scheduled [HN cluster=%d]",
						papp->StrId(), curr_cluster_id);
				}
				return err;
			}
			// No partitions found for the current resource mappinf option
			else if (rmv_err == ResourcePartitionValidator::PMV_SKIMMER_FAIL) {
				logger->Debug("ServeApp: [%s] at least one skimmer failed"
				              " [HN cluster=%d]",
				              papp->StrId(), curr_cluster_id);
			} else if (rmv_err == ResourcePartitionValidator::PMV_NO_PARTITION) {
				logger->Debug("ServeApp: [%s] no HW partitions available"
				              " [HN cluster=%d]",
				              papp->StrId(), curr_cluster_id);
			} else {
				// Ehi what's happened here?
				logger->Fatal("ServeApp: [%s] unexpected error while retrieving"
				              " partitions", papp->StrId());
				return SCHED_ERROR;
			}
		}

		// Not schedulable resource mapping options
		err = SCHED_R_UNAVAILABLE;
	}

	return err;
}

SchedulerPolicyIF::ExitCode_t MangASchedPol::DealWithNoPartitionFound(ba::AppCPtr_t papp) noexcept {
	UNUSED(papp);
	switch(rmv.GetLastFailed()) {
	case PartitionSkimmer::SKT_MANGO_HN:
		// In this case we can try to reduce the allocated
		// bandwidth
	case PartitionSkimmer::SKT_MANGO_MEMORY_MANAGER:
		// We have no sufficient memory to run the application
		return SCHED_R_UNAVAILABLE;
	case PartitionSkimmer::SKT_MANGO_POWER_MANAGER:
		// Strict thermal constraints must not violated - do
		// not schedule the application
	default:
		return SCHED_SKIP_APP;
	}
}


std::string ArchMappingString(TaskMap_t & task_map)
{
	std::string mapping_opt_str;
	for (auto & t_entry: task_map) {
		auto & t_id = t_entry.first;
		mapping_opt_str += "arch mapping = { " + std::to_string(t_id) + ":"
		                   + GetStringFromArchType(t_entry.second->GetAssignedArch())
		                   + " ";
	}
	mapping_opt_str += "}";
	return mapping_opt_str;
}

SchedulerPolicyIF::ExitCode_t MangASchedPol::InitTaskGraphMappingOptions(
    ba::AppCPtr_t papp) noexcept {

	if (nullptr == papp->GetTaskGraph()) {
		logger->Error("InitTaskGraphMappingOptions: [%s] task-graph not available",
		papp->StrId());
		return SCHED_SKIP_APP;
	}

	auto & task_map = papp->GetTaskGraph()->Tasks();
	logger->Debug("InitTaskGraphMappingOptions: nr_tasks=%d", task_map.size());

	for (auto & task_pair: task_map) {
		auto & task = task_pair.second;
		const auto & task_reqs = papp->GetTaskRequirements(task->Id());

		if (task_reqs.NumArchPreferences() == 0) {
			logger->Error("InitTaskGraphMappingOptions: [%s] task %d"
			" has no target architecture - check the recipe",
			papp->StrId(), task->Id());
			return SCHED_SKIP_APP;
		}
		logger->Debug("InitTaskGraphMappingOptions: [%s] task %d"
		              " has %d arch options",
		              papp->StrId(), task->Id(), task_reqs.NumArchPreferences());

		// Target processor architecture
		auto target_unit_arch = task_reqs.ArchPreference(0);
		logger->Debug("InitTaskGraphMappingOptions: [%s] task %d "
		              "target [arch=%s (%d)] option",
		              papp->StrId(), task->Id(),
		              GetStringFromArchType(target_unit_arch),
		              target_unit_arch);
		task->SetAssignedArch(target_unit_arch);

		// Interconnect bandwidth requirements
		logger->Debug("InitTaskGraphMappingOptions: [%s] task %d "
		              "ctime=%dms bandwidth: in=%d, out=%d (Kbps)",
		              papp->StrId(), task->Id(),
		              task_reqs.CompletionTime(),
		              task_reqs.GetAssignedBandwidth().in_kbps,
		              task_reqs.GetAssignedBandwidth().out_kbps);
		task->SetAssignedBandwidth(task_reqs.GetAssignedBandwidth());
	}

	logger->Debug("InitTaskGraphMappingOptions: %s", ArchMappingString(task_map).c_str());

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t MangASchedPol::NextTaskGraphMappingOption(
    ba::AppCPtr_t papp,
    std::vector<int> & arch_index,
    uint32_t task_id)
{

	if (papp->GetTaskGraph() == nullptr) {
		logger->Error("NextTaskGraphMappingOption: [%s] task-graph missing."
		              " Corrupted?", papp->StrId());
		return SCHED_SKIP_APP;
	}

	auto & task_map = papp->GetTaskGraph()->Tasks();
	ExitCode_t ret = SCHED_OK;

	// if the task id does not exist we reached the end
	if (task_map.find(task_id) == task_map.end()) {
		logger->Debug("NextTaskGraphMappingOption: task_id=%d"
		              " not valid - more mapping options? (nr_tasks=%d)",
		              task_id, task_map.size());
		return SCHED_OPT_OVER;
	}

	auto & curr_task = task_map[task_id];
	auto & arch_id = arch_index[task_id];
	const auto & task_reqs = papp->GetTaskRequirements(task_id);

	// Is it the architecture id a valid option?
	if (task_reqs.ArchPreference(arch_id) == ArchType::NONE) {
		logger->Debug("NextTaskGraphMappingOption: task_id=%d arch_id=%d not valid",
		              task_id, arch_id);
		return SCHED_OPT_OVER;
	}

	logger->Debug("NextTaskGraphMappingOption: task_id=%d: current arch=%d [%s]",
	              task_id, arch_id,
	              GetStringFromArchType(task_reqs.ArchPreference(arch_id)));

	// Next architecture option
	if (size_t(arch_id) <= (task_reqs.NumArchPreferences()-2)) {
		arch_id++;
		curr_task->SetAssignedArch(task_reqs.ArchPreference(arch_id));
		logger->Debug("NextTaskGraphMappingOption: task_id=%d: next arch=%d [%s]",
		              task_id, arch_index[task_id],
		              GetStringFromArchType(task_reqs.ArchPreference(arch_id)));
	}
	// Last option of current task => 'carry' effect
	else {
		logger->Debug("NextTaskGraphMappingOption: task_id=%d: arch %d >= %d",
		              task_id, arch_id, (task_reqs.NumArchPreferences()-2));

		arch_index[task_id] = 0;
		task_id++;
		logger->Debug("NextTaskGraphMappingOption: task_id=%d next...", task_id);
		ret = NextTaskGraphMappingOption(papp, arch_index, task_id);
	}

	logger->Debug("NextTaskGraphMappingOption: %s",
	              ArchMappingString(task_map).c_str());
	return ret;
}


SchedulerPolicyIF::ExitCode_t
MangASchedPol::ScheduleApplication(
    ba::AppCPtr_t papp, const std::list<Partition> &partitions) noexcept {
	// Order the HW resources partition
	auto ret = SortPartitions(papp, partitions);
	if (ret != SCHED_OK) {
		logger->Error("ScheduleApplication: [%s] partition ordering failed",
		papp->StrId());
		return ret;
	}

	// Select/build a working mode
	auto tg = papp->GetTaskGraph();
	for (auto & selected_partition: partitions) {
		logger->Debug("ScheduleApplication: [%s] selecting AWM for partition %d",
		papp->StrId(), selected_partition.GetId());

		// Set a working mode an perform a schedule request
		ret = SelectWorkingMode(papp, selected_partition);
		if (ret == SCHED_OK) {
			logger->Debug("ScheduleApplication: [%s] selected partition %d",
			papp->StrId(), selected_partition.GetId());

			// Assign a resource partition of the MANGO platform
			papp->SetPartition(std::make_shared<Partition>(selected_partition));

			// Make the change effective
			auto pret = rmv.PropagatePartition(*tg, selected_partition);
			if (pret != ResourcePartitionValidator::ExitCode_t::PMV_OK) {
				logger->Error("ScheduleApplication: partition propagation"
				" failed [error=%d]", pret);
				// Abort the the schedule request
				ApplicationManager & am(ApplicationManager::GetInstance());
				am.ScheduleRequestAbort(papp, sched_status_view);
				ret = SCHED_R_UNAVAILABLE;
			} else {
				// Update task-graph info
				logger->Debug("ScheduleApplication: [%s] updating task-graph mapping...",
				              papp->StrId());
				papp->SetTaskGraph(tg);
				return SCHED_OK;
			}
		}
		else if (ret == SCHED_R_UNAVAILABLE) {
			logger->Warn("ScheduleApplication: [%s] not enough resources"
				" for partition %d", papp->StrId(), selected_partition.GetId());
		}
		else {
			logger->Warn("ScheduleApplication: [%s] not schedulable", papp->StrId());
			break;
		}
	}

	logger->Notice("ScheduleApplication: [%s] ret=%d", papp->StrId(), ret);
	return ret;
}


SchedulerPolicyIF::ExitCode_t
MangASchedPol::SortPartitions(ba::AppCPtr_t papp, const std::list<Partition> &partitions) noexcept {
	UNUSED(papp);
	bbque_assert(partitions.size() > 0);
	logger->Warn("SortPartitions: sorting the list of partitions... (TODO)");
	// TODO: Intelligent policy. For the demo just select the first
	// partition
	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t
MangASchedPol::SelectWorkingMode(
    ba::AppCPtr_t papp,
    const Partition & selected_partition) noexcept {
	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(
		    papp->WorkingModes().size(),"Run-time", 1, papp);
	}
	pawm->ClearResourceBinding();
	pawm->ClearResourceRequests();
	logger->Info("SelectWorkingMode: [%s] partition mapping...", papp->StrId());
	int32_t ref_num = -1;

	// Now we will update the Resource Accounter in order to trace the resource allocation
	// This has no effect in the platform resource assignment, since the effective
	// assignment was performed (by the platform proxy) during the PropagatePartition

	uint32_t nr_cores = 1;
	auto tg = papp->GetTaskGraph();
	uint32_t cluster_id = selected_partition.GetClusterId();

	for (auto & task_entry: tg->Tasks()) {
		auto & task(task_entry.second);

		// System node id
		auto sys_rsrc = ra.GetResources("sys");
		task->SetAssignedSystem(sys_rsrc.front()->ID());
		logger->Debug("SelectWorkingMode: task=%d system=%d",
			task->Id(),
			task->GetAssignedSystem());

		// Tile id
		uint32_t tile_id = selected_partition.GetUnit(task);

		// Nr. cores: if thread count not specified, assign all the
		// available cores
		logger->Debug("SelectWorkingMode: task=%d thread_count=%d",
			task->Id(),
			task->GetThreadCount());
		if (task->GetThreadCount() <= 0) {
			nr_cores = pe_per_acc[cluster_id][tile_id];
			logger->Debug("SelectWorkingMode: task=%d nr_cores=%d",
				task->Id(), nr_cores);
		}
		task->SetAssignedCoresCount(nr_cores);

		// Resource request of processing resources (cluster binding
		// already set in)
		std::string acc_path("sys.grp");
		acc_path += std::to_string(cluster_id);
		acc_path += ".acc" + std::to_string(tile_id) + ".pe";
		pawm->AddResourceRequest(
				acc_path,
				100 * nr_cores,
				br::ResourceAssignment::Policy::BALANCED);
		logger->Debug("SelectWorkingMode: [%s] task=%d add accelerator requested...",
		              papp->StrId(), task->Id());

		// Resource binding: HN tile (accelerator)
		logger->Debug("SelectWorkingMode: [%s] task=%d bind accelerator request to "
		              "tile=%d",
		              papp->StrId(), task->Id(), tile_id);
		ref_num = pawm->BindResource(
		              br::ResourceType::ACCELERATOR,
			      tile_id,
		              tile_id,
		              ref_num);

		logger->Debug("SelectWorkingMode: [%s] task=%d -> cluster=<%d> tile=<%d>",
		              papp->StrId(), task->Id(),
		              cluster_id,
		              tile_id);
	}

	// Amount of memory to allocate per bank
	std::map<uint32_t, uint64_t> mem_req_amount;

	// Accouting of memory resources
	for (auto buff : tg->Buffers()) {
		uint32_t mem_bank_id = selected_partition.GetMemoryBank(buff.second);
		mem_req_amount[mem_bank_id] += buff.second->Size();
		logger->Debug("SelectWorkingMode: buffer=%d -> memory bank %d (id=%d)",
		              buff.first,
		              selected_partition.GetMemoryBank(buff.second),
		              mem_bank_id);
	}

	for (auto &mem_entry: mem_req_amount) {
		auto & mem_bank_id = mem_entry.first;
		auto & mem_amount  = mem_entry.second;

		// Resource request for memory (set cluster and the bank id yet)
		std::string mem_path("sys.grp");
		mem_path += std::to_string(cluster_id);
		mem_path += ".mem" + std::to_string(mem_bank_id);
		pawm->AddResourceRequest(mem_path, mem_amount);
		logger->Debug("SelectWorkingMode: [%s] memory bank %d (requested=%d)",
		              papp->StrId(), mem_bank_id, mem_amount);

		// Resource binding: memory node
		ref_num = pawm->BindResource(
		              br::ResourceType::MEMORY,
		              mem_bank_id,
		              mem_bank_id,
		              ref_num);
	}

	// Update the accounting of resources with a schedule request
	ApplicationManager & am(ApplicationManager::GetInstance());
	auto ret = am.ScheduleRequest(papp, pawm, sched_status_view, ref_num);
	if (ret == ApplicationManager::AM_SUCCESS) {
		logger->Info("SelectWorkingMode: [%s] schedule request accepted", papp->StrId());
		return SCHED_OK;
	} else if ((ret == ApplicationManager::AM_APP_BLOCKING) ||
	           (ret == ApplicationManager::AM_APP_DISABLED)) {
		logger->Debug("SelectWorkingMode: [%s] schedule request for blocking/disabled",
		              papp->StrId());
		return SCHED_SKIP_APP;
	} else if (ret == ApplicationManager::AM_AWM_NOT_SCHEDULABLE) {
		logger->Debug("SelectWorkingMode: [%s] schedule request not allocable",
		              papp->StrId());
		return SCHED_R_UNAVAILABLE;
	} else {
		logger->Error("SelectWorkingMode: [%s] schedule request unexpected error", papp->StrId());
		return SCHED_ERROR;
	}
}


SchedulerPolicyIF::ExitCode_t
MangASchedPol::ReassignWorkingMode(ba::AppCPtr_t papp) noexcept {
	ApplicationManager & am(ApplicationManager::GetInstance());
	auto ret = am.ScheduleRequestAsPrev(papp, sched_status_view);
	if (ret != ApplicationManager::AM_SUCCESS) {
		logger->Error("ReassignWorkingMode: [%s] rescheduling failed", papp->StrId());
		return SCHED_SKIP_APP;
	}

	logger->Info("ReassignWorkingMode: [%s] rescheduling done", papp->StrId());
	return SCHED_OK;
}


} // namespace plugins

} // namespace bbque
