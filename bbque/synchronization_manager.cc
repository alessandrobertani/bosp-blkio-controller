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

#include "bbque/synchronization_manager.h"

#include "bbque/application_manager.h"
#include "bbque/configuration_manager.h"
#include "bbque/modules_factory.h"
#include "bbque/plugin_manager.h"
#include "bbque/resource_accounter.h"
#include "bbque/system.h"

#include "bbque/app/application.h"
#include "bbque/app/working_mode.h"

#include "bbque/utils/utility.h"

// The prefix for configuration file attributes
#define MODULE_CONFIG "SynchronizationManager"

/** Metrics (class COUNTER) declaration */
#define SM_COUNTER_METRIC(NAME, DESC)\
	{SYNCHRONIZATION_MANAGER_NAMESPACE "." NAME, DESC, \
		MetricsCollector::COUNTER, 0, NULL, 0}
/** Increase counter for the specified metric */
#define SM_COUNT_EVENT(METRICS, INDEX) \
	mc.Count(METRICS[INDEX].mh);
/** Increase counter for the specified metric */
#define SM_COUNT_EVENT2(METRICS, INDEX, AMOUNT) \
	mc.Count(METRICS[INDEX].mh, AMOUNT);

/** Metrics (class SAMPLE) declaration */
#define SM_SAMPLE_METRIC(NAME, DESC)\
	{SYNCHRONIZATION_MANAGER_NAMESPACE "." NAME, DESC, \
		MetricsCollector::SAMPLE, 0, NULL, 0}
/** Reset the timer used to evaluate metrics */
#define SM_RESET_TIMING(TIMER) \
	TIMER.start();
/** Acquire a new completion time sample */
#define SM_GET_TIMING(METRICS, INDEX, TIMER) \
	mc.AddSample(METRICS[INDEX].mh, TIMER.getElapsedTimeMs());
/** Acquire a new EXC reconfigured sample */
#define SM_ADD_SAMPLE(METRICS, INDEX, COUNT) \
	mc.AddSample(METRICS[INDEX].mh, COUNT);

/** SyncState Metrics (class SAMPLE) declaration */
#define SM_SAMPLE_METRIC_SYNCSTATE(NAME, DESC)\
	{SYNCHRONIZATION_MANAGER_NAMESPACE "." NAME, DESC, MetricsCollector::SAMPLE, \
		bbque::app::Schedulable::SYNC_STATE_COUNT, \
		bbque::app::Schedulable::syncStateStr, 0}
/** Acquire a new completion time sample for SyncState Metrics*/
#define SM_GET_TIMING_SYNCSTATE(METRICS, INDEX, TIMER, STATE) \
	mc.AddSample(METRICS[INDEX].mh, TIMER.getElapsedTimeMs(), STATE);

namespace bu = bbque::utils;
namespace bp = bbque::plugins;
namespace po = boost::program_options;
namespace br = bbque::res;

