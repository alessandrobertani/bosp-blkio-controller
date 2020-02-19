/*
 * Copyright (C) 2012  Politecnico di Milano
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

#include "bbque/application_proxy.h"

#include "bbque/config.h"
#include "bbque/application_manager.h"
#include "bbque/process_manager.h"
#include "bbque/resource_accounter.h"
#include "bbque/resource_manager.h"
#include "bbque/modules_factory.h"
#include "bbque/app/working_mode.h"
#include "bbque/utils/utility.h"
#include "bbque/cpp11/chrono.h"

#ifdef CONFIG_TARGET_OPENCL
#include "bbque/pp/opencl_platform_proxy.h"
#endif

#define MODULE_NAMESPACE APPLICATION_PROXY_NAMESPACE

namespace ba = bbque::app;
namespace bl = bbque::rtlib;
namespace br = bbque::res;

namespace bbque
{

ApplicationProxy::ApplicationProxy():
	Worker()
{

	//---------- Setup Worker
	Worker::Setup(BBQUE_MODULE_NAME("ap"), APPLICATION_PROXY_NAMESPACE);

	//---------- Initialize the RPC channel module
	// TODO look the configuration file for the required channel
	// Build an RPCChannelIF object
	rpc = ModulesFactory::GetRPCChannelModule();
	if (!rpc) {
		logger->Fatal("RPC Channel module creation FAILED");
		abort();
	}
	// RPC channel initialization
	if (rpc->Init()) {
		logger->Fatal("RPC Channel module setup FAILED");
		abort();
	}

#define CMD_APP_TERMINATED ".terminated"
	CommandManager & cm(CommandManager::GetInstance());
	cm.RegisterCommand(MODULE_NAMESPACE CMD_APP_TERMINATED,
	                   static_cast<CommandHandler*>(this),
	                   "Unregister the specified EXC");
	// Spawn the command dispatching thread
	Worker::Start();
}

ApplicationProxy::~ApplicationProxy()
{
	// TODO add code to release the RPC Channel module
}

ApplicationProxy & ApplicationProxy::GetInstance()
{
	static ApplicationProxy instance;
	return instance;
}

bl::rpc_msg_type_t ApplicationProxy::GetNextMessage(pchMsg_t & pChMsg)
{
	rpc->RecvMessage(pChMsg);
	logger->Debug("GetNextMessage: RX [typ: %d, pid: %d]",
	              pChMsg->typ, pChMsg->app_pid);

	return (bl::rpc_msg_type_t)pChMsg->typ;
}

/*******************************************************************************
 * Command Sessions
 ******************************************************************************/

inline ApplicationProxy::pcmdSn_t ApplicationProxy::SetupCmdSession(
        AppPtr_t papp) const
{
	pcmdSn_t pcs(new cmdSn_t());
	pcs->papp = papp;
	pcs->resp_prm = resp_prm_t();

	// Resetting command session response message
	// This is the condition verified by the reception thread
	pcs->pmsg = NULL;

	logger->Debug("SetupCmdSession: session for [%s]", pcs->papp->StrId());
	return pcs;
}

inline void ApplicationProxy::EnqueueHandler(pcmdSn_t pcs)
{
	std::unique_lock<std::mutex> cmdSnMap_ul(cmdSnMap_mtx);

	assert(pcs);

	pcs->pid = gettid();

	if (cmdSnMap.find(pcs->pid) != cmdSnMap.end()) {
		logger->Crit("EnqueueHandler: handler enqueuing FAILED "
		             "(Error: duplicated handler thread)");
		assert(cmdSnMap.find(pcs->pid) == cmdSnMap.end());
		return;
	}

	cmdSnMap.insert(std::pair<pid_t, pcmdSn_t>(pcs->pid, pcs));

	logger->Debug("EnqueueHandler: enqueue command session [%05d] for [%s], "
	              "[qcount: %d]", pcs->pid, pcs->papp->StrId(), cmdSnMap.size());

}

RTLIB_ExitCode_t ApplicationProxy::StopExecutionSync(AppPtr_t papp)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx,
	                std::defer_lock);
	conCtxMap_t::iterator it;
	pconCtx_t pcon;

	bl::rpc_msg_token_t token_id = gettid();  // use thread id
	bl::rpc_msg_BBQ_STOP_t stop_msg = {
		{
			bl::RPC_BBQ_STOP_EXECUTION,
			token_id,
			static_cast<int>(papp->Pid()),
			papp->ExcId()
		},
		{0, 100} // FIXME get a timeout parameter
	};

	// Send the stop command
	logger->Debug("StopExecutionSync: Send command [RPC_BBQ_STOP_EXECUTION] -> "
	              "[app: %s, pid: %d, exc: %d]",
	              papp->Name().c_str(), papp->Pid(), papp->ExcId());

	assert(rpc);

	// Recover the communication context for this application
	conCtxMap_ul.lock();
	it = conCtxMap.find(papp->Pid());
	conCtxMap_ul.unlock();

	// Check we have a connection contexe already configured
	if (it == conCtxMap.end()) {
		logger->Error("StopExecutionSync: Connection context not found "
		              "for application [app: %s, pid: %d]",
		              papp->Name().c_str(), papp->Pid());
		return RTLIB_BBQUE_CHANNEL_UNAVAILABLE;
	}

	// Sending message on the application connection context
	pcon = (*it).second;
	rpc->SendMessage(pcon->pd, &stop_msg.hdr,
	                 (size_t)RPC_PKT_SIZE(BBQ_STOP));

	return RTLIB_OK;
}

void ApplicationProxy::StopExecutionTrd(pcmdSn_t pcs)
{
	(void)pcs;
#if 1
	pcmdRsp_t pcmdRsp(new cmdRsp_t);

	// Enqueuing the Command Session Handler
	EnqueueHandler(pcs);

	logger->Debug("StopExecutionTrd: [pid=%5d] (%s) START",
	              pcs->pid, pcs->papp->StrId());

	// Run the Command Synchronously
	pcmdRsp->result = StopExecutionSync(pcs->papp);
	// Give back the result to the calling thread
	(pcs->resp_prm).set_value(pcmdRsp->result);

	logger->Debug("StopExecutionTrd: [pid=%5d] (%s) END",
	              pcs->pid, pcs->papp->StrId());
#endif
}

RTLIB_ExitCode_t
ApplicationProxy::StopExecution(AppPtr_t papp)
{
	(void)papp;
#if 1
	ApplicationProxy::pcmdSn_t psn;

	// Ensure the application is still active
	assert(papp->State() < Application::FINISHED);
	if (papp->State() >= Application::FINISHED) {
		logger->Warn("StopExecution: multiple application [%s] stop",
		             papp->StrId());
		return RTLIB_OK;
	}

	// Setup a new command session
	psn = SetupCmdSession(papp);
	logger->Info("StopExecution: [%s] closing communication...",
	             papp->StrId());
	// Spawn a new Command Executor (passing the future)
	psn->exe = std::thread(&ApplicationProxy::StopExecutionTrd, this, psn);
	psn->exe.detach();


	// Return the promise (thus unlocking the executor)
	psn->resp_prm.get_future();
	logger->Info("StopExecution: [%s] communication closed",
	             papp->StrId());
#endif
	return RTLIB_OK;
}


/*******************************************************************************
 * Runtime profiling - OpenCL
 ******************************************************************************/

