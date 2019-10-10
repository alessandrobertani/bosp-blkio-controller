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

#include "tempbalance_schedpol.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iostream>

#include "bbque/modules_factory.h"
#include "bbque/utils/logging/logger.h"

#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "bbque/res/resource_utils.h"

#ifdef CONFIG_BBQUE_PM_CPU
#include "bbque/pm/power_manager.h"
#endif

using namespace std::placeholders;

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * TempBalanceSchedPol::Create(PF_ObjectParams *)
{
	return new TempBalanceSchedPol();
}

int32_t TempBalanceSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (TempBalanceSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * TempBalanceSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

TempBalanceSchedPol::TempBalanceSchedPol():
	cm(ConfigurationManager::GetInstance()),
	ra(ResourceAccounter::GetInstance())
{
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);

	if (logger)
		logger->Info("tempbalance: Built a new dynamic object[%p]",
		             this);
	else
		fprintf(stderr,
		        FI("tempbalance: Built new dynamic object [%p]\n"),
		        (void *)this);
}


TempBalanceSchedPol::~TempBalanceSchedPol()
{
	entities.clear();
}


SchedulerPolicyIF::ExitCode_t TempBalanceSchedPol::Init()
{
	// Build a string path for the resource state view
	std::string token_path(MODULE_NAMESPACE);
	++status_view_count;
	token_path.append(std::to_string(status_view_count));
	logger->Debug("Init: requiring a new resource state view [%s]",
	              token_path.c_str());

	// A new resource status view
	auto ra_result = ra.GetView(token_path, sched_status_view);
	if (ra_result != ResourceAccounterStatusIF::RA_SUCCESS) {
		logger->Fatal("Init: cannot get a new resource state view");
		return SCHED_ERROR_VIEW;
	}
	logger->Debug("Init: resources state view token = %ld", sched_status_view);

	// Keep track of all the available CPU processing elements (cores)
	if (proc_elements.empty())
		proc_elements = sys->GetResources("sys.cpu.pe");

#ifdef CONFIG_BBQUE_PM_CPU
	// Sort processing elements (by temperature)
	SortProcessingElements();
#endif

	// Compute the number of slots for priority-proportional assignments
	if (sys->HasApplications(ApplicationStatusIF::READY))
		nr_slots = GetSlots();
	logger->Debug("Init: number of assignable slots = %d", nr_slots);

	return SCHED_OK;
}


#ifdef CONFIG_BBQUE_PM_CPU

void TempBalanceSchedPol::SortProcessingElements()
{
	proc_elements.sort(bbque::res::CompareTemperature);
	for (auto & pe : proc_elements)
		logger->Debug("<%s> : %.0f C",
		              pe->Path()->ToString().c_str(),
		              pe->GetPowerInfo(PowerManager::InfoType::TEMPERATURE));

	uint32_t temp_mean;
	PowerManager & pm(PowerManager::GetInstance());
	pm.GetTemperature(ra.GetPath("sys.cpu.pe"), temp_mean);
	logger->Debug("Init: <sys.cpu.pe> mean temperature = %d", temp_mean);
}

#endif // CONFIG_BBQUE_PM_CPU


uint64_t TempBalanceSchedPol::ComputeResourceQuota(
        std::string resource_path_str, bbque::app::AppCPtr_t papp) const
{
	// Amount of processing resources to assign
	uint64_t total_quota = sys->ResourceTotal(resource_path_str);
	uint64_t resource_slot_size = total_quota / nr_slots;
	logger->Debug("Assign: <%s> total = %lu slot_size=%d",
	              resource_path_str.c_str(),
	              total_quota,
	              resource_slot_size);
	uint64_t assigned_quota =
	        (sys->ApplicationLowestPriority() - papp->Priority() + 1)
	        * resource_slot_size;
	logger->Info("Assign: [%s] amount of <%s> assigned = %4d",
	             papp->StrId(),
	             resource_path_str.c_str(),
	             assigned_quota);

	return assigned_quota;
}

SchedulerPolicyIF::ExitCode_t
TempBalanceSchedPol::AssignWorkingMode(bbque::app::AppCPtr_t papp)
{
	logger->Debug("Assign: [%s] assigning resources...", papp->StrId());

	if (papp->Blocking()) {
		logger->Info("Assign: [%s] is being blocked", papp->StrId());
		return SCHED_OK;
	}

	if (papp->Running())
		papp->CurrentAWM()->ClearResourceRequests();

	// New AWM
	auto pawm = papp->CurrentAWM();
	if (pawm == nullptr)
		pawm = std::make_shared<ba::WorkingMode>(
		               papp->WorkingModes().size(), "Run-time", 1, papp);

	// Processing element quota
	std::string resource_path_str("sys.cpu.pe");
	uint64_t assigned_quota = ComputeResourceQuota(resource_path_str, papp);
	if (assigned_quota == 0) {
		logger->Warn("Assign: [%s] will have no resources", papp->StrId());
		return SCHED_OK;
	}

	// Assign...
	pawm->AddResourceRequest(resource_path_str,
	                         assigned_quota,
	                         br::ResourceAssignment::Policy::BALANCED);
	logger->Debug("Assign: [%s] added resource request [#%d]",
	              papp->StrId(), pawm->NumberOfResourceRequests());

	// Enqueue scheduling entity
	auto sched_entity = std::make_shared<SchedEntity_t>(papp, pawm, R_ID_ANY, 0);
	entities.push_back(sched_entity);

	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t TempBalanceSchedPol::BindWorkingModesAndSched()
{

	bbque::res::ResourcePtrList_t::const_iterator iter = proc_elements.begin();
	auto proc_path = ra.GetPath("sys.cpu.pe");

	for (auto & sched_entity : entities) {
		logger->Info("Bind: [%s] binding and scheduling...",
		             sched_entity->papp->StrId());
		uint64_t req_amount = sched_entity->pawm->RequestedAmount(proc_path);
		size_t num_procs = req_amount > 100 ? ceil(float(req_amount) / 100.0) : 1;
		logger->Debug("Bind: [%s] <sys.cpu.pe>=%d => num_procs=%d",
		              sched_entity->papp->StrId(), req_amount, num_procs);

		auto proc_mask =
		        bbque::res::ResourceBinder::GetMaskInRange(
		                proc_elements, iter, num_procs);
		logger->Debug("Bind: [%s] <sys.cpu.pe> mask = %s",
		              sched_entity->papp->StrId(), proc_mask.ToString().c_str());

		if ((req_amount % 100 == 0) || ((*iter)->Available() == 0)) {
			logger->Debug("Bind: increment the iterator");
			iter++;
		}

		sched_entity->bind_refn =
		        sched_entity->pawm->BindResource(proc_path, proc_mask);

		ApplicationManager & am(ApplicationManager::GetInstance());
		am.ScheduleRequest(
		        sched_entity->papp, sched_entity->pawm,
		        sched_status_view, sched_entity->bind_refn);
	}

	return SCHED_OK;
}



SchedulerPolicyIF::ExitCode_t
TempBalanceSchedPol::Schedule(
        System & system,
        RViewToken_t & status_view)
{
	SchedulerPolicyIF::ExitCode_t result = SCHED_DONE;

	// Class providing query functions for applications and resources
	sys = &system;
	Init();

	// Resource (AWM) assignment
	auto assign_awm = std::bind(&TempBalanceSchedPol::AssignWorkingMode, this, _1);
	ForEachReadyAndRunningDo(assign_awm);

	// Resource binding and then scheduling
	BindWorkingModesAndSched();
	entities.clear();

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	return result;
}

} // namespace plugins

} // namespace bbque