namespace bbque
{

/* Definition of metrics used by this module */
MetricsCollector::MetricsCollection_t
SynchronizationManager::metrics[SM_METRICS_COUNT] = {
	//----- Event counting metrics
	SM_COUNTER_METRIC("runs", "SyncP executions count"),
	SM_COUNTER_METRIC("comp", "SyncP completion count"),
	SM_COUNTER_METRIC("excs", "Total EXC reconf count"),
	SM_COUNTER_METRIC("sync_hit",  "Syncs HIT count"),
	SM_COUNTER_METRIC("sync_miss", "Syncs MISS count"),
	//----- Timing metrics
	SM_SAMPLE_METRIC("sp.a.time",  "Avg SyncP execution t[ms]"),
	SM_SAMPLE_METRIC("sp.a.lat",   " Pre-Sync Lat   t[ms]"),
	SM_SAMPLE_METRIC_SYNCSTATE("sp.a.pre",   " PreChange  exe t[ms]"),
	SM_SAMPLE_METRIC_SYNCSTATE("sp.a.sync",  " SyncChange exe t[ms]"),
	SM_SAMPLE_METRIC_SYNCSTATE("sp.a.synp",  " SyncPlatform exe t[ms]"),
	SM_SAMPLE_METRIC_SYNCSTATE("sp.a.do",    " DoChange   exe t[ms]"),
	SM_SAMPLE_METRIC_SYNCSTATE("sp.a.post",  " PostChange exe t[ms]"),
	//----- Couting statistics
	SM_SAMPLE_METRIC("avge", "Average EXCs reconf"),
	SM_SAMPLE_METRIC("app.SyncLat", "Average SyncLatency declared"),

};

SynchronizationManager & SynchronizationManager::GetInstance()
{
	static SynchronizationManager ym;
	return ym;
}

SynchronizationManager::SynchronizationManager() :
	am(ApplicationManager::GetInstance()),
	ap(ApplicationProxy::GetInstance()),
	mc(bu::MetricsCollector::GetInstance()),
	ra(ResourceAccounter::GetInstance()),
	plm(PlatformManager::GetInstance()),
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	prm(ProcessManager::GetInstance()),
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER
	sv(System::GetInstance()),
	sync_count(0)
{
	std::string sync_policy;

	//---------- Get a logger module
	logger = bu::Logger::GetLogger(SYNCHRONIZATION_MANAGER_NAMESPACE);
	assert(logger);

	logger->Debug("Starting synchronization manager...");

	//---------- Loading module configuration
	ConfigurationManager & cm = ConfigurationManager::GetInstance();
	po::options_description opts_desc("Synchronization Manager Options");
	opts_desc.add_options()
	(MODULE_CONFIG".policy",
	 po::value<std::string>
	 (&sync_policy)->default_value(
	         BBQUE_DEFAULT_SYNCHRONIZATION_MANAGER_POLICY),
	 "The name of the optimization policy to use")
	;
	po::variables_map opts_vm;
	cm.ParseConfigurationFile(opts_desc, opts_vm);

	//---------- Load the required optimization plugin
	std::string sync_namespace(SYNCHRONIZATION_POLICY_NAMESPACE".");
	logger->Debug("Loading synchronization policy [%s%s]...",
	              sync_namespace.c_str(), sync_policy.c_str());
	policy = ModulesFactory::GetModule<bp::SynchronizationPolicyIF>(
	                 sync_namespace + sync_policy);
	if (!policy) {
		logger->Fatal("Synchronization policy load FAILED "
		              "(Error: missing plugin for [%s%s])",
		              sync_namespace.c_str(), sync_policy.c_str());
		assert(policy);
	}

	//---------- Setup all the module metrics
	mc.Register(metrics, SM_METRICS_COUNT);

}

SynchronizationManager::~SynchronizationManager()
{
}

bool
SynchronizationManager::Reshuffling(AppPtr_t papp)
{
	if ((papp->SyncState() == Schedulable::RECONF))
		return !(papp->SwitchingAWM());
	return false;
}

SynchronizationManager::ExitCode_t
SynchronizationManager::Sync_PreChange(Schedulable::SyncState_t syncState)
{
	ExitCode_t syncInProgress = NOTHING_TO_SYNC;
	AppsUidMapIt apps_it;

	typedef std::map<AppPtr_t, ApplicationProxy::pPreChangeRsp_t> RspMap_t;
	typedef std::pair<AppPtr_t, ApplicationProxy::pPreChangeRsp_t> RspMapEntry_t;

	ApplicationProxy::pPreChangeRsp_t presp;
	RspMap_t::iterator resp_it;
	RTLIB_ExitCode_t result;
	RspMap_t rsp_map;
	AppPtr_t papp;

	logger->Debug("Sync_PreChange: STEP 1 => START");
	SM_RESET_TIMING(sm_tmr);

	papp = am.GetFirst(syncState, apps_it);
	for ( ; papp; papp = am.GetNext(syncState, apps_it)) {

		if (!policy->DoSync(papp))
			continue;

		if (Reshuffling(papp) ||
		    papp->IsContainer()) {
			syncInProgress = OK;
			continue;
		}

		logger->Debug("Sync_PreChange: STEP 1 => [%s]", papp->StrId());

		// Do the minimum for disabled applications
		if (papp->Disabled()) {
			logger->Debug("Sync_PreChange: STEP 1: ignoring disabled EXC [%s]",
			              papp->StrId());
			syncInProgress = OK;
			continue;
		}

		// Pre-Change (just starting it if asynchronous)
		presp = ApplicationProxy::pPreChangeRsp_t(
		                new ApplicationProxy::preChangeRsp_t());
		result = ap.SyncP_PreChange(papp, presp);
		if (result != RTLIB_OK)
			continue;

		// This flag is set if there is at least one sync pending
		syncInProgress = OK;


// Pre-Change completion (just if asynchronous)
#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
		// Mapping the response future for responses collection
		rsp_map.insert(RspMapEntry_t(papp, presp));

	}

	// Collecting EXC responses
	resp_it = rsp_map.begin();
	while (resp_it != rsp_map.end()) {
		papp  = (*resp_it).first;
		presp = (*resp_it).second;
#endif
		// Jumping meanwhile disabled applications
		if (papp->Disabled()) {
			logger->Debug("Sync_PreChange: STEP 1: "
			              "ignoring (meanwhile) disabled EXC [%s]",
			              papp->StrId());
			continue;
		}

		Sync_PreChange_Check_EXC_Response(papp, presp);

// Pre-Change completion (just if asynchronous)
#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
		// Remove the respose future
		resp_it = rsp_map.erase(resp_it);
#else
		++resp_it;
#endif // CONFIG_BBQUE_YP_SASB_ASYNC

		// This is required just for clean compilation
		continue;

	}

	// Collecing execution metrics
	SM_GET_TIMING_SYNCSTATE(metrics, SM_SYNCP_TIME_PRECHANGE,
	                        sm_tmr, syncState);
	logger->Debug("Sync_PreChange: STEP 1 => DONE");

	if (syncInProgress == NOTHING_TO_SYNC)
		return NOTHING_TO_SYNC;

	return OK;
}

void SynchronizationManager::Sync_PreChange_Check_EXC_Response(
        AppPtr_t papp,
        ApplicationProxy::pPreChangeRsp_t presp)
{

	SynchronizationPolicyIF::ExitCode_t syncp_result;

// Pre-Change completion (just if asynchronous)
#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
	RTLIB_ExitCode_t result;
	logger->Debug("Sync_PreChange: STEP 1 => [%s] ... (wait) ... ",
	              papp->StrId());

	// Send RTLIB Sync-PreChange message
	result = ap.SyncP_PreChange_GetResult(presp);
	if (result == RTLIB_BBQUE_CHANNEL_TIMEOUT) {
		logger->Warn("Sync_PreChange: STEP 1 => [%s] TIMEOUT!",
		             papp->StrId());
		sync_fails_apps.insert(papp);
		return;
	}

	if (result == RTLIB_BBQUE_CHANNEL_WRITE_FAILED) {
		logger->Error("Sync_PreChange: STEP 1 => [%s] failed channel write [err=%d]",
		              papp->StrId(), result);
		sync_fails_apps.insert(papp);
		return;
	}

	if (result != RTLIB_OK) {
		logger->Error("Sync_PreChange: STEP 1 => [%s] library error occurred [err=%d]",
		              papp->StrId(), result);
		// FIXME This case should be handled
		assert(false);
	}

#endif
	logger->Debug("Sync_PreChange: STEP 1 => [%s] OK!",
	              papp->StrId());
	logger->Debug("Sync_PreChange: STEP 1 => [%s] sync_latency=%dms",
	              papp->StrId(), presp->syncLatency);

	// Collect stats on declared sync latency
	SM_ADD_SAMPLE(metrics, SM_SYNCP_APP_SYNCLAT, presp->syncLatency);

	syncp_result = policy->CheckLatency(papp, presp->syncLatency);
	UNUSED(syncp_result); // TODO: check the POLICY required action
}

SynchronizationManager::ExitCode_t
SynchronizationManager::Sync_SyncChange(
        Schedulable::SyncState_t syncState)
{
	AppsUidMapIt apps_it;

	typedef std::map<AppPtr_t, ApplicationProxy::pSyncChangeRsp_t> RspMap_t;
	typedef std::pair<AppPtr_t, ApplicationProxy::pSyncChangeRsp_t> RspMapEntry_t;

	ApplicationProxy::pSyncChangeRsp_t presp;
	RspMap_t::iterator resp_it;
	RTLIB_ExitCode_t result;
	RspMap_t rsp_map;
	AppPtr_t papp;

	logger->Debug("Sync_SyncChange: STEP 2 => START");
	SM_RESET_TIMING(sm_tmr);

	papp = am.GetFirst(syncState, apps_it);
	for ( ; papp; papp = am.GetNext(syncState, apps_it)) {

		if (!policy->DoSync(papp))
			continue;

		if (Reshuffling(papp) ||
		    papp->IsContainer())
			continue;

		logger->Debug("Sync_SyncChange: STEP 2 => [%s]", papp->StrId());

		// Jumping meanwhile disabled applications
		if (papp->Disabled()) {
			logger->Debug("Sync_SyncChange: STEP 2 => [%s] ignoring disabled EXC",
			              papp->StrId());
			continue;
		}

		// Sync-Change (just starting it if asynchronous)
		presp = ApplicationProxy::pSyncChangeRsp_t(
		                new ApplicationProxy::syncChangeRsp_t());
		result = ap.SyncP_SyncChange(papp, presp);
		if (result != RTLIB_OK)
			continue;

// Sync-Change completion (just if asynchronous)
#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
		// Mapping the response future for responses collection
		rsp_map.insert(RspMapEntry_t(papp, presp));
	}

	// Collecting EXC responses
	resp_it = rsp_map.begin();
	for (; resp_it != rsp_map.end(); ++resp_it) {
		papp  = (*resp_it).first;
		presp = (*resp_it).second;
#endif
		// Jumping meanwhile disabled applications
		if (papp->Disabled()) {
			logger->Debug("Sync_SyncChange: STEP 2 => [%s] "
			              "ignoring (meanwhile) disabled EXC",
			              papp->StrId());
			continue;
		}

		Sync_SyncChange_Check_EXC_Response(papp, presp);

// Sync-Change completion (just if asynchronous)
#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
		// Remove the respose future
		rsp_map.erase(resp_it);
#endif // CONFIG_BBQUE_YP_SASB_ASYNC

		// This is required just for clean compilation
		continue;

	}

	// Collecing execution metrics
	SM_GET_TIMING_SYNCSTATE(metrics, SM_SYNCP_TIME_SYNCCHANGE, sm_tmr, syncState);
	logger->Debug("Sync_SyncChange: STEP 2 => DONE ");

	return OK;
}

void SynchronizationManager::Sync_SyncChange_Check_EXC_Response(
        AppPtr_t papp,
        ApplicationProxy::pSyncChangeRsp_t presp)
{

// Sync-Change completion (just if asynchronous)
#ifdef CONFIG_BBQUE_YP_SASB_ASYNC
	RTLIB_ExitCode_t result;
	logger->Debug("Sync_SyncChange: STEP 2 => [%s] ... (wait)... ",
	              papp->StrId());

	// Send RTLIB Sync-Change message
	result = ap.SyncP_SyncChange_GetResult(presp);
	if (result == RTLIB_BBQUE_CHANNEL_TIMEOUT) {
		logger->Warn("Sync_SyncChange: STEP 2 => [%s] TIMEOUT! ",
		             papp->StrId());
		sync_fails_apps.insert(papp);
		SM_COUNT_EVENT(metrics, SM_SYNCP_SYNC_MISS);
		return;
	}

	if (result == RTLIB_BBQUE_CHANNEL_WRITE_FAILED) {
		logger->Error("Sync_SyncChange: STEP 2 => [%s] channel write error [err=%d]",
		              papp->StrId(), result);
		// Accounting for syncpoints missed
		sync_fails_apps.insert(papp);
		SM_COUNT_EVENT(metrics, SM_SYNCP_SYNC_MISS);
		return;
	}


	if (result != RTLIB_OK) {
		logger->Error("Sync_SyncChange: STEP 2 => [%s] library error [err=%d]",
		              papp->StrId(), result);
		// TODO Here the synchronization policy should be queryed to
		// decide if the synchronization latency is compliant with the
		// RTRM optimization goals.
		//
		DB(logger->Warn("TODO: Check sync policy for sync miss reaction"));
		sync_fails_apps.insert(papp);
	}
#endif
	// Accounting for syncpoints missed
	SM_COUNT_EVENT(metrics, SM_SYNCP_SYNC_HIT);
	logger->Debug("Sync_SyncChange: STEP 2 => OK!");
}

SynchronizationManager::ExitCode_t
SynchronizationManager::Sync_DoChange(Schedulable::SyncState_t syncState)
{
	AppsUidMapIt apps_it;

	RTLIB_ExitCode_t result;

	logger->Debug("Sync_DoChange: STEP 3 => START");
	SM_RESET_TIMING(sm_tmr);

	AppPtr_t papp = am.GetFirst(syncState, apps_it);
	for ( ; papp; papp = am.GetNext(syncState, apps_it)) {

		if (!policy->DoSync(papp))
			continue;

		if (Reshuffling(papp) ||
		    papp->IsContainer())
			continue;

		logger->Debug("Sync_DoChange: STEP 3 => [%s]", papp->StrId());

		// Jumping meanwhile disabled applications
		if (papp->Disabled()) {
			logger->Debug("Sync_DoChange: STEP 3 => [%s] ignoring disabled EXC",
			              papp->StrId());
			continue;
		}

		// Send a Do-Change
		result = ap.SyncP_DoChange(papp);
		if (result != RTLIB_OK)
			continue;

		logger->Debug("Sync_DoChange: STEP 3 => [%s] OK", papp->StrId());
	}

	// Collecing execution metrics
	SM_GET_TIMING_SYNCSTATE(metrics, SM_SYNCP_TIME_DOCHANGE, sm_tmr, syncState);
	logger->Debug("Sync_DoChange: STEP 3 => DONE");

	return OK;
}

SynchronizationManager::ExitCode_t
SynchronizationManager::Sync_PostChange(Schedulable::SyncState_t syncState)
{
	ApplicationProxy::pPostChangeRsp_t presp;
	AppsUidMapIt apps_it;
	AppPtr_t papp;
	uint8_t excs = 0;

	logger->Debug("Sync_PostChange: STEP 4 => START");
	SM_RESET_TIMING(sm_tmr);

	papp = am.GetFirst(syncState, apps_it);
	for ( ; papp; papp = am.GetNext(syncState, apps_it)) {
		logger->Debug("Sync_PostChange: STEP 4 => [%s]", papp->StrId());

		if (!policy->DoSync(papp))
			continue;

		// Skip failed synchronizations
		if (sync_fails_apps.find(papp) != sync_fails_apps.end()) {
			logger->Warn("Sync_PostChange: STEP 4 => [%s] skipped (sync failure)",
			             papp->StrId());
			continue;
		}

		// Commit changes if everything went fine
		SyncCommit(papp);
		logger->Debug("Sync_PostChange: STEP 4 => [%s] OK", papp->StrId());
		excs++;
	}

	// Collecing execution metrics
	SM_GET_TIMING_SYNCSTATE(metrics, SM_SYNCP_TIME_POSTCHANGE,
	                        sm_tmr, syncState);
	// Account for total reconfigured EXCs
	SM_COUNT_EVENT2(metrics, SM_SYNCP_EXCS, excs);
	// Collect statistics on average EXCSs reconfigured.
	SM_ADD_SAMPLE(metrics, SM_SYNCP_AVGE, excs);

	logger->Debug("Sync_PostChange: STEP 4 => DONE");

	return OK;
}


void SynchronizationManager::SyncCommit(AppPtr_t papp)
{
	logger->Debug("SyncCommit: [%s] is in %s/%s", papp->StrId(),
	              papp->StateStr(papp->State()),
	              papp->SyncStateStr(papp->SyncState()));

	// Acquiring the resources for RUNNING Applications
	if (!papp->Blocking() && !papp->Disabled()) {
		auto ra_result = ra.SyncAcquireResources(papp);
		if (ra_result != ResourceAccounter::RA_SUCCESS) {
			logger->Error("SyncCommit: [%s] failed (ret=%d)",
			              papp->StrId(), ra_result);
			am.SyncAbort(papp);
		}
	}

	// Committing change to the manager (to update queues)
	logger->Debug("SyncCommit: [%s] (adaptive) commit...", papp->StrId());
	am.SyncCommit(papp);
}


SynchronizationManager::ExitCode_t
SynchronizationManager::Sync_Platform(Schedulable::SyncState_t syncState)
{
	ExitCode_t result;
	bool at_least_one_success = false;

	logger->Debug("Sync_Platform <%s>: START adaptive applications",
	              Schedulable::SyncStateStr(syncState));
	SM_RESET_TIMING(sm_tmr);

	// Enforce resource assignments to applications
	AppsUidMapIt apps_it;
	AppPtr_t papp;
	papp = am.GetFirst(syncState, apps_it);
	for ( ; papp; papp = am.GetNext(syncState, apps_it)) {
		logger->Debug("Sync_Platform <%s>: [%s] ...",
		              papp->SyncStateStr(syncState), papp->StrId());
		if (!policy->DoSync(papp))
			continue;
		logger->Debug("Sync_Platform <%s>: [%s] to sync",
		              papp->SyncStateStr(syncState), papp->StrId());

		result = MapResources(papp);
		if (result != SynchronizationManager::OK) {
			logger->Error("Sync_Platform <%s>: [%s] failed [err=%d]",
			              papp->SyncStateStr(syncState),
			              papp->StrId(),
			              result);
			sync_fails_apps.insert(papp);
			continue;
		} else if (!at_least_one_success)
			at_least_one_success = true;

		logger->Debug("Sync_Platform <%s>: [%s] => OK",
		              papp->SyncStateStr(syncState),
		              papp->StrId());
	}

	// Collecting execution metrics
	SM_GET_TIMING_SYNCSTATE(metrics, SM_SYNCP_TIME_SYNCPLAT, sm_tmr, syncState);
	logger->Debug("Sync_Platform <%s>: DONE with adaptive applications",
	              papp->SyncStateStr(syncState));

	if (at_least_one_success)
		return OK;
	return PLATFORM_SYNC_FAILED;
}


SynchronizationManager::ExitCode_t
SynchronizationManager::MapResources(SchedPtr_t papp)
{
	PlatformManager::ExitCode_t result = PlatformManager::PLATFORM_OK;
	logger->Debug("MapResources <%s>: [%s] resource mapping...",
	              papp->SyncStateStr(papp->SyncState()),
	              papp->StrId());

	// Check the status before the scheduling in order to identify
	// restoring or thawed applications/processes
	auto pre_sync_state = papp->PreSyncState();
	logger->Debug("MapResources <%s>: [%s] pre-sync-state: %s",
	              papp->SyncStateStr(papp->SyncState()),
	              papp->StrId(),
	              papp->StateStr(pre_sync_state));

	// To restore?
	if (pre_sync_state == Schedulable::RESTORING) {
		logger->Debug("MapResources <%s>: [%s] restoring...",
		              papp->SyncStateStr(papp->SyncState()),
		              papp->StrId());

		auto ret = plm.Restore(papp->Pid(), papp->Name());
		if (ret != ReliabilityActionsIF::ExitCode_t::OK) {
			logger->Error("MapResources: [%s] restore "
			              "failed. Skipping...",
			              papp->StrId());
		}
	}
	// Was frozen?
	else if (pre_sync_state == Schedulable::THAWED) {
		logger->Debug("MapResources <%s>: [%s] thawing...",
		              papp->SyncStateStr(papp->SyncState()),
		              papp->StrId());
		plm.Thaw(papp);
	}

	// Synchronization of the scheduling decision
	switch (papp->SyncState()) {
	case Schedulable::STARTING:
	case Schedulable::RECONF:
	case Schedulable::MIGREC:
	case Schedulable::MIGRATE:
		result = plm.MapResources(
		                 papp,
		                 papp->NextAWM()->GetResourceBinding());
		break;

	case Schedulable::BLOCKED:
		logger->Debug("MapResources <%s>: [%s] reclaiming resources ",
		              papp->SyncStateStr(papp->SyncState()),
		              papp->StrId());
		result = plm.ReclaimResources(papp);
		break;

	case Schedulable::DISABLED:
		logger->Debug("MapResources <%s>: [%s] resources already reclaimed",
		              papp->SyncStateStr(papp->SyncState()),
		              papp->StrId());
		plm.Release(papp);
		break;

	default:
		break;
	}

	if (result != PlatformManager::PLATFORM_OK
	    && (kill(papp->Pid(), 0) == 0)) {
		logger->Warn("MapResources <%s>: [%s] failure occurred [ret=%d]",
		             papp->SyncStateStr(papp->SyncState()),
		             papp->StrId(),
		             result);

		return PLATFORM_SYNC_FAILED;
	}
	return OK;
}


SynchronizationManager::ExitCode_t
SynchronizationManager::SyncApps(Schedulable::SyncState_t syncState)
{
	ExitCode_t result;

	if (!am.HasApplications(syncState)) {
		logger->Warn("SyncApps: no adaptive applications");
		assert(syncState != Schedulable::SYNC_NONE);
		return NOTHING_TO_SYNC;
	}

#ifdef CONFIG_BBQUE_YM_SYNC_FORCE
	SynchronizationPolicyIF::SyncLatency_t syncLatency;

	result = Sync_PreChange(syncState);
	if (result != OK)
		return result;

	syncLatency = policy->EstimatedSyncTime();
	SM_ADD_SAMPLE(metrics, SM_SYNCP_TIME_LATENCY, syncLatency);

	// Wait for the policy specified sync point
	logger->Debug("SyncApps: wait sync point for %d[ms]", syncLatency);
	std::this_thread::sleep_for(
	        std::chrono::milliseconds(syncLatency));

	result = Sync_SyncChange(syncState);
	if (result != OK) {
		logger->Debug("SyncApps: returning after sync-change");
		return result;
	}

	result = Sync_Platform(syncState);
	if (result != OK) {
		logger->Debug("SyncApps: returning after sync-platform");
		return result;
	}

	result = Sync_DoChange(syncState);
	if (result != OK) {
		logger->Debug("SyncApps: returning after sync-dochange");
		return result;
	}

#else
	// Platform is synched before to:
	// 1. speed-up resources assignement
	// 2. properly setup platform specific data for the time the
	// application reconfigure it-self
	// e.g. CGroups should be already properly initialized
	result = Sync_Platform(syncState);
	if (result != OK) {
		logger->Debug("SyncApps: returning after sync-platform");
		return result;
	}

	result = Sync_PreChange(syncState);
	if (result != OK) {
		logger->Debug("SyncApps: returning after sync-prechange");
		return result;
	}

#endif // CONFIG_BBQUE_YM_SYNC_FORCE

	result = Sync_PostChange(syncState);
	if (result != OK) {
		logger->Debug("SyncApps: returning after sync-postchange");
		return result;
	}

	return OK;
}


SynchronizationManager::ExitCode_t
SynchronizationManager::SyncSchedule()
{
	Schedulable::SyncState_t syncState;
	ResourceAccounter::ExitCode_t raResult;
	bu::Timer syncp_tmr;
	ExitCode_t result;
	// TODO add here proper tracing/monitoring events for statistics
	// collection

	// Update session count
	++sync_count;
	logger->Notice("SyncSchedule: synchronization [%d] START, policy [%s]",
	               sync_count, policy->Name());
	am.PrintStatusQ();
	am.PrintSyncQ();
	SM_COUNT_EVENT(metrics, SM_SYNCP_RUNS); // Account for SyncP runs
	SM_RESET_TIMING(syncp_tmr);             // Reset the SyncP overall timer

	// TODO here a synchronization decision policy is used
	// to decide if a synchronization should be run or not, e.g. based on
	// the kind of applications in SYNC state or considering stability
	// problems and synchronization overheads.
	// The role of the SynchronizationManager is quite simple: it calls a
	// policy provided method which should decide what applications must be
	// synched. As soon as the queue of apps to sync returned is empty, the
	// syncronization is considered terminated and will start again only at
	// the next synchronization event.
	logger->Debug("SyncSchedule: getting the applications queue...");
	syncState = policy->GetApplicationsQueue(sv, true);
	if (syncState == Schedulable::SYNC_NONE) {
		logger->Info("SyncSchedule: session=%d ABORTED", sync_count);
		// Possibly this should never happens
		assert(syncState != Schedulable::SYNC_NONE);
		return OK;
	}

	// Start the resource accounter synchronized session
	logger->Debug("SyncSchedule: starting the synchronization...");
	raResult = ra.SyncStart();
	if (raResult != ResourceAccounter::RA_SUCCESS) {
		logger->Fatal("SyncSchedule: session=%d unable to start "
		              "resource accounting ",
		              sync_count);
		return ABORTED;
	}

	while (syncState != Schedulable::SYNC_NONE) {
		// Synchronize the adaptive applications/EXC in order of status
		// as returned by the policy
		logger->Debug("SyncSchedule: adaptive applications <%s>...",
		              app::Schedulable::SyncStateStr(syncState));

		result = SyncApps(syncState);
		if ((result != NOTHING_TO_SYNC) && (result != OK)) {
			logger->Warn("SyncSchedule: session %d: not possible "
			             "to sync <%s> applications...",
			             sync_count, app::Schedulable::SyncStateStr(syncState));
			syncState = policy->GetApplicationsQueue(sv);
			continue;
		}

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
		// Synchronization of generic processes
		logger->Debug("SyncSchedule: not integrated processes <%s>...",
		              app::Schedulable::SyncStateStr(syncState));
		result = SyncProcesses();
		if ((result != NOTHING_TO_SYNC) && (result != OK)) {
			logger->Warn("SyncSchedule: session %d: not possible "
			             "to sync <%s> process...",
			             sync_count, app::Schedulable::SyncStateStr(syncState));
			syncState = policy->GetApplicationsQueue(sv);
			continue;
		}
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

		// Next set of applications to synchronize (if any)
		syncState = policy->GetApplicationsQueue(sv);
	}

	// Commit the resource accounter synchronized session
	raResult = ra.SyncCommit();
	if (raResult != ResourceAccounter::RA_SUCCESS) {
		logger->Fatal("SyncSchedule: session=%d resource accounting commit failed",
		              sync_count);
		return ABORTED;
	}
	SM_GET_TIMING(metrics, SM_SYNCP_TIME, syncp_tmr); // Overall SyncP execution time
	SM_COUNT_EVENT(metrics, SM_SYNCP_COMP);           // Account for SyncP completed

	DisableFailedApps();
	logger->Notice("SyncSchedule: session=%d DONE", sync_count);
	am.PrintStatusQ();
	am.PrintSyncQ();
	return OK;
}

void SynchronizationManager::DisableFailedApps()
{
	for (auto papp : sync_fails_apps) {
		logger->Warn("DisableFailedApps: checking [%s] after sync failure",
		             papp->StrId());
		if (!am.CheckEXC(papp, true)) {
			logger->Warn("DisableFailedApps: [%s] is alive",
			             papp->StrId());
		} else {
			logger->Warn("DisableFailedApps: [%s] forced to DISABLE",
			             papp->StrId());
			am.DisableEXC(papp, true);
		}
	}
	sync_fails_apps.clear();
}


#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER

SynchronizationManager::ExitCode_t
SynchronizationManager::SyncProcesses()
{
	// Perform resource mapping
	logger->Debug("SyncProcesses: platform mapping...");
	ExitCode_t result = Sync_PlatformForProcesses();
	if (result != OK) {
		logger->Debug("SyncProcesses: exit code: %d", result);
		return result;
	}
	// Commit changes
	logger->Debug("SyncProcesses: post-change commit...");
	return Sync_PostChangeForProcesses();
}

SynchronizationManager::ExitCode_t
SynchronizationManager::Sync_PostChangeForProcesses()
{
	logger->Debug("STEP 4.2: postChange() START: processes");

	// Commit SYNC -> RUNNING
	ProcessMapIterator procs_it;
	ProcPtr_t proc = prm.GetFirst(Schedulable::SYNC, procs_it);
	for ( ; proc; proc = prm.GetNext(Schedulable::SYNC, procs_it)) {
		SyncCommit(proc);
		logger->Info("STEP 4.2: <--------- OK -- [%s]", proc->StrId());
	}

	// Commit FINISHED -> <removed>
	proc = prm.GetFirst(Schedulable::FINISHED, procs_it);
	for ( ; proc; proc = prm.GetNext(Schedulable::FINISHED, procs_it)) {
		logger->Info("STEP 4.2: <---- RELEASED -- [%s]", proc->StrId());
		SyncCommit(proc);
	}

	logger->Debug("STEP 4.2: postChange() DONE: processes");
	return OK;
}

SynchronizationManager::ExitCode_t
SynchronizationManager::Sync_PlatformForProcesses()
{
	ExitCode_t result;
	bool at_least_one_success = false;

	logger->Debug("STEP M.2: SyncPlatform() START: processes");
	SM_RESET_TIMING(sm_tmr);

	if (!prm.HasProcesses(Schedulable::SYNC)) {
		logger->Debug("STEP M.2: SyncPlatform() NONE: no processes");
		return NOTHING_TO_SYNC;
	}

	ProcessMapIterator procs_it;
	ProcPtr_t proc = prm.GetFirst(Schedulable::SYNC, procs_it);
	for ( ; proc; proc = prm.GetNext(Schedulable::SYNC, procs_it)) {
		result = MapResources(proc);
		if (result != SynchronizationManager::OK) {
			logger->Error("STEP M.2: cannot synchronize application [%s]", proc->StrId());
			sync_fails_procs.insert(proc);
			continue;
		} else if (!at_least_one_success)
			at_least_one_success = true;
		logger->Info("STEP M.2: <--------- OK -- [%s]", proc->StrId());
	}

	// Collecting execution metrics
	logger->Debug("STEP M.2: SyncPlatform() DONE: processes");
	if (at_least_one_success)
		return OK;
	return PLATFORM_SYNC_FAILED;
}

void SynchronizationManager::SyncCommit(ProcPtr_t proc)
{
	logger->Debug("SyncCommit: [%s] is in %s/%s", proc->StrId(),
	              proc->StateStr(proc->State()),
	              proc->SyncStateStr(proc->SyncState()));

	// Acquiring the resources for RUNNING Applications
	if (!proc->Blocking() && !proc->Disabled()) {
		auto ra_result = ra.SyncAcquireResources(proc);
		if (ra_result != ResourceAccounter::RA_SUCCESS) {
			logger->Error("SyncCommit: failed for [%s] (ret=%d)",
			              proc->StrId(), ra_result);
			prm.SyncAbort(proc);
		}
	}

	// Committing change to the manager (to update queues)
	logger->Debug("SyncCommit: [%s] (process) commit...", proc->StrId());
	prm.SyncCommit(proc);
}

void SynchronizationManager::DisableFailedProcesses()
{
	for (auto proc : sync_fails_procs) {
		logger->Warn("DisableFailedProcesses: disabling [%s] due to failure",
		             proc->StrId());
		//	prm.NotifyStop(proc);
		// TODO what to do for disabling?
	}
}

#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

} // namespace bbque
