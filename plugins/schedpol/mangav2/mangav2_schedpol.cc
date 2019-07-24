/*
 * Copyright (C) 2019  Politecnico di Milano
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

#include "mangav2_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>

#include "bbque/modules_factory.h"
#include "bbque/utils/logging/logger.h"

#include "bbque/app/recipe.h"
#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "tg/task_graph.h"


#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * ManGAv2SchedPol::Create(PF_ObjectParams *)
{
	return new ManGAv2SchedPol();
}

int32_t ManGAv2SchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (ManGAv2SchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * ManGAv2SchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

ManGAv2SchedPol::ManGAv2SchedPol():
	cm(ConfigurationManager::GetInstance()),
	ra(ResourceAccounter::GetInstance())
{
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	if (logger)
		logger->Info("mangav2: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
		        FI("mangav2: Built new dynamic object [%p]\n"), (void *)this);
}


ManGAv2SchedPol::~ManGAv2SchedPol()
{
	pe_per_acc.clear();
}


SchedulerPolicyIF::ExitCode_t ManGAv2SchedPol::Init()
{
	// Build a string path for the resource state view
	std::string token_path(MODULE_NAMESPACE);
	++status_view_count;
	token_path.append(std::to_string(status_view_count));
	logger->Debug("Init: getting new resource state view token from <%s>",
	              token_path.c_str());

	// Get a fresh resource status view
	ResourceAccounterStatusIF::ExitCode_t ra_result =
	    ra.GetView(token_path, sched_status_view);
	if (ra_result != ResourceAccounterStatusIF::RA_SUCCESS) {
		logger->Fatal("Init: cannot get a resource state view");
		return SCHED_ERROR_VIEW;
	}
	logger->Debug("Init: resources state view token: %ld", sched_status_view);

	// Load the application task graphs
	logger->Debug("Init: loading the applications task graphs");
	fut_tg = std::async(std::launch::async, &System::LoadTaskGraphs, sys);

	// Get the amount of processing elements from each accelerator
	if (pe_per_acc.empty()) {
		// Get the number of HN clusters (groups)
		auto clusters = sys->GetResources("sys.grp");
		logger->Debug("Init: # clusters: %d", clusters.size());
		for (auto & hn_cluster: clusters) {

			// Cluster info
			auto cluster_id = hn_cluster->ID();
			std::string cluster_path(
			    std::string("sys.grp") + std::to_string(cluster_id));

			// Nr. of accelerators
			std::string acc_path(cluster_path + std::string(".acc"));
			logger->Debug("Init: accelerators path: <%s>", acc_path.c_str());
			auto accs = sys->GetResources(acc_path);
			pe_per_acc.emplace(cluster_id, std::vector<uint32_t>(accs.size()));
			arch_per_acc.emplace(cluster_id, std::vector<ArchType>(accs.size()));
			logger->Debug("Init: cluster=<%d>: # accelerators %d",
			              cluster_id, accs.size());

			// Nr. of cores per accelerator
			for (br::ResourcePtr_t const & acc: accs) {
				std::string pe_binding_path(
				    acc_path + std::to_string(acc->ID()) + std::string(".pe"));
				logger->Debug("Init: binding resource path: <%s>",
				              pe_binding_path.c_str());

				auto rsrc_pes = sys->GetResources(pe_binding_path);
				auto & first_pe = rsrc_pes.front();

				pe_per_acc[cluster_id][acc->ID()] = rsrc_pes.size();
				logger->Debug("Init: <%s> #proc_element=%d arch=%s",
				              pe_binding_path.c_str(),
				              pe_per_acc[cluster_id][acc->ID()],
					      first_pe->Model().c_str());

				arch_per_acc[cluster_id][acc->ID()] =
					GetArchTypeFromString(first_pe->Model());
			}
		}
	}

	// Test and profiling purposes: force a mapping choice
	std::ifstream fmf("/tmp/bbque_manga_force");
	while (fmf.good()) {
		char buff[5];
		if (fmf.getline(buff, 5)) {
			int m_id = atoi(buff);
			forced_mapping_ids.push_back(m_id);
			logger->Notice("Init: [%d] forced to select mapping id=%d",
				forced_mapping_ids.size()-1,
				m_id);
		}
	}

	fmf.close();

	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t ManGAv2SchedPol::Schedule(
    System & system,
    RViewToken_t & status_view)
{
	// Class providing query functions for applications and resources
	sys = &system;
	Init();
	fut_tg.get();

	// Re-assing resources to running applications: no reconfiguration
	// supported by MANGO platform
	ExitCode_t result;
	bbque::app::AppCPtr_t papp;
	AppsUidMapIt app_it;

	papp = sys->GetFirstRunning(app_it);
	for (; papp; papp = sys->GetNextReady(app_it)) {
		logger->Debug("Schedule: [%s] is RUNNING -> rescheduling", papp->StrId());
		result = ReassignWorkingMode(papp);
		if (result == SCHED_ERROR) {
			logger->Crit("Schedule: [%s] unexpected error [err=%d]",
				     papp->StrId(), result);
			forced_mapping_ids.clear();
			return SCHED_ERROR;
		}
		++apps_count;
	}

	for (AppPrio_t priority = 0; priority <= sys->ApplicationLowestPriority(); priority++) {
		if (!sys->HasApplications(priority))
			continue;

		logger->Debug("Schedule: serving applications with priority %d", priority);
		result = SchedulePriority(priority);
		if (result == SCHED_ERROR) {
			logger->Error("Schedule: error occurred while scheduling priority %d",
			              priority);
			forced_mapping_ids.clear();
			return result;
		}
	}

	forced_mapping_ids.clear();
	apps_count = 0;

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	return SCHED_DONE;
}


SchedulerPolicyIF::ExitCode_t ManGAv2SchedPol::SchedulePriority(
    AppPrio_t priority)
{
	// Get all the applications @ this priority
	AppsUidMapIt app_iterator;
	ba::AppCPtr_t papp = sys->GetFirstWithPrio(priority, app_iterator);
	for (; papp; papp = sys->GetNextWithPrio(priority, app_iterator)) {
		logger->Debug("SchedulePriority: [%s] looking for resources...",
		              papp->StrId());

		// Skip disabled
		if (papp->Disabled()) {
			logger->Debug("SchedulePriority: [%s] DISABLED: skip...",
			              papp->StrId());
			continue;
		}

		// Skip already scheduled running applications
		if (papp->Running()) {
			logger->Debug("SchedulePriority: [%s] RUNNING: skip...",
			              papp->StrId());
			continue;
		}

		// Evaluate the mapping options
		ExitCode_t ret = EvalMappingAlternatives(papp);
		if (ret != SCHED_OK) {
			logger->Warn("SchedulePriority: [%s] not schedulable",
			             papp->StrId());
			// Do not schedule
			ApplicationManager & am(ApplicationManager::GetInstance());
			am.NoSchedule(papp);
			ret = SCHED_OK;
		}

		++apps_count;
	}

	return ExitCode_t::SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t ManGAv2SchedPol::ReassignWorkingMode(
    bbque::app::AppCPtr_t papp)
{
	ApplicationManager & am(ApplicationManager::GetInstance());
	auto ret = am.ScheduleRequestAsPrev(papp, sched_status_view);
	if (ret != ApplicationManager::AM_SUCCESS) {
		logger->Error("ReassignWorkingMode: [%s] rescheduling failed", papp->StrId());
		return SCHED_SKIP_APP;
	}

	logger->Info("ReassignWorkingMode: [%s] rescheduling done", papp->StrId());
	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t ManGAv2SchedPol::EvalMappingAlternatives(
    bbque::app::AppCPtr_t papp)
{
	auto recipe = papp->GetRecipe();
	auto task_graph = papp->GetTaskGraph();
	ExitCode_t ret = ExitCode_t::SCHED_OK;

	// Optionally add an ordering criteria here...
	logger->Debug("EvalMappingAlternatives: [%s] nr. mapping options = %d",
		papp->StrId(), recipe->GetTaskGraphMappingAll().size());

	bbque::app::Recipe::TaskGraphMapping curr_mapping;

	for (auto & m: recipe->GetTaskGraphMappingAll()) {
		int curr_mapping_id;
		bool forced = false;

		// Check if a mapping id has been forced (testing purpose)
		logger->Debug("EvalMappingAlternatives: [%s] nr.apps=%d nr.forced=%d",
			papp->StrId(), apps_count, forced_mapping_ids.size());
		if (apps_count < forced_mapping_ids.size() && (!forced) && (ret == SCHED_OK)) {
			int m_id = forced_mapping_ids[apps_count];
			curr_mapping = recipe->GetTaskGraphMapping(m_id);
			curr_mapping_id = m_id;
			logger->Warn("EvalMappingAlternatives: [%s] forced mapping id=%d ",
				papp->StrId(), curr_mapping_id);
			forced = true;
		}
		else {
			curr_mapping_id = m.first;
			curr_mapping    = m.second;
		}

		logger->Info("EvalMappingAlternatives: [%s] id=%d "
		             "exec_time=%d[ms] power=%d[mW] memory_bw=%d[Kbps] ",
		             papp->StrId(),
		             curr_mapping_id,
		             curr_mapping.exec_time_ms,
		             curr_mapping.power_mw,
		             curr_mapping.mem_bw);

		if (task_graph->Tasks().size() > curr_mapping.tasks.size()) {
			logger->Error("EvalMappingAlternatives: [%s] "
			              "possible mismatch in recipe file!",
			              papp->StrId());
			return SCHED_ERROR;
		}

		bool task_mapping_succeeded = false;

		// Task mapping
		for (auto const & task_mapping: curr_mapping.tasks) {
			auto & task_id = task_mapping.first;
			auto & mapping_info = task_mapping.second;

			auto task = task_graph->GetTask(task_id);
			if (task == nullptr) {
				logger->Warn("EvalMappingAlternatives: [%s] task id=%d not found. "
				             "Possible mismatch in recipe file!",
				             papp->StrId(), task_id);
				continue;
			}

			if (mapping_info.type == bbque::res::ResourceType::ACCELERATOR) {
				// get the mango architecture
				auto curr_arch = arch_per_acc[0][mapping_info.id];
				logger->Debug("EvalMappingAlternatives: [%s] "
				              "task=%d => [acc=%d arch: %s]",
					papp->StrId(), task_id,
					mapping_info.id,
					GetStringFromArchType(curr_arch));

				// check the supported architectures
				auto & target_archs = task->Targets();
				if (target_archs.find(curr_arch) == target_archs.end()) {
					logger->Debug("EvalMappingAlternatives: [%s] "
						      "task=%d => [acc=%d arch: %s] missing kernel binary...",
						papp->StrId(),
						task->Id(),
						mapping_info.id,
						GetStringFromArchType(curr_arch));
					task_mapping_succeeded = false;
					break;
				}

				// assign the processor
				task->SetAssignedProcessor(mapping_info.id);
				task_mapping_succeeded = true;

				// TODO: frequency setting ...
			}
		}

		if (!task_mapping_succeeded) {
			logger->Debug("EvalMappingAlternatives: [%s] id=%d skipping..",
				papp->StrId(),
				curr_mapping);
			continue;
		}

		// Buffer mapping
		for (auto const & buff_mapping: curr_mapping.buffers) {
			auto & buff_id = buff_mapping.first;
			auto & mapping_info = buff_mapping.second;

			auto buffer = task_graph->GetBuffer(buff_id);
			if (buffer == nullptr) {
				logger->Warn("EvalMappingAlternatives: [%s] buffer id=%d not found. "
				             "Possible mismatch in recipe file!",
				             papp->StrId(), buff_id);
				continue;
			}

			if (mapping_info.type == bbque::res::ResourceType::MEMORY) {
				logger->Debug("EvalMappingAlternatives: [%s] "
				              "buffer=%d => [mem=%d]",
				              papp->StrId(), buffer->Id(), mapping_info.id);
				buffer->SetMemoryBank(mapping_info.id);
			}
		}

		// Check if the mapping is feasible in at least one platform cluster
		ret = CheckMappingFeasibility(papp);
		if (ret == SCHED_OK) {
			logger->Info("EvalMappingAlternatives: [%s] mapping [id=%d] is feasible",
			             papp->StrId(),
			             curr_mapping_id);
			return ret;
		} else if (ret == SCHED_ERROR) {
			logger->Error("EvalMappingAlternatives: [%s] mapping [id=%d] interrupted",
			              papp->StrId(),
			              curr_mapping_id);
			return ret;
		}
		else {
			logger->Debug("EvalMappingAlternatives: [%s] mapping [id=%d] not applicable."
				     " Trying next option...",
			              papp->StrId(),
			              curr_mapping_id);
		}

		// if here... try with the next mapping option
	}

	return SCHED_R_UNAVAILABLE;
}


SchedulerPolicyIF::ExitCode_t ManGAv2SchedPol::CheckMappingFeasibility(
    bbque::app::AppCPtr_t papp)
{
	ExitCode_t ret = SCHED_ERROR;

	// Iterate over all the HN clusters
	uint32_t nr_clusters = pe_per_acc.size();
	logger->Debug("CheckMappingFeasibility: [%s] nr. of clusters on the platform: %d",
	              papp->StrId(),
		      nr_clusters);

	for (uint32_t curr_cluster_id = 0; curr_cluster_id < nr_clusters; curr_cluster_id++) {
		logger->Debug("CheckMappingFeasibility: [%s] checking availability "
		              "in cluster=%d",
		              papp->StrId(), curr_cluster_id);

		// Assign current cluster id
		papp->GetTaskGraph()->SetCluster(curr_cluster_id);

		// Set the working mode
		int32_t ref_num;
		auto pawm = SelectWorkingMode(papp, ref_num);
		if (pawm == nullptr) {
			logger->Crit("CheckMappingFeasibility: [%s] null working mode",
			             papp->StrId());
			return ExitCode_t::SCHED_ERROR;
		}

		// Schedule request
		ret = ScheduleApplication(papp, pawm, ref_num);
		if (ret != SCHED_R_UNAVAILABLE) {
			logger->Debug("CheckMappingFeasibility: [%s] terminated [ret=%d]",
			              papp->StrId(), ret);
			return ret;
		}
		else {
			logger->Debug("CheckMappingFeasibility: [%s] mapping not feasible",
			              papp->StrId());
		}
	}

	return ret;
}


ba::AwmPtr_t ManGAv2SchedPol::SelectWorkingMode(ba::AppCPtr_t papp, int & ref_num)
{
	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(
		           papp->WorkingModes().size(),"Run-time", 1, papp);
	}
	pawm->ClearResourceBinding();
	ref_num = -1;

	uint32_t nr_cores = 1;
	auto tg = papp->GetTaskGraph();
	uint32_t cluster_id = tg->GetCluster();

	logger->Info("SelectWorkingMode: [%s] looking on cluster %d",
		papp->StrId(), cluster_id);

	for (auto task : tg->Tasks()) {
		// if thread count not specified, assign all the available cores
		logger->Debug("SelectWorkingMode: [%s] task=%d thread_count=%d",
		              papp->StrId(), task.first, task.second->GetThreadCount());

		int tile_id = task.second->GetAssignedProcessor();
		if (tile_id < 0) {
			logger->Error("SelectWorkingMode: [%s] task=%d mapping missing",
			              papp->StrId(), task.first);
			return nullptr;
		}

		if (task.second->GetThreadCount() <= 0) {
			nr_cores = pe_per_acc[cluster_id][tile_id];
			logger->Debug("SelectWorkingMode: [%s] task=%d nr_cores=%d",
			              papp->StrId(), task.first, nr_cores);
		}

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
		              papp->StrId(), task.first);

		// Resource binding: HN tile (accelerator)
		logger->Debug("SelectWorkingMode: [%s] task=%d bind accelerator request to "
		              "tile=%d",
		              papp->StrId(), task.first, tile_id);
		ref_num = pawm->BindResource(
		              br::ResourceType::ACCELERATOR,
			      tile_id,
		              tile_id,
		              ref_num);

		logger->Debug("SelectWorkingMode: [%s] task=%d -> cluster=<%d> tile=<%d>",
		              papp->StrId(), task.first,
		              cluster_id,
		              tile_id);
	}

	// Amount of memory to allocate per bank
	std::map<uint32_t, uint64_t> mem_req_amount;

	// Accouting of memory resources
	for (auto buff : tg->Buffers()) {
		int mem_bank_id = buff.second->MemoryBank();
		if (mem_bank_id < 0) {
			logger->Error("SelectWorkingMode: [%s] buffer=%d mapping missing",
			              papp->StrId(), buff.first);
			return nullptr;
		}

		mem_req_amount[mem_bank_id] += buff.second->Size();
		logger->Debug("SelectWorkingMode: [%s] buffer=%d -> memory bank =<%d>",
		              papp->StrId(),
		              buff.first,
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

	return pawm;
}


SchedulerPolicyIF::ExitCode_t ManGAv2SchedPol::ScheduleApplication(
    ba::AppCPtr_t papp,
    ba::AwmPtr_t pawm,
    int32_t ref_num)
{
	// Update the accounting of resources with a schedule request
	ApplicationManager & am(ApplicationManager::GetInstance());
	auto ret = am.ScheduleRequest(papp, pawm, sched_status_view, ref_num);
	if (ret == ApplicationManager::AM_SUCCESS) {
		logger->Info("ScheduleApplication: [%s] schedule request accepted",
		             papp->StrId());
		return SCHED_OK;
	} else if ((ret == ApplicationManager::AM_APP_BLOCKING) ||
	           (ret == ApplicationManager::AM_APP_DISABLED)) {
		logger->Debug("ScheduleApplication: [%s] schedule request for blocking/disabled",
		              papp->StrId());
		return SCHED_SKIP_APP;
	} else if (ret == ApplicationManager::AM_AWM_NOT_SCHEDULABLE) {
		logger->Debug("ScheduleApplication: [%s] schedule request not allocable",
		              papp->StrId());
		return SCHED_R_UNAVAILABLE;
	} else {
		logger->Error("ScheduleApplication: [%s] schedule request unexpected error",
		              papp->StrId());
		return SCHED_ERROR;
	}
}



} // namespace plugins

} // namespace bbque