RTLIB_ExitCode_t
ApplicationProxy::Prof_GetRuntimeData(ba::AppPtr_t papp)
{
	// Command session setup
	pcmdSn_t pcs(SetupCmdSession(papp));
	assert(pcs);

	// Command executor (passing the future)
	pcs->exe = std::thread(
	                   &ApplicationProxy::Prof_GetRuntimeDataTrd, this, pcs);
	pcs->exe.detach();

	// Setup the promise (thus unlocking the executor)
	pcs->resp_ftr = (pcs->resp_prm).get_future();

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::Prof_GetRuntimeDataTrd(pcmdSn_t pcs)
{
	RTLIB_ExitCode_t result;

	// Command session handler
	EnqueueHandler(pcs);

	// Send get runtime profile request
	result = Prof_GetRuntimeDataSend(pcs->papp);
	if (result != RTLIB_OK) {
		logger->Error("Prof_GetRuntimeDataTrd: profile data request failed");
		return RTLIB_ERROR;
	}

	// Receive runtime profiling data
	result = Prof_GetRuntimeDataRecv(pcs);
	if (result != RTLIB_OK) {
		logger->Error("Prof_GetRuntimeDataTrd: profile data receiving failed");
		return RTLIB_ERROR;
	}

	return RTLIB_OK;

}

RTLIB_ExitCode_t
ApplicationProxy::Prof_GetRuntimeDataSend(ba::AppPtr_t papp)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx,
	                std::defer_lock);
	conCtxMap_t::iterator it;
	pconCtx_t pcon;

	// Get runtime profile data request message
	bl::rpc_msg_BBQ_GET_PROFILE_t stop_msg = {
		{
			bl::RPC_BBQ_GET_PROFILE,
			static_cast<unsigned int>(gettid()),
			static_cast<int>(papp->Pid()),
			papp->ExcId()
		},
		true
	};

	// Recover the communication context for this application
	conCtxMap_ul.lock();
	it = conCtxMap.find(papp->Pid());
	conCtxMap_ul.unlock();
	if (it == conCtxMap.end()) {
		logger->Error("Prof_GetRuntimeDataSend: Connection context not found "
		              "for application [app: %s, pid: %d]",
		              papp->Name().c_str(), papp->Pid());
		return RTLIB_BBQUE_CHANNEL_UNAVAILABLE;
	}
	pcon = (*it).second;

	// Send the command
	logger->Debug("Prof_GetRuntimeDataSend: Command [RPC_BBQ_GET_PROFILE] -> "
	              "[app: %s, pid: %d, exc: %d]",
	              papp->Name().c_str(), papp->Pid(), papp->ExcId());
	assert(rpc);
	rpc->SendMessage(pcon->pd, &stop_msg.hdr,
	                 (size_t)RPC_PKT_SIZE(BBQ_GET_PROFILE));

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::Prof_GetRuntimeDataRecv(pcmdSn_t pcs)
{
	std::unique_lock<std::mutex> resp_ul(pcs->resp_mtx);
	bl::rpc_msg_BBQ_GET_PROFILE_RESP_t *pmsg_pyl;
	rpc_msg_header_t *pmsg_hdr;
	std::cv_status ready;
	pchMsg_t pchMsg;

	// Wait for a response (if not yet available)
	if (!pcs->pmsg) {
		logger->Debug("Prof_GetRuntimeDataRecv: waiting for runtime profile data, "
		              "Timeout: %d[ms]", BBQUE_SYNCP_TIMEOUT);
		ready = (pcs->resp_cv).wait_for(resp_ul,
		                                std::chrono::milliseconds(
		                                        BBQUE_SYNCP_TIMEOUT));
		if (ready == std::cv_status::timeout) {
			logger->Warn("Prof_GetRuntimeDataRecv: TIMEOUT");
			pcs->pmsg = NULL;
			return RTLIB_BBQUE_CHANNEL_TIMEOUT;
		}
	}

	// Getting command response
	pchMsg   = pcs->pmsg;
	pmsg_hdr = pchMsg;
	pmsg_pyl = (bl::rpc_msg_BBQ_GET_PROFILE_RESP_t*)pmsg_hdr;
	logger->Debug("Prof_GetRuntimeDataRecv: command response [typ: %d, pid: %d]",
	              pmsg_hdr->typ,
	              pmsg_hdr->app_pid);
	assert(pmsg_hdr->typ == bl::RPC_BBQ_RESP);

	// Update application/EXC profile data
	logger->Info("Prof_GetRuntimeDataRecv: profile timings [us]: { exec: %d mem: %d }",
	             pmsg_pyl->exec_time, pmsg_pyl->mem_time);

	assert(pcs->papp->CurrentAWM());
	pcs->papp->CurrentAWM()->SetRuntimeProfExecTime(pmsg_pyl->exec_time);
	pcs->papp->CurrentAWM()->SetRuntimeProfMemTime(pmsg_pyl->mem_time);
	logger->Info("Prof_GetRuntimeDataRecv: [%s %s] runtime profile set",
	             pcs->papp->StrId(), pcs->papp->CurrentAWM()->StrId());

	return RTLIB_OK;
}

/*******************************************************************************
 * Synchronization Protocol - PreChange
 ******************************************************************************/

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PreChangeSend(pcmdSn_t pcs)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx,
	                std::defer_lock);
	conCtxMap_t::iterator it;
	AppPtr_t papp = pcs->papp;
	pconCtx_t pcon;
	ssize_t result;
#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
	Application::CGroupSetupData_t cgroup_data = papp->GetCGroupSetupData();
