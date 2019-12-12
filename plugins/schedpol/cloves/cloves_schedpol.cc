/*
 * Copyright (C) 2014  Politecnico di Milano
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

#include "cloves_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "bbque/modules_factory.h"
#include "bbque/utils/logging/logger.h"

#include "bbque/application_manager.h"
#include "bbque/app/working_mode.h"
#include "bbque/res/binder.h"
#include "bbque/res/resource_path.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * ClovesSchedPol::Create(PF_ObjectParams *)
{
	return new ClovesSchedPol();
}

int32_t ClovesSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (ClovesSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * ClovesSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

ClovesSchedPol::ClovesSchedPol():
	cm(ConfigurationManager::GetInstance()),
	ra(ResourceAccounter::GetInstance()),
	bdm(BindingManager::GetInstance()),
	wm(PowerMonitor::GetInstance())
{
	// Logger instance
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);

	if (logger)
		logger->Info("cloves: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
		        FI("cloves: Built new dynamic object [%p]\n"), (void *)this);
}


ClovesSchedPol::~ClovesSchedPol()
{
	queues.clear();
}


SchedulerPolicyIF::ExitCode_t ClovesSchedPol::_Init()
{
	ExitCode_t result = OK;

	// Device queues
	if (!queues_ready) {
		result = InitDeviceQueues();
		if (result != SCHED_OK) {
			logger->Fatal("Init: Scheduling queues not available");
			return result;
		}
		// Start sampling power info from devices
		wm.Start();
	}

	// Update applications runtime profiling data
	ApplicationManager & am(ApplicationManager::GetInstance());
	am.UpdateRuntimeProfiles();

	return OK;
}


SchedulerPolicyIF::ExitCode_t ClovesSchedPol::InitDeviceQueues()
{
	ExitCode_t result = OK;
	logger->Debug("Init: device queues initialization...");

	// A device queue must be created for each binding domain
	// (i.e., OpenCL device type (CPU, GPU,...))
	BindingMap_t & bindings(bdm.GetBindingDomains());
	for (auto & bd_entry : bindings) {
		br::ResourceType  bd_type = bd_entry.first;
		BindingInfo_t const & bd_info(*(bd_entry.second));

		// Device map
		DeviceQueueMapPtr_t pdev_queue_map = DeviceQueueMapPtr_t(new DeviceQueueMap_t());
		queues.insert(
		        std::pair<br::ResourceType, DeviceQueueMapPtr_t>(
		                bd_type, pdev_queue_map));
		logger->Debug("Init: adding queues for device type '%s'",
		              br::GetResourceTypeString(bd_type));

		// Fill the map with device queues
		CreateDeviceQueues(pdev_queue_map, bd_info);
	}

	logger->Info("Init: found %d device types", queues.size());
	if (queues.empty())
		result = SCHED_ERROR_INIT;

	queues_ready = true;
	return result;
}

void ClovesSchedPol::CreateDeviceQueues(
        DeviceQueueMapPtr_t & pdev_queue_map,
        BindingInfo_t const & bd_info)
{

	// [CPU|GPU|..]0..n
	for (br::ResourcePtr_t const & rsrc : bd_info.resources) {
		DeviceQueuePtr_t pdev_queue = DeviceQueuePtr_t(new DeviceQueue_t());

		br::ResourcePathPtr_t r_path(new br::ResourcePath(rsrc->Path()));
		r_path->AppendString("pe");

		pdev_queue_map->insert(
		        std::pair<br::ResourcePathPtr_t, DeviceQueuePtr_t>(
		                r_path, pdev_queue));
		logger->Debug("Init: added queue for '%s'",
		              r_path->ToString().c_str());
	}

	logger->Info("Init: added %d device queues for device type %s",
	             pdev_queue_map->size(), bd_info.base_path->ToString().c_str());
}


SchedulerPolicyIF::ExitCode_t
ClovesSchedPol::Schedule(
        System & system,
        RViewToken_t & status_view)
{
	ExitCode_t result;

	// Class providing query functions for applications and resources
	sys = &system;

	// Initialize a new resources state view
	result = Init();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: Initialization failed");
		goto error;
	}

	// Iteration per priority
	for (ba::AppPrio_t prio = 0;
	     prio <= sys->ApplicationLowestPriority(); ++prio) {
		if (!sys->HasApplications(prio))
			continue;
		result = SchedulePriority(prio);
	}

	// Flush device quees sending the schedule requests
	result = Flush();
	if (result != SCHED_OK) {
		logger->Fatal("Schedule: Failures in schedule requests");
		goto error;
	}

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	ra.PrintStatusReport(status_view);
	return SCHED_DONE;
error:
	ra.PutView(status_view);
	return SCHED_ERROR;
}

SchedulerPolicyIF::ExitCode_t
ClovesSchedPol::SchedulePriority(ba::AppPrio_t prio)
{
	ExitCode_t result = OK;
	AppsUidMapIt app_it;
	ba::AppCPtr_t papp;

	// For each application look for the best OpenCL device
	papp = sys->GetFirstWithPrio(prio, app_it);
	for (; papp; papp = sys->GetNextWithPrio(prio, app_it)) {
		result = EnqueueIntoDevice(papp);
	}
	return result;
}

SchedulerPolicyIF::ExitCode_t
ClovesSchedPol::EnqueueIntoDevice(ba::AppCPtr_t papp)
{
	br::ResourceType dev_type = br::ResourceType::UNDEFINED;
	float highest_xm_time_ratio = -1.0;
	float xm_time_ratio;
	uint64_t cpu_qt, gpu_qt;

	// Device type selection: AWM evaluation
	SchedEntityPtr_t psched(new SchedEntity_t(papp, nullptr, R_ID_NONE, 0.0));
	ba::AwmPtrList_t const & awms(papp->WorkingModes());
	logger->Debug("EnqueueIntoDevice: [%s], #AWMs: %d",
	              papp->StrId(), awms.size());
	for (ba::AwmPtr_t const & pawm : awms) {
		ba::WorkingMode::RuntimeProfiling_t awm_prof =
		        pawm->GetProfilingData();

		// Execution time / Memory transfers time ratio
		if ((awm_prof.mem_time == 0) || (awm_prof.exec_time == 0)) {
			logger->Debug("EnqueueIntoDevice: [%s %s] profiling not available",
			              papp->StrId(), pawm->StrId());
			xm_time_ratio = pawm->Value();
		} else {
			logger->Debug("EnqueueIntoDevice: [%s %s] Xtime: %d, Mtime: %d",
			              papp->StrId(), pawm->StrId(),
			              awm_prof.exec_time, awm_prof.mem_time);
			xm_time_ratio =
			        pawm->Value() *
			        (awm_prof.exec_time / awm_prof.mem_time);
		}
		logger->Debug("EnqueueIntoDevice: [%s %s] X/M time weighted ratio = %2.2f",
		              papp->StrId(), pawm->StrId(), xm_time_ratio);

		if ((highest_xm_time_ratio > 0) &&
		    (xm_time_ratio <= highest_xm_time_ratio)) {
			logger->Fatal("No update: h=%2.2f r=%2.2f",
			              highest_xm_time_ratio, xm_time_ratio);
			continue;
		}

		// Set device type
		gpu_qt = ra.GetAssignedAmount(
		                 pawm->ResourceRequests(), papp, sched_status_view,
		                 br::ResourceType::PROC_ELEMENT, br::ResourceType::GPU);
		cpu_qt = ra.GetAssignedAmount(
		                 pawm->ResourceRequests(), papp, sched_status_view,
		                 br::ResourceType::PROC_ELEMENT, br::ResourceType::CPU);

		gpu_qt > 0 ? dev_type = br::ResourceType::GPU : dev_type = br::ResourceType::CPU;
		logger->Debug("EnqueueIntoDevice: [%s %s] requiring processing load: "
		              "GPU: %" PRIu64 ", CPU: %" PRIu64 "",
		              papp->StrId(), pawm->StrId(), gpu_qt, cpu_qt);

		// AWM (device) selected so far
		psched->SetAWM(pawm);
		highest_xm_time_ratio = xm_time_ratio;
	}

	// Enqueing into the best device
	logger->Debug("EnqueueIntoDevice: %s enqueing...", psched->StrId());
	return Enqueue(psched, dev_type);
}

SchedulerPolicyIF::ExitCode_t ClovesSchedPol::Enqueue(
        SchedEntityPtr_t psched,
        br::ResourceType dev_type)
{
	ExitCode_t result;

	// Queue ordering metrics
	ComputeOrderingMetrics(psched);

	// Device queue
	DeviceQueuePtr_t pdev_queue(SelectDeviceQueue(psched, dev_type));
	if (pdev_queue == nullptr) {
		logger->Fatal("Enqueue: device queue missing");
		return SCHED_ERROR_QUEUE;
	}

	// Resource binding
	result = BindResources(psched);
	if (result != SCHED_OK)
		return result;
	if (psched->bind_type == br::ResourceType::GPU) {
		logger->Debug("Enqueue: %s binding host resources on CPU",
		              psched->StrId());
		size_t b_refn = psched->pawm->BindResource(
		                        br::ResourceType::CPU,
		                        R_ID_ANY, R_ID_NONE,
		                        psched->bind_refn);
		psched->bind_refn = b_refn;
	}

	// Enqueue
	pdev_queue->push(psched);
	logger->Debug("Enqueue: %s queued [size: %d]",
	              psched->StrId(), pdev_queue->size());

	return SCHED_OK;
}

void ClovesSchedPol::ComputeOrderingMetrics(SchedEntityPtr_t psched)
{
	ba::WorkingMode::RuntimeProfiling_t const & awm_prof(
	        psched->pawm->GetProfilingData());
	// Check the availability cumulated runtime profiling data
	if ((awm_prof.exec_time_tot) == 0 ||
	    (psched->pawm->GetSchedulingCount() == 0)) {
		psched->metrics = psched->pawm->Value();
		logger->Debug("Order: %s profiling history not available yet",
		              psched->StrId());
		return;
	}

	// Ordering metrics for the AWM in the device queue
	psched->metrics =
	        awm_prof.exec_time_tot /
	        psched->pawm->GetSchedulingCount() * psched->pawm->Value();
	logger->Info("Order: %s metrics = %2.2f [sched_count: %d, x_tot: %d]",
	             psched->StrId(), psched->metrics,
	             psched->pawm->GetSchedulingCount(), awm_prof.exec_time_tot);
}

ClovesSchedPol::DeviceQueuePtr_t
ClovesSchedPol::SelectDeviceQueue(
        SchedEntityPtr_t psched,
        br::ResourceType dev_type)
{
	br::ResourcePathPtr_t curr_path;
	size_t min_qlen = INT_MAX;
	size_t curr_qlen;

	// Device type queue
	DeviceQueueMapPtr_t & pdev_queue_map(queues[dev_type]);
	DeviceQueueMap_t & dev_queue_map(*(pdev_queue_map.get()));

	// Retrieve the queue of the device to bind
	for (auto & dq_entry : dev_queue_map) {
		br::ResourcePtrList_t const & r_list(ra.GetResources(dq_entry.first));
		br::ResourcePtr_t const & rsrc(r_list.front());

		if (rsrc == nullptr) {
			logger->Fatal("Missing resource %s",
			              dq_entry.first->ToString().c_str());
			return DeviceQueuePtr_t();
		}

		// Look for the shortest device queue
//		curr_load = rsrc->GetPowerInfo(PowerManager::InfoType::LOAD);
		curr_qlen = dq_entry.second->size();
		if (curr_qlen < min_qlen) {
			min_qlen  = curr_qlen;
			curr_path = dq_entry.first;
			if (dev_type == br::ResourceType::GPU)
				psched->SetBindingID(
				        dq_entry.first->GetID(dev_type), dev_type);
			else
				psched->SetBindingID(R_ID_NONE, dev_type);
		}
		logger->Debug("SelectDeviceQueue: %s queue length = %d",
		              rsrc->Path().c_str(), curr_qlen);
	}

	logger->Debug("SelectDeviceQueue: selected (%s)", curr_path->ToString().c_str());
	return dev_queue_map[curr_path];
}

SchedulerPolicyIF::ExitCode_t
ClovesSchedPol::BindResources(SchedEntityPtr_t psched)
{
	size_t r_refn;
	r_refn = psched->pawm->BindResource(
	                 psched->bind_type, R_ID_ANY,
	                 psched->bind_id,
	                 psched->bind_refn);
	logger->Debug("BindResources: reference number %ld", r_refn);
	if (r_refn == 0) {
		logger->Error("BindResources: %s failed binding", psched->StrId());
		return SCHED_R_UNAVAILABLE;
	}
	psched->bind_refn = r_refn;

	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t ClovesSchedPol::Flush()
{
	uint32_t app_count = 0;

	// Device types
	for (auto & dtqm_entry : queues) {
		DeviceQueueMap_t & dqm(*(dtqm_entry.second.get()));
		// Devices
		for (auto & dqm_entry : dqm) {
			DeviceQueue_t & dev_queue(*dqm_entry.second.get());
			app_count += SendScheduleRequests(dev_queue);
		}
	}

	if (app_count == 0)
		return SCHED_ERROR;
	return SCHED_OK;
}

uint32_t ClovesSchedPol::SendScheduleRequests(DeviceQueue_t & dev_queue)
{
	ba::Application::ExitCode_t app_result = ba::Application::APP_SUCCESS;
	uint32_t app_count = 0;

	// Flush the entire device queue
	while (!dev_queue.empty()) {

		// Pick the topmost schedule entity
		SchedEntityPtr_t const & psched(dev_queue.top());

		// Skip if disabled meanwhile
		if (psched->papp->Disabled()) {
			logger->Debug("Flush: %s disabled. Skipping...");
			dev_queue.pop();
			continue;
		}

		// Send request
		ApplicationManager & am(ApplicationManager::GetInstance());
		auto ret = am.ScheduleRequest(
		                   psched->papp, psched->pawm, sched_status_view, psched->bind_refn);
		logger->Debug("Flush: %s schedule requested", psched->StrId());
		if (ret != ApplicationManager::AM_SUCCESS) {
			logger->Error("Flush: %s failed scheduling", psched->StrId());
		}
		dev_queue.pop();
		++app_count;
	}

	return app_count;
}

} // namespace plugins

} // namespace bbque