#endif // CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
	bl::rpc_msg_BBQ_SYNCP_PRECHANGE_t syncp_prechange_msg = {
		{
			bl::RPC_BBQ_SYNCP_PRECHANGE, pcs->pid,
			static_cast<int>(papp->Pid()), papp->ExcId()
		},
		(uint8_t)papp->SyncState(),
		// If the application is BLOCKING we don't have a NextAWM but we also
		// don't care at RTLib side about the value of this parameter
		0,
#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
		// cpuset.cpus
		cgroup_data.cpu_ids,
		// cpuset.cpus in isolation,
		cgroup_data.cpus_ids_isolation,
		// cpuset.mems
		cgroup_data.mem_ids,
#endif // CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
		// Currently we consider only one system
		1
	};
	bl::rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t local_sys_msg = {
		// SystemID
		0,
		// Number of resources (CPU, PROC_ELEMENT cores)
		0, 0,
		// Resource amount (PROC_ELEMENT, MEMORY)
		0, 0,
#ifdef CONFIG_TARGET_OPENCL
		// Resource amount (GPU, ACCELERATOR)
		0, 0,
		// Device ID
		R_ID_NONE,
#endif
	};

	ResourceAccounter &ra(ResourceAccounter::GetInstance());

	// Set the next AWM only if the application is not going to be blocked
	if (BBQUE_LIKELY(!papp->Blocking())) {
		syncp_prechange_msg.awm = papp->NextAWM()->Id();
#ifndef CONFIG_TARGET_EMULATED_HOST
		// CPUs (processors)
		local_sys_msg.nr_cpus =
		        papp->NextAWM()->BindingSet(br::ResourceType::CPU).Count();
		// Processing elements (number of)
		local_sys_msg.nr_procs =
		        papp->NextAWM()->BindingSet(br::ResourceType::PROC_ELEMENT).Count();
		// Processing elements (quota)
		local_sys_msg.r_proc = ra.GetAssignedAmount(
		                               papp->NextAWM()->GetResourceBinding(),
		                               papp, ra.GetScheduledView(),
		                               br::ResourceType::PROC_ELEMENT);
		// Memory amount
		local_sys_msg.r_mem = ra.GetAssignedAmount(
		                              papp->NextAWM()->GetResourceBinding(),
		                              papp, ra.GetScheduledView(),
		                              br::ResourceType::MEMORY);
#else
		logger->Warn("APPs PRX: TPD enabled. No resource assignment enforcing");
#endif // CONFIG_TARGET_EMULATED_HOST
		logger->Debug("SyncP_PreChangeSend: Command [RPC_BBQ_SYNCP_PRECHANGE] -> "
		              "EXC [%s], CPUs=<%d>, PROCs=<%2d [%d%%]>,MEM=<%d> @sv{%d}",
		              papp->StrId(),
		              local_sys_msg.nr_cpus,
		              local_sys_msg.nr_procs,
		              local_sys_msg.r_proc,
		              local_sys_msg.r_mem,
		              ra.GetScheduledView());

#ifdef CONFIG_TARGET_OPENCL
		br::ResourceBitset gpu_ids(papp->NextAWM()->BindingSet(br::ResourceType::GPU));
		BBQUE_RID_TYPE r_id = gpu_ids.FirstSet();

		// If no GPU have been bound, the CPU is the OpenCL device assigned
		if (r_id == R_ID_NONE) {
			OpenCLPlatformProxy * ocl_proxy(OpenCLPlatformProxy::GetInstance());
			auto pdev_ids(ocl_proxy->GetDeviceIDs(br::ResourceType::CPU));
			if (pdev_ids && !pdev_ids->empty())
				r_id = pdev_ids->at(0);
			else
				r_id = R_ID_NONE;
		}

		local_sys_msg.dev = r_id;
		switch(r_id) {
		case R_ID_NONE:
			logger->Info("SyncP_PreChangeSend: [%s] NO OpenCL device assigned");
			break;
		case R_ID_ANY:
			logger->Info("SyncP_PreChangeSend: [%s] NO OpenCL device forcing");
			break;
		default:
			logger->Info("SyncP_PreChangeSend: [%s] OpenCL device assigned: %d",
			             papp->StrId(), r_id);
		}
#endif
	}

	// Send the required synchronization action
	logger->Debug("SyncP_PreChangeSend: Command [RPC_BBQ_SYNCP_PRECHANGE] -> "
	              "EXC [%s], Action [%d:%s]", papp->StrId(), papp->SyncState(),
	              ApplicationStatusIF::SyncStateStr(papp->SyncState()));

	// Recover the communication context for this application
	conCtxMap_ul.lock();
	it = conCtxMap.find(papp->Pid());
	conCtxMap_ul.unlock();

	// Check we have a connection context already configured
	if (it == conCtxMap.end()) {
		logger->Error("SyncP_PreChangeSend: Command [RPC_BBQ_SYNCP_PRECHANGE] "
		              "-> EXC [%s] FAILED (Error: connection context not found)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_UNAVAILABLE;
	}

	// Sending message on the application connection context
	pcon = (*it).second;
	result = rpc->SendMessage(pcon->pd, &syncp_prechange_msg.hdr,
	                          (size_t)RPC_PKT_SIZE(BBQ_SYNCP_PRECHANGE));
	if (result == -1) {
		logger->Error("SyncP_PreChangeSend: Command [RPC_BBQ_SYNCP_PRECHANGE] "
		              "-> EXC [%s] FAILED (Error: write failed)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_WRITE_FAILED;
	}

	result = rpc->SendMessage(pcon->pd, (rpc_msg_header_t*)(&local_sys_msg),
	                          (size_t)RPC_PKT_SIZE(BBQ_SYNCP_PRECHANGE_SYSTEM));
	if (result == -1) {
		logger->Error("SyncP_PreChangeSend: Command [RPC_BBQ_SYNCP_PRECHANGE] "
		              "-> EXC [%s] FAILED (Error: write failed)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_WRITE_FAILED;
	}


	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PreChangeRecv(pcmdSn_t pcs,
                                      pPreChangeRsp_t presp)
{
	std::unique_lock<std::mutex> resp_ul(pcs->resp_mtx);
	bl::rpc_msg_BBQ_SYNCP_PRECHANGE_RESP_t *pmsg_pyl;
	rpc_msg_header_t *pmsg_hdr;
	std::cv_status ready;
	pchMsg_t pchMsg;

	// Wait for a response (if not yet available)
	if (!pcs->pmsg) {
		logger->Debug("SyncP_PreChangeRecv: waiting for response, "
		              "Timeout: %d[ms]", BBQUE_SYNCP_TIMEOUT);
		ready = (pcs->resp_cv).wait_for(resp_ul,
		                                std::chrono::milliseconds(
		                                        BBQUE_SYNCP_TIMEOUT));
		if (ready == std::cv_status::timeout) {
			logger->Warn("SyncP_PreChangeRecv: response TIMEOUT");
			pcs->pmsg = NULL;
			return RTLIB_BBQUE_CHANNEL_TIMEOUT;
		}
	}

	// Getting command response
	pchMsg = pcs->pmsg;
	pmsg_hdr = pchMsg;
	pmsg_pyl = (bl::rpc_msg_BBQ_SYNCP_PRECHANGE_RESP_t*)pmsg_hdr;

	logger->Debug("SyncP_PreChangeRecv: command response [typ: %d, pid: %d, latency: %u]",
	              pmsg_hdr->typ,
	              pmsg_hdr->app_pid,
	              pmsg_pyl->syncLatency);

	assert(pmsg_hdr->typ == bl::RPC_BBQ_RESP);

	// Processing response
	presp->syncLatency = pmsg_pyl->syncLatency;
	if (pcs->papp->CurrentAWM())
		pcs->papp->CurrentAWM()->SetRuntimeProfSyncTime(
		        presp->syncLatency);

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PreChange(pcmdSn_t pcs, pPreChangeRsp_t presp)
{
	assert(pcs);
	assert(presp);

	// Send the Command
	presp->result = SyncP_PreChangeSend(pcs);
	if (presp->result != RTLIB_OK)
		return presp->result;

	// Get back the response
	presp->result = SyncP_PreChangeRecv(pcs, presp);
	if (presp->result != RTLIB_OK)
		return presp->result;

#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
	// Give back the result to the calling thread
	(presp->pcs->resp_prm).set_value(presp->result);
	logger->Debug("APPs PRX [%05d]: Set response for [%s]",
	              presp->pcs->pid, presp->pcs->papp->StrId());
#endif

	return RTLIB_OK;

}

void
ApplicationProxy::SyncP_PreChangeTrd(pPreChangeRsp_t presp)
{
	// Enqueuing the Command Session Handler
	EnqueueHandler(presp->pcs);

	logger->Debug("SyncP_PreChangeTrd: [pid=%05d] START",
	              presp->pcs->pid, presp->pcs->papp->StrId());

	// Run the Command Executor
	SyncP_PreChange(presp->pcs, presp);

	logger->Debug("SyncP_PreChangeTrd: [pid=%05d]: (%s) END",
	              presp->pcs->pid, presp->pcs->papp->StrId());
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PreChange(AppPtr_t papp, pPreChangeRsp_t presp)
{
	RTLIB_ExitCode_t result = RTLIB_OK;
	assert(papp);
	assert(presp);

	presp->pcs = SetupCmdSession(papp);
	assert(presp->pcs);

#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
	// Spawn a new Command Executor (passing the future)
	presp->pcs->exe = std::thread(&ApplicationProxy::SyncP_PreChangeTrd,
	                              this, presp);
	presp->pcs->exe.detach();

	// Setup the promise (thus unlocking the executor)
	presp->pcs->resp_ftr = (presp->pcs->resp_prm).get_future();

#else
	// Enqueuing the Command Session Handler
	EnqueueHandler(presp->pcs);

	// Run the Command Executor
	result = SyncP_PreChange(presp->pcs, presp);

	// Releasing the command session
	ReleaseCommandSession(presp->pcs);
#endif

	return result;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PreChange_GetResult(pPreChangeRsp_t presp)
{
	RTLIB_ExitCode_t result = RTLIB_BBQUE_CHANNEL_TIMEOUT;
	FutureStatus_t ftrStatus;

	assert(presp);

	// Wait for the promise being returned
	ftrStatus = presp->pcs->resp_ftr.wait_for(std::chrono::milliseconds(BBQUE_SYNCP_TIMEOUT));
	if (!FutureTimedout(ftrStatus))
		result = presp->pcs->resp_ftr.get();

	// Releasing the command session
	ReleaseCommandSession(presp->pcs);

	return result;
}

/*******************************************************************************
 * Synchronization Protocol - SyncChange
 ******************************************************************************/

RTLIB_ExitCode_t
ApplicationProxy::SyncP_SyncChangeSend(pcmdSn_t pcs)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx,
	                std::defer_lock);
	conCtxMap_t::iterator it;
	AppPtr_t papp = pcs->papp;
	pconCtx_t pcon;
	ssize_t result;
	bl::rpc_msg_BBQ_SYNCP_SYNCCHANGE_t syncp_syncchange_msg = {
		{
			bl::RPC_BBQ_SYNCP_SYNCCHANGE, pcs->pid,
			static_cast<int>(papp->Pid()), papp->ExcId()
		}
	};

	// Send the stop command
	logger->Debug("SyncP_SyncChangeSend: Command [RPC_BBQ_SYNCP_SYNCCHANGE] -> "
	              "EXC [%s]", papp->StrId());

	// Recover the communication context for this application
	conCtxMap_ul.lock();
	it = conCtxMap.find(papp->Pid());
	conCtxMap_ul.unlock();

	// Check we have a connection context already configured
	if (it == conCtxMap.end()) {
		logger->Error("SyncP_SyncChangeSend: Command [RPC_BBQ_SYNCP_SYNCCHANGE] "
		              "-> EXC [%s] FAILED (Error: connection context not found)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_UNAVAILABLE;
	}

	// Sending message on the application connection context
	pcon = (*it).second;
	result = rpc->SendMessage(pcon->pd, &syncp_syncchange_msg.hdr,
	                          (size_t)RPC_PKT_SIZE(BBQ_SYNCP_SYNCCHANGE));
	if (result == -1) {
		logger->Error("SyncP_SyncChangeSend: Command [RPC_BBQ_SYNCP_SYNCCHANGE] "
		              "-> EXC [%s] FAILED (Error: write failed)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_WRITE_FAILED;
	}

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_SyncChangeRecv(pcmdSn_t pcs,
                                       pSyncChangeRsp_t presp)
{
	std::unique_lock<std::mutex> resp_ul(pcs->resp_mtx);
	//rpc_msg_BBQ_SYNCP_SYNCCHANGE_RESP_t *pmsg_pyl;
	rpc_msg_header_t *pmsg_hdr;
	std::cv_status ready;
	pchMsg_t pchMsg;

	// Wait for a response (if not yet available)
	if (!pcs->pmsg) {
		logger->Debug("SyncP_SyncChangeRecv: waiting for response, Timeout: %d[ms]",
		              BBQUE_SYNCP_TIMEOUT);
		ready = (pcs->resp_cv).wait_for(
		                resp_ul, std::chrono::milliseconds(BBQUE_SYNCP_TIMEOUT));
		if (ready == std::cv_status::timeout) {
			logger->Warn("SyncP_SyncChangeRecv: response TIMEOUT");
			pcs->pmsg = NULL;
			return RTLIB_BBQUE_CHANNEL_TIMEOUT;
		}
	}

	// Getting command response
	pchMsg = pcs->pmsg;
	pmsg_hdr = pchMsg;
	//pmsg_pyl = (rpc_msg_BBQ_SYNCP_SYNCCHANGE_RESP_t*)pmsg_hdr;

	logger->Debug("SyncP_SyncChangeRecv: command response [typ: %d, pid: %d]",
	              pmsg_hdr->typ,
	              pmsg_hdr->app_pid);

	assert(pmsg_hdr->typ == bl::RPC_BBQ_RESP);

	// Processing response (nothing to process right now)
	(void)presp;

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_SyncChange(pcmdSn_t pcs, pSyncChangeRsp_t presp)
{
	assert(pcs);
	assert(presp);

	// Send the Command
	presp->result = SyncP_SyncChangeSend(pcs);
	if (presp->result != RTLIB_OK)
		return presp->result;

	// Get back the response
	presp->result = SyncP_SyncChangeRecv(pcs, presp);
	if (presp->result != RTLIB_OK)
		return presp->result;

#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
	// Give back the result to the calling thread
	(presp->pcs->resp_prm).set_value(presp->result);
	logger->Debug("APPs PRX [%05d]: Set response for [%s]",
	              presp->pcs->pid, presp->pcs->papp->StrId());
#endif

	return RTLIB_OK;

}

void
ApplicationProxy::SyncP_SyncChangeTrd(pSyncChangeRsp_t presp)
{

	assert(presp);

	// Enqueuing the Command Session Handler
	EnqueueHandler(presp->pcs);

	logger->Debug("SyncP_SyncChangeTrd: [pid=%05d]: [%s] START",
	              presp->pcs->pid, presp->pcs->papp->StrId());

	// Run the Command Executor
	SyncP_SyncChange(presp->pcs, presp);

	logger->Debug("SyncP_SyncChangeTrd: [pid=%05d]: [%s] END",
	              presp->pcs->pid, presp->pcs->papp->StrId());
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_SyncChange(AppPtr_t papp, pSyncChangeRsp_t presp)
{
	RTLIB_ExitCode_t result = RTLIB_OK;

	assert(papp);
	assert(presp);

	presp->pcs = SetupCmdSession(papp);
	assert(presp->pcs);

#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
	// Spawn a new Command Executor (passing the future)
	presp->pcs->exe = std::thread(&ApplicationProxy::SyncP_SyncChangeTrd,
	                              this, presp);
	presp->pcs->exe.detach();

	// Setup the promise (thus unlocking the executor)
	presp->pcs->resp_ftr = (presp->pcs->resp_prm).get_future();
#else
	// Enqueuing the Command Session Handler
	EnqueueHandler(presp->pcs);

	// Run the Command Executor
	result = SyncP_SyncChange(presp->pcs, presp);

	// Releasing the command session
	ReleaseCommandSession(presp->pcs);
#endif

	return result;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_SyncChange_GetResult(pSyncChangeRsp_t presp)
{
	RTLIB_ExitCode_t result = RTLIB_BBQUE_CHANNEL_TIMEOUT;
	FutureStatus_t ftrStatus;

	assert(presp);

	// Wait for the promise being returned
	ftrStatus = presp->pcs->resp_ftr.wait_for(std::chrono::milliseconds(
	                        BBQUE_SYNCP_TIMEOUT));
	if (!FutureTimedout(ftrStatus))
		result = presp->pcs->resp_ftr.get();

	// Releasing the command session
	ReleaseCommandSession(presp->pcs);

	return result;
}


/*******************************************************************************
 * Synchronization Protocol - DoChange
 ******************************************************************************/

RTLIB_ExitCode_t
ApplicationProxy::SyncP_DoChangeSend(pcmdSn_t pcs)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx,
	                std::defer_lock);
	conCtxMap_t::iterator it;
	AppPtr_t papp = pcs->papp;
	pconCtx_t pcon;
	ssize_t result;
	bl::rpc_msg_BBQ_SYNCP_DOCHANGE_t syncp_syncchange_msg = {
		{
			bl::RPC_BBQ_SYNCP_DOCHANGE, pcs->pid,
			static_cast<int>(papp->Pid()), papp->ExcId()
		}
	};

	// Send the stop command
	logger->Debug("SyncP_DoChangeSend: Command [RPC_BBQ_SYNCP_DOCHANGE] -> "
	              "EXC [%s]", papp->StrId());

	// Recover the communication context for this application
	conCtxMap_ul.lock();
	it = conCtxMap.find(papp->Pid());
	conCtxMap_ul.unlock();

	// Check we have a connection context already configured
	if (it == conCtxMap.end()) {
		logger->Error("SyncP_DoChangeSend: Command [RPC_BBQ_SYNCP_DOCHANGE] "
		              "-> EXC [%s] FAILED (Error: connection context not found)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_UNAVAILABLE;
	}

	// Sending message on the application connection context
	pcon = (*it).second;
	result = rpc->SendMessage(pcon->pd, &syncp_syncchange_msg.hdr,
	                          (size_t)RPC_PKT_SIZE(BBQ_SYNCP_DOCHANGE));
	if (result == -1) {
		logger->Error("SyncP_DoChangeSend: Command [RPC_BBQ_SYNCP_DOCHANGE] "
		              "-> EXC [%s] FAILED (Error: write failed)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_WRITE_FAILED;
	}

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_DoChange(pcmdSn_t pcs, pDoChangeRsp_t presp)
{

	assert(pcs);
	assert(presp);

	// Send the Command
	presp->result = SyncP_DoChangeSend(pcs);
	if (presp->result != RTLIB_OK)
		return presp->result;

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_DoChange(AppPtr_t papp)
{
	pDoChangeRsp_t presp = ApplicationProxy::pDoChangeRsp_t(
	                               new ApplicationProxy::doChangeRsp_t());

	assert(papp);

	presp->pcs = SetupCmdSession(papp);

	// Run the Command Executor
	return SyncP_DoChange(presp->pcs, presp);
}


/*******************************************************************************
 * Synchronization Protocol - PostChange
 ******************************************************************************/

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PostChangeSend(pcmdSn_t pcs)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx,
	                std::defer_lock);
	conCtxMap_t::iterator it;
	AppPtr_t papp = pcs->papp;
	pconCtx_t pcon;
	ssize_t result;
	bl::rpc_msg_BBQ_SYNCP_POSTCHANGE_t syncp_syncchange_msg = {
		{
			bl::RPC_BBQ_SYNCP_POSTCHANGE, pcs->pid,
			static_cast<int>(papp->Pid()), papp->ExcId()
		}
	};

	// Send the stop command
	logger->Debug("SyncP_PostChangeSend: Send Command [RPC_BBQ_SYNCP_POSTCHANGE] to "
	              "EXC [%s]", papp->StrId());

	// Recover the communication context for this application
	conCtxMap_ul.lock();
	it = conCtxMap.find(papp->Pid());
	conCtxMap_ul.unlock();

	// Check we have a connection context already configured
	if (it == conCtxMap.end()) {
		logger->Error("SyncP_PostChangeSend: Send Command [RPC_BBQ_SYNCP_POSTCHANGE] "
		              "to EXC [%s] FAILED (Error: connection context not found)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_UNAVAILABLE;
	}

	// Sending message on the application connection context
	pcon = (*it).second;
	result = rpc->SendMessage(pcon->pd, &syncp_syncchange_msg.hdr,
	                          (size_t)RPC_PKT_SIZE(BBQ_SYNCP_POSTCHANGE));
	if (result == -1) {
		logger->Error("SyncP_PostChangeSend: Send Command [RPC_BBQ_SYNCP_POSTCHANGE] "
		              "to EXC [%s] FAILED (Error: write failed)",
		              papp->StrId());
		return RTLIB_BBQUE_CHANNEL_WRITE_FAILED;
	}

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PostChangeRecv(pcmdSn_t pcs,
                                       pPostChangeRsp_t presp)
{
	std::unique_lock<std::mutex> resp_ul(pcs->resp_mtx);
	//rpc_msg_BBQ_SYNCP_POSTCHANGE_RESP_t *pmsg_pyl;
	rpc_msg_header_t *pmsg_hdr;
	std::cv_status ready;
	pchMsg_t pchMsg;

	// Wait for a response (if not yet available)
	if (!pcs->pmsg) {
		logger->Debug("SyncP_PostChangeRecv: waiting for PostChange response, "
		              "Timeout: %d[ms]", BBQUE_SYNCP_TIMEOUT);
		ready = (pcs->resp_cv).wait_for(resp_ul,
		                                std::chrono::milliseconds(
		                                        BBQUE_SYNCP_TIMEOUT));
		if (ready == std::cv_status::timeout) {
			logger->Warn("SyncP_PostChangeRecv: PostChange response TIMEOUT");
			pcs->pmsg = NULL;
			return RTLIB_BBQUE_CHANNEL_TIMEOUT;
		}
	}

	// Getting command response
	pchMsg = pcs->pmsg;
	pmsg_hdr = pchMsg;
	//pmsg_pyl = (rpc_msg_BBQ_SYNCP_POSTCHANGE_RESP_t*)pmsg_hdr;

	logger->Debug("APPs PRX: command response [typ: %d, pid: %d]",
	              pmsg_hdr->typ,
	              pmsg_hdr->app_pid);

	assert(pmsg_hdr->typ == bl::RPC_BBQ_RESP);

	// Build a new communication context
	logger->Debug("APPs PRX: PostChangeResp [pid: %d]", pmsg_hdr->app_pid);

	// Processing response (nothing to process right now)
	(void)presp;

	return RTLIB_OK;
}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PostChange(pcmdSn_t pcs, pPostChangeRsp_t presp)
{
	assert(pcs);
	assert(presp);

	// Send the Command
	presp->result = SyncP_PostChangeSend(pcs);
	if (presp->result == RTLIB_OK) {
		// Get back the response
		presp->result = SyncP_PostChangeRecv(pcs, presp);
	}

	// Releasing the command session
	ReleaseCommandSession(pcs);

	return presp->result;

}

RTLIB_ExitCode_t
ApplicationProxy::SyncP_PostChange(AppPtr_t papp, pPostChangeRsp_t presp)
{

	assert(papp);
	assert(presp);

	presp->pcs = SetupCmdSession(papp);

	// Enqueuing the Command Session Handler
	EnqueueHandler(presp->pcs);

	// Run the Command Executor
	return SyncP_PostChange(presp->pcs, presp);
}



/*******************************************************************************
 * Command Sessions Helpers
 ******************************************************************************/

ApplicationProxy::pcmdSn_t
ApplicationProxy::GetCommandSession(rpc_msg_header_t *pmsg_hdr)
{
	std::unique_lock<std::mutex> cmdSnMap_ul(cmdSnMap_mtx);
	cmdSnMap_t::iterator it;
	pcmdSn_t pcs;

	// Looking for a valid command session
	it = cmdSnMap.find(pmsg_hdr->token);
	if (it == cmdSnMap.end()) {
		cmdSnMap_ul.unlock();
		logger->Warn("GetCommandSession: get session [%5d] FAILED",
		             "(Error: command session not found)", pmsg_hdr->token);
		assert(it != cmdSnMap.end());
		return pcmdSn_t();
	}

	pcs = (*it).second;
	logger->Debug("APPs PRX: Command session get [%05d] for [%s]",
	              pcs->pid, pcs->papp->StrId());

	return pcs;
}

void
ApplicationProxy::ReleaseCommandSession(pcmdSn_t pcs)
{
	std::unique_lock<std::mutex> cmdSnMap_ul(cmdSnMap_mtx);
	cmdSnMap_t::iterator it;

	// Looking for a valid command session
	it = cmdSnMap.find(pcs->pid);
	if (it == cmdSnMap.end()) {
		cmdSnMap_ul.unlock();
		logger->Warn("ReleaseCommandSession: session [%5d] release FAILED",
		             "(Error: command session not found)", pcs->pid);
		assert(it != cmdSnMap.end());
		return;
	}

	// Remove the command session from the map of pending responses
	// thus cleaning-up all of its data
	cmdSnMap.erase(it);

	logger->Debug("ReleaseCommandSession: dequeing command session [%05d] for [%s], "
	              "[qcount: %d]", pcs->pid, pcs->papp->StrId(), cmdSnMap.size());
}

void ApplicationProxy::CompleteTransaction(pchMsg_t & pmsg)
{
	rpc_msg_header_t * pmsg_hdr = pmsg;
	pcmdSn_t pcs;

	assert(pmsg_hdr);

	logger->Debug("CompleteTransaction: dispatching command response "
	              "[typ: %d, pid: %d] to [%5d]...",
	              pmsg_hdr->typ,
	              pmsg_hdr->app_pid,
	              pmsg_hdr->token);

	// Looking for a valid command session
	pcs = GetCommandSession(pmsg_hdr);
	if (!pcs) {
		logger->Crit("CompleteTransaction: dispatching command response FAILED "
		             "(Error: cmd session not found for token [%d])",
		             pmsg_hdr->token);
		assert(pcs);
		return;
	}

	// Setup command session response buffer
	pcs->pmsg = pmsg;

	// Notify command session
	std::unique_lock<std::mutex> resp_ul(pcs->resp_mtx);
	(pcs->resp_cv).notify_one();
}


/*******************************************************************************
 * Request Sessions
 ******************************************************************************/

#define APP_STRID "[%05d:%6s]"
#define AppStrId(pcon) pcon->app_pid, pcon->app_name

#define EXC_STRID "[%05d:%6s:%02d]"
#define ExcStrId(pcon, exc_id) pcon->app_pid, pcon->app_name, exc_id

ApplicationProxy::pconCtx_t ApplicationProxy::GetConnectionContext(
        rpc_msg_header_t *pmsg_hdr)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx);
	conCtxMap_t::iterator it;
	pconCtx_t pcon;

	// Looking for a valid connection context
	it = conCtxMap.find(pmsg_hdr->app_pid);
	if (it == conCtxMap.end()) {
		conCtxMap_ul.unlock();
		logger->Warn("GetConnectionContext: EXC registration FAILED",
		             "[pid: %d, exc: %d] (Error: application not paired)",
		             pmsg_hdr->app_pid, pmsg_hdr->exc_id);
		return pconCtx_t();
	}
	return (*it).second;
}

void ApplicationProxy::RpcACK(pconCtx_t pcon, rpc_msg_header_t *pmsg_hdr,
                              bl::rpc_msg_type_t type)
{
	bl::rpc_msg_resp_t resp;

	// Sending response to application
	logger->Debug("RpcACK: Send RPC channel ACK " APP_STRID, AppStrId(pcon));
	::memcpy(&resp.hdr, pmsg_hdr, RPC_PKT_SIZE(header));
	resp.hdr.typ = type;
	resp.result = RTLIB_OK;
	rpc->SendMessage(pcon->pd, &resp.hdr, (size_t)RPC_PKT_SIZE(resp));
}

void ApplicationProxy::RpcNAK(pconCtx_t pcon, rpc_msg_header_t *pmsg_hdr,
                              bl::rpc_msg_type_t type, RTLIB_ExitCode_t error)
{
	bl::rpc_msg_resp_t resp;

	// Sending response to application
	logger->Debug("RpcNAK: Send RPC channel NAK " APP_STRID ", error [%d]",
	              AppStrId(pcon), error);
	::memcpy(&resp.hdr, pmsg_hdr, RPC_PKT_SIZE(header));
	resp.hdr.typ = type;
	resp.result = error;
	rpc->SendMessage(pcon->pd, &resp.hdr, (size_t)RPC_PKT_SIZE(resp));
}

void ApplicationProxy::RpcExcRegister(prqsSn_t prqs)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx, std::defer_lock);
	ApplicationManager &am(ApplicationManager::GetInstance());
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	bl::rpc_msg_EXC_REGISTER_t * pmsg_pyl = (bl::rpc_msg_EXC_REGISTER_t*)pmsg_hdr;
	pconCtx_t pcon;
	AppPtr_t papp;

	assert(pchMsg);

	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon)
		return;

	// Registering a new Execution Context
	logger->Info("RpcExcRegister: Registering EXC " EXC_STRID ", name [%s]",
	             ExcStrId(pcon, pmsg_hdr->exc_id), pmsg_pyl->exc_name);

	// Register the EXC with the ApplicationManager
	papp  = am.CreateEXC(pmsg_pyl->exc_name, pcon->app_pid,
	                     pmsg_hdr->exc_id, pmsg_pyl->recipe, pmsg_pyl->lang);
	if (!papp) {
		logger->Error("RpcExcRegister: EXC " EXC_STRID ", name [%s] "
		              "registration FAILED "
		              "(Error: missing recipe or recipe load failure)",
		              ExcStrId(pcon, pmsg_hdr->exc_id), pmsg_pyl->exc_name);
		RpcNAK(pcon, pmsg_hdr, bl::RPC_EXC_RESP, RTLIB_EXC_MISSING_RECIPE);
		return;
	}

	// Sending ACK response to application
	RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcExcUnregister(prqsSn_t prqs)
{
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	bl::rpc_msg_EXC_UNREGISTER_t * pmsg_pyl =
	        (bl::rpc_msg_EXC_UNREGISTER_t*)pmsg_hdr;
	pconCtx_t pcon;

	assert(pchMsg);

	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon)
		return;

	// Unregister an Execution Context
	logger->Info("RpcExcUnregister: Unregistering EXC "
	             "[app: %s, pid: %d, exc: %d, nme: %s]",
	             pcon->app_name, pcon->app_pid,
	             pmsg_hdr->exc_id, pmsg_pyl->exc_name);

	// Unregister the EXC from the ApplicationManager
//	ApplicationManager &am(ApplicationManager::GetInstance());
//	am.DestroyEXC(pcon->app_pid, pmsg_hdr->exc_id);

	// FIXME we should deliver the error code to the application
	// This is tracked by Issues tiket #10

	// Sending ACK response to application
	RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcExcSet(prqsSn_t prqs)
{
	ApplicationManager &am(ApplicationManager::GetInstance());
	ResourceManager &rm(ResourceManager::GetInstance());
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	bl::rpc_msg_EXC_SET_t * pmsg_pyl = (bl::rpc_msg_EXC_SET_t*)pmsg_hdr;
	pconCtx_t pcon;
	ApplicationManager::ExitCode_t result;

	assert(pchMsg);

	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon)
		return;

	// Setting constraints for the requesting Execution Context
	logger->Info("RpcExcSet: Set [%d] constraints on EXC "
	             "[app: %s, pid: %d, exc: %d]",
	             pmsg_pyl->count, pcon->app_name,
	             pcon->app_pid, pmsg_hdr->exc_id);
	result = am.SetConstraintsEXC(pcon->app_pid, pmsg_hdr->exc_id,
	                              &(pmsg_pyl->constraints), pmsg_pyl->count);
	if (result == ApplicationManager::AM_RESCHED_REQUIRED) {
		// Notify the ResourceManager for a new reschedule request
		logger->Debug("RpcExcSet: Notifing ResourceManager...");
		rm.NotifyEvent(ResourceManager::BBQ_OPTS);
	} else if (result != ApplicationManager::AM_SUCCESS) {
		logger->Error("RpcExcSet: EXC [pid: %d, exc: %d] "
		              "set [%d] constraints FAILED",
		              pcon->app_pid, pmsg_hdr->exc_id, pmsg_pyl->count);
		RpcNAK(pcon, pmsg_hdr, bl::RPC_EXC_RESP, RTLIB_EXC_ENABLE_FAILED);
		return;
	}

	// Sending ACK response to application
	RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcExcClear(prqsSn_t prqs)
{
	ApplicationManager &am(ApplicationManager::GetInstance());
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	pconCtx_t pcon;
	ApplicationManager::ExitCode_t result;

	assert(pchMsg);

	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon)
		return;

	// Clearing constraints for the requesting Execution Context
	logger->Info("RpcExcClear: Clearing constraints on EXC "
	             "[app: %s, pid: %d, exc: %d]",
	             pcon->app_name, pcon->app_pid, pmsg_hdr->exc_id);
	result = am.ClearConstraintsEXC(pcon->app_pid, pmsg_hdr->exc_id);
	if (result != ApplicationManager::AM_SUCCESS) {
		logger->Error("RpcExcClear: EXC [pid: %d, exc: %d] "
		              "clear constraints FAILED",
		              pcon->app_pid, pmsg_hdr->exc_id);
		RpcNAK(pcon, pmsg_hdr, bl::RPC_EXC_RESP, RTLIB_EXC_ENABLE_FAILED);
		return;
	}

	// Sending ACK response to application
	RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcExcRuntimeProfileNotify(prqsSn_t prqs)
{
	ApplicationManager &am(ApplicationManager::GetInstance());
	ResourceManager &rm(ResourceManager::GetInstance());
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	bl::rpc_msg_EXC_RTNOTIFY_t * pmsg_pyl =
	        (bl::rpc_msg_EXC_RTNOTIFY_t*)pmsg_hdr;
	pconCtx_t pcon;
	ApplicationManager::ExitCode_t result;
	assert(pchMsg);
	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon) {
		logger->Error("RpcExcRuntimeProfileNotify: No connection context");
		return;
	}

	logger->Info("RpcExcRuntimeProfileNotify: Profile received for EXC "
	             "[app: %s, pid: %d, exc: %d]",
	             pcon->app_name, pcon->app_pid, pmsg_hdr->exc_id);
	result = am.SetRuntimeProfile(pcon->app_pid, pmsg_hdr->exc_id,
	                              pmsg_pyl->gap, pmsg_pyl->cusage, pmsg_pyl->ctime_ms);

	switch (result) {
	case ApplicationManager::AM_SUCCESS:
		break;
	case ApplicationManager::AM_RESCHED_REQUIRED:
		logger->Debug("RpcExcRuntimeProfileNotify: Notifying ResourceManager");
		rm.NotifyEvent(ResourceManager::BBQ_OPTS);
		break;
	default:
		//RpcNAK(pcon, pmsg_hdr, bl::RPC_EXC_RESP, RTLIB_EXC_ENABLE_FAILED);
		return;
	}

	// Sending ACK response to application
	//RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcExcStart(prqsSn_t prqs)
{
	ApplicationManager &am(ApplicationManager::GetInstance());
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	pconCtx_t pcon;
	ApplicationManager::ExitCode_t result;

	assert(pchMsg);

	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon)
		return;

	// Registering a new Execution Context
	logger->Info("RpcExcStart: Starting EXC "
	             "[app: %s, pid: %d, exc: %d]",
	             pcon->app_name, pcon->app_pid, pmsg_hdr->exc_id);

	// Enabling the EXC to the ApplicationManager
	result = am.EnableEXC(pcon->app_pid, pmsg_hdr->exc_id);
	if (result != ApplicationManager::AM_SUCCESS) {
		logger->Error("RpcExcStart: EXC "
		              "[pid: %d, exc: %d] "
		              "start FAILED",
		              pcon->app_pid, pmsg_hdr->exc_id);
		RpcNAK(pcon, pmsg_hdr, bl::RPC_EXC_RESP, RTLIB_EXC_ENABLE_FAILED);
		return;
	}

	// Sending ACK response to application
	RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcExcStop(prqsSn_t prqs)
{
	ApplicationManager &am(ApplicationManager::GetInstance());
	ResourceManager &rm(ResourceManager::GetInstance());
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	pconCtx_t pcon;
	ApplicationManager::ExitCode_t result;

	assert(pchMsg);

	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon)
		return;

	// Stopping an Execution Context
	logger->Info("RpcExcStop: Stopping EXC [app: %s, pid: %d, exc: %d]",
	             pcon->app_name, pcon->app_pid, pmsg_hdr->exc_id);

	// If there is an optimization in progress, wait...
	rm.WaitForReady();

	// Disabling the EXC from the ApplicationManager
	result = am.DisableEXC(pcon->app_pid, pmsg_hdr->exc_id, true);
	if (result == ApplicationManager::AM_EXC_STATUS_CHANGE_FAILED) {
		logger->Error("RpcExcStop: EXC [pid: %d, exc: %d] stop FAILED",
		              pcon->app_pid, pmsg_hdr->exc_id);
		RpcNAK(pcon, pmsg_hdr, bl::RPC_EXC_RESP, RTLIB_EXC_DISABLE_FAILED);
		return;
	}

	// Notify the ResourceManager for the application stopped
	logger->Debug("RpcExcStop: Notifying ResourceManager...");
	rm.NotifyEvent(ResourceManager::EXC_STOP);

	// Sending ACK response to the RTLib
	RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcExcSchedule(prqsSn_t prqs)
{
	ResourceManager &rm(ResourceManager::GetInstance());
	rpc_msg_header_t * pmsg_hdr = prqs->pmsg;
	pconCtx_t pcon;

	assert(pmsg_hdr);

	// Looking for a valid connection context
	pcon = GetConnectionContext(pmsg_hdr);
	if (!pcon)
		return;

	// Registering a new Execution Context
	logger->Info("RpcExcSchedule: Schedule request for EXC "
	             "[app: %s, pid: %d, exc: %d]",
	             pcon->app_name, pcon->app_pid, pmsg_hdr->exc_id);

	// Notify the ResourceManager for a new application willing to start
	logger->Debug("RpcExcSchedule: Notifying ResourceManager...");
	rm.NotifyEvent(ResourceManager::BBQ_OPTS);

	// Sending ACK response to application
	RpcACK(pcon, pmsg_hdr, bl::RPC_EXC_RESP);
}

void ApplicationProxy::RpcAppPair(prqsSn_t prqs)
{
	std::unique_lock<std::mutex> conCtxMap_ul(
	        conCtxMap_mtx, std::defer_lock);
	pchMsg_t pchMsg = prqs->pmsg;
	rpc_msg_header_t * pmsg_hdr = pchMsg;
	bl::rpc_msg_APP_PAIR_t * pmsg_pyl = (bl::rpc_msg_APP_PAIR_t*)pmsg_hdr;
	pconCtx_t pcon;

	// Ensure this application has not yet registerd
	assert(pmsg_hdr->typ == bl::RPC_APP_PAIR);
	assert(conCtxMap.find(pmsg_hdr->app_pid) == conCtxMap.end());

	// Build a new communication context
	logger->Debug("RpcAppPair: Setting-up RPC channel [pid: %d, name: %s]...",
	              pmsg_hdr->app_pid, pmsg_pyl->app_name);

	// Checking API versioning
	if (pmsg_pyl->mjr_version != RTLIB_VERSION_MAJOR ||
	    pmsg_pyl->mnr_version > RTLIB_VERSION_MINOR) {
		logger->Error("RpcAppPair: Setup RPC channel [pid: %d, name: %s] "
		              "FAILED (Error: version mismatch, "
		              "app_v%d.%d != rtlib_v%d.%d)",
		              pmsg_hdr->app_pid, pmsg_pyl->app_name,
		              pmsg_pyl->mjr_version, pmsg_pyl->mnr_version,
		              RTLIB_VERSION_MAJOR, RTLIB_VERSION_MINOR);
		return;
	}

	// Setting up a new communication context
	pcon = pconCtx_t(new conCtx_t);
	assert(pcon);
	if (!pcon) {
		logger->Error("RpcAppPair: Setup RPC channel [pid: %d, name: %s] "
		              "FAILED (Error: connection context setup)",
		              pcon->app_pid,
		              pcon->app_name);
		// TODO If an application do not get a responce withih a timeout
		// should try again to register. This support should be provided
		// by the RTLib
		return;
	}

	pcon->app_pid = pmsg_hdr->app_pid;
	::strncpy(pcon->app_name, pmsg_pyl->app_name, RTLIB_APP_NAME_LENGTH);
	pcon->pd = rpc->GetPluginData(pchMsg);
	assert(pcon->pd);
	if (!pcon->pd) {
		logger->Error("RpcAppPair: Setup RPC channel [pid: %d, name: %s] "
		              "FAILED (Error: communication channel setup)",
		              pcon->app_pid,
		              pcon->app_name);
		// TODO If an application do not get a responce withih a timeout
		// should try again to register. This support should be provided
		// by the RTLib
		return;
	}

	// Backup communication context for further messages
	conCtxMap_ul.lock();
	conCtxMap.insert(std::pair<pid_t, pconCtx_t>(
	                         pcon->app_pid, pcon));
	conCtxMap_ul.unlock();

	// Sending ACK response to application
	RpcACK(pcon, pmsg_hdr, bl::RPC_APP_RESP);
}

void ApplicationProxy::RpcAppExit(prqsSn_t prqs)
{
	std::unique_lock<std::mutex> conCtxMap_ul(conCtxMap_mtx);
	pchMsg_t pmsg = prqs->pmsg;
	conCtxMap_t::iterator conCtxIt;
	pconCtx_t pconCtx;

	// Ensure this application is already registerd
	conCtxIt = conCtxMap.find(pmsg->app_pid);
	assert(conCtxIt != conCtxMap.end());

	// Releasing application resources
	logger->Info("RpcAppExit: Application [app_pid: %d] ended, "
	             "releasing resources...",
	             pmsg->app_pid);

	// Cleanup communication channel resources
	pconCtx = (*conCtxIt).second;
	rpc->ReleasePluginData(pconCtx->pd);

	// Removing the connection context
	conCtxMap.erase(conCtxIt);
	conCtxMap_ul.unlock();
	logger->Info("RpcAppExit: resources release should have been already performed");
}

void ApplicationProxy::RequestExecutor(prqsSn_t prqs)
{
	std::unique_lock<std::mutex> snCtxMap_ul(snCtxMap_mtx);
	snCtxMap_t::iterator it;
	psnCtx_t psc;

	// Set the thread PID
	prqs->pid = gettid();

	// This look could be acquiren only when the ProcessCommand has returned
	// Thus we use this lock to synchronize the CommandExecutr starting with
	// the completion of the ProcessCommand, which also setup thread
	// tracking data structures.
	snCtxMap_ul.unlock();

	logger->Debug("RequestExecutor: [%d:%d]: Executor START",
	              prqs->pid, prqs->pmsg->typ);

	assert(prqs->pmsg->typ < bl::RPC_EXC_MSGS_COUNT);

	// TODO put here command execution code
	switch(prqs->pmsg->typ) {
	case bl::RPC_EXC_REGISTER:
		logger->Debug("EXC_REGISTER");
		RpcExcRegister(prqs);
		break;

	case bl::RPC_EXC_UNREGISTER:
		logger->Debug("EXC_UNREGISTER");
		RpcExcUnregister(prqs);
		break;

	case bl::RPC_EXC_SET:
		logger->Debug("EXC_SET");
		RpcExcSet(prqs);
		break;

	case bl::RPC_EXC_CLEAR:
		logger->Debug("EXC_CLEAR");
		RpcExcClear(prqs);
		break;

	case bl::RPC_EXC_RTNOTIFY:
		logger->Debug("EXC_RTNOTIFY");
		RpcExcRuntimeProfileNotify(prqs);
		break;

	case bl::RPC_EXC_START:
		logger->Debug("EXC_START");
		RpcExcStart(prqs);
		break;

	case bl::RPC_EXC_STOP:
		logger->Debug("EXC_STOP");
		RpcExcStop(prqs);
		break;

	case bl::RPC_EXC_SCHEDULE:
		logger->Debug("EXC_SCHEDULE");
		RpcExcSchedule(prqs);
		break;

	case bl::RPC_APP_PAIR:
		logger->Debug("APP_PAIR");
		RpcAppPair(prqs);
		break;

	case bl::RPC_APP_EXIT:
		logger->Debug("APP_EXIT");
		RpcAppExit(prqs);
		break;

	default:
		// Unknowen command
		assert(false);
		break;
	}

	// Releasing the thread tracking data before exiting
	snCtxMap_ul.lock();
	it = snCtxMap.lower_bound(prqs->pmsg->typ);
	for ( ; it != snCtxMap.end() ; it++) {
		psc = it->second;
		if (psc->pid == prqs->pid) {
			snCtxMap.erase(it);
			break;
		}
	}
	snCtxMap_ul.unlock();

	logger->Debug("RequestExecutor: [%d:%d] Executor END",
	              prqs->pid, prqs->pmsg->typ);

}

void ApplicationProxy::ProcessRequest(pchMsg_t & pmsg)
{
	std::unique_lock<std::mutex> snCtxMap_ul(snCtxMap_mtx);
	prqsSn_t prqsSn = prqsSn_t(new rqsSn_t);
	assert(prqsSn);

	prqsSn->pmsg = pmsg;
	// Create a new executor thread, this will start locked since it needs
	// the execMap_mtx we already hold. This is used to ensure that the
	// executor thread start only alfter the playground has been properly
	// prepared
	prqsSn->exe = std::thread(
	                      &ApplicationProxy::RequestExecutor,
	                      this, prqsSn);
	prqsSn->exe.detach();

	logger->Debug("ProcessRequest: enqueuing new request...");

	// Add a new threaded command executor
	snCtxMap.insert(std::pair<uint8_t, psnCtx_t>(
	                        pmsg->typ, prqsSn));

}

void ApplicationProxy::Task()
{
	bl::rpc_msg_type_t msgType;
	pchMsg_t pmsg;
	logger->Info("Task: Messages dispatcher STARTED");

	while (!done) {
		if (rpc->Poll() < 0)
			continue;
		logger->Debug("Task: new incoming message from RTLIB");

		msgType = GetNextMessage(pmsg);
		if (msgType > bl::RPC_EXC_MSGS_COUNT) {
			CompleteTransaction(pmsg);
			continue;
		}
		ProcessRequest(pmsg);
	}

	logger->Info("Task: Messages dispatcher ENDED");
}


int ApplicationProxy::CommandsCb(int argc, char *argv[])
{

	// Manage external application termination
	if (0 == strcmp(argv[0], MODULE_NAMESPACE CMD_APP_TERMINATED)) {
		if (argc < 2) {
			logger->Error("Releasing EXC FAILED: no pid/eid provided in unregister signal.");
			return -1;
		}
		uint32_t pid = atoi(argv[1]);
		uint32_t eid = atoi(argv[1] + 13);
		logger->Info("CommandsCb: [%d:%d] checking for release...", pid, eid);
		ApplicationManager &am(ApplicationManager::GetInstance());
		auto papp = am.GetApplication(pid, eid);
		if (papp) {
			// Do nothing if already terminated (via RTLIB RPC)
			if (papp->Disabled() || papp->Finished()) {
				logger->Debug("CommandsCb: [%s] already in status [%s/%s]",
				              papp->StrId(),
				              Application::stateStr[papp->State()],
				              Application::syncStateStr[papp->SyncState()]);
				return 1;
			}

			// If dead, notify the resource manager
			bool is_alive = am.CheckEXC(papp);
			logger->Debug("CommandsCb: [%d:%d] alive: %d", pid, eid, is_alive);
			if (!is_alive) {
				ResourceManager &rm(ResourceManager::GetInstance());
				rm.NotifyEvent(ResourceManager::EXC_STOP);
			}
		}
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
		else {
			logger->Debug("CommandsCb: [%d:%d] should be a process...", pid, eid);
			ProcessManager &prm(ProcessManager::GetInstance());
			prm.NotifyExit(pid);
		}
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER
		return 0;
	}

	return 0;
}


} // namespace bbque
