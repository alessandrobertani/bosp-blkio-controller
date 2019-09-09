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

#include "sasb_syncpol.h"

#include "bbque/synchronization_manager.h"
#include "bbque/modules_factory.h"

#include <iostream>

/** Metrics (class COUNTER) declaration */
#define SM_COUNTER_METRIC(NAME, DESC)\
 {SYNCHRONIZATION_MANAGER_NAMESPACE "." SYNCHRONIZATION_POLICY_NAME "." NAME,\
	 DESC, MetricsCollector::COUNTER, 0, NULL, 0}
/** Increase counter for the specified metric */
#define SM_COUNT_EVENT(METRICS, INDEX) \
	mc.Count(METRICS[INDEX].mh);

/** Metrics (class SAMPLE) declaration */
#define SM_SAMPLE_METRIC(NAME, DESC)\
 {SYNCHRONIZATION_MANAGER_NAMESPACE "." SYNCHRONIZATION_POLICY_NAME "." NAME,\
	 DESC, MetricsCollector::SAMPLE, 0, NULL, 0}
/** Reset the timer used to evaluate metrics */
#define SM_START_TIMER(TIMER) \
	TIMER.start();
/** Acquire a new completion time sample */
#define SM_GET_TIMING(METRICS, INDEX, TIMER) \
	if (TIMER.Running()) {\
		mc.AddSample(METRICS[INDEX].mh, TIMER.getElapsedTimeMs());\
		TIMER.stop();\
	}

namespace bu = bbque::utils;

namespace bbque { namespace plugins {

/* Definition of metrics used by this module */
MetricsCollector::MetricsCollection_t
SasbSyncPol::metrics[SM_METRICS_COUNT] = {
	//----- Event counting metrics
	SM_COUNTER_METRIC("runs", "SASB SyncP executions count"),
	//----- Timing metrics
	SM_SAMPLE_METRIC("start", "START queue sync t[ms]"),
	SM_SAMPLE_METRIC("rec",   "RECONF queue sync t[ms]"),
	SM_SAMPLE_METRIC("mreg",  "MIGREC queue sync t[ms]"),
	SM_SAMPLE_METRIC("mig",   "MIGRATE queue sync t[ms]"),
	SM_SAMPLE_METRIC("block", "BLOCKED queue sync t[ms]"),
};

SasbSyncPol::SasbSyncPol() :
	mc(bu::MetricsCollector::GetInstance()) {

	// Get a logger
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);

	//---------- Setup all the module metrics
	mc.Register(metrics, SM_METRICS_COUNT);

	assert(logger);
	logger->Debug("Built SASB SyncPol object @%p", (void*)this);

}

SasbSyncPol::~SasbSyncPol() {
}

//----- Scheduler policy module interface

char const *SasbSyncPol::Name() {
	return SYNCHRONIZATION_POLICY_NAME;
}


ba::Schedulable::SyncState_t SasbSyncPol::step1(
			bbque::System & sv) {

	logger->Debug("STEP 1.0: Running => Disabled");
	if (sv.HasSchedulables(ba::Schedulable::DISABLED)) {
		status = STEP11;
		return ba::Schedulable::DISABLED;
	}

	logger->Debug("STEP 1.1: Running => Blocked");
	if (sv.HasSchedulables(ba::Schedulable::BLOCKED))
		return ba::Schedulable::BLOCKED;

	logger->Debug("STEP 1.0:            "
			"No EXCs to be BLOCKED");
	return ba::Schedulable::SYNC_NONE;
}

ba::Schedulable::SyncState_t SasbSyncPol::step2(
			bbque::System & sv) {
	ba::Schedulable::SyncState_t syncState;

	switch(status) {
	case STEP21:
		logger->Debug("STEP 2.1: Running => Migration (lower value)");
		syncState = ba::Schedulable::MIGRATE;
		break;

	case STEP22:
		logger->Debug("STEP 2.2: Running => Migration/Reconf (lower value)");
		syncState = ba::Schedulable::MIGREC;
		break;

	case STEP23:
		logger->Debug("STEP 2.3: Running => Reconf (lower value)");
		syncState = ba::Schedulable::RECONF;
		break;

	default:
		// We should never got here
		syncState = ba::Schedulable::SYNC_NONE;
		assert(false);
	}

	if (sv.HasSchedulables(syncState))
		return syncState;

	logger->Debug("STEP 2.0:            "
			"No EXCs to be reschedule (lower value)");
	return ba::Schedulable::SYNC_NONE;
}


ba::Schedulable::SyncState_t SasbSyncPol::step3(
			bbque::System & sv) {
	ba::Schedulable::SyncState_t syncState;

	switch(status) {
	case STEP31:
		logger->Debug("STEP 3.1: Running => Migration (higher value)");
		syncState = ba::Schedulable::MIGRATE;
		break;

	case STEP32:
		logger->Debug("STEP 3.2: Running => Migration/Reconf (higher value)");
		syncState = ba::Schedulable::MIGREC;
		break;

	case STEP33:
		logger->Debug("STEP 3.3: Running => Reconf (higher value)");
		syncState = ba::Schedulable::RECONF;
		break;

	default:
		// We should never got here
		syncState = ba::Schedulable::SYNC_NONE;
		assert(false);
	}

	if (sv.HasSchedulables(syncState))
		return syncState;

	logger->Debug("STEP 3.0:            "
			"No EXCs to be reschedule (higher value)");
	return ba::Schedulable::SYNC_NONE;
}

ba::Schedulable::SyncState_t SasbSyncPol::step4(
			bbque::System & sv) {

	logger->Debug("STEP 4.0: Ready   => Running");
	if (sv.HasSchedulables(ba::Schedulable::STARTING))
		return ba::Schedulable::STARTING;

	logger->Debug("STEP 4.0:            "
			"No EXCs to be started");
	return ba::Schedulable::SYNC_NONE;
}

ba::Schedulable::SyncState_t SasbSyncPol::GetApplicationsQueue(
			bbque::System & sv, bool restart) {
	static ba::Schedulable::SyncState_t servedSyncState;
	ba::Schedulable::SyncState_t syncState;

	// Get timings for previously synched queue
	if (servedSyncState != ba::Schedulable::SYNC_NONE) {
		SM_GET_TIMING(metrics, SM_SASB_TIME_START + \
				(servedSyncState - ba::Schedulable::STARTING),
				sm_tmr);
	}

	// Ensure to do one step at each entry
	++status;

	// Eventaully restart if the sync protocol ask to start from scratch
	if (restart) {
		logger->Debug("Resetting sync status");
		servedSyncState = ba::Schedulable::SYNC_NONE;
		status = STEP10;
		// Account for Policy runs
		SM_COUNT_EVENT(metrics, SM_SASB_RUNS);
	}

	// Resetting the maximum latency since a new queue is going to be served,
	// thus a new SyncP is going to start
	max_latency = 0;

	bool do_sync = false;

	syncState = ba::Schedulable::SYNC_NONE;
	for( ; status<=STEP40 && !do_sync; ++status) {
		switch(status) {
		case STEP10:
		case STEP11:
			syncState = step1(sv);
			if (syncState != ba::Schedulable::SYNC_NONE)
				do_sync = true;
			continue;
		case STEP21:
		case STEP22:
		case STEP23:
			syncState = step2(sv);
			if (syncState != ba::Schedulable::SYNC_NONE)
				do_sync = true;
			continue;
		case STEP31:
		case STEP32:
		case STEP33:
			syncState = step3(sv);
			if (syncState != ba::Schedulable::SYNC_NONE)
				do_sync = true;

			continue;
		case STEP40:
			syncState = step4(sv);
			if (syncState != ba::Schedulable::SYNC_NONE)
				do_sync = true;
			continue;
		};
	}

	if (! do_sync) {
		servedSyncState = ba::Schedulable::SYNC_NONE;
		return servedSyncState;
	}

	if (status <= STEP40) {
		// We have to go back to one status,
		// since we have exited with
		// syncState != ba::Schedulable::SYNC_NONE
		// and the status variable was incremented one
		// time more than correct
		status--;
	}

	servedSyncState = syncState;
	SM_START_TIMER(sm_tmr);
	return syncState;

}

bool SasbSyncPol::DoSync(AppPtr_t papp) {
	bool reconf = true;
	logger->Debug("Checking [%s] @ step [%d]: sync_state [%s]",
			papp->StrId(),
			status,
			papp->SyncStateStr(papp->SyncState())
		    );

	// Steps specific synchronization hinibitors
	switch(status) {

	// STEP 1
	// Blocked applications are always authorized
	case STEP10:
	case STEP11:
		return true;

	// STEP 2
	// reconfigure just apps which lower their AWM value since,
	// in general, the lower the AWM value => the lower the resources
	case STEP21:
		reconf &= (papp->SyncState() == ApplicationStatusIF::MIGRATE);
		break;
	case STEP22:
		reconf &= (papp->SyncState() == ApplicationStatusIF::MIGREC);
		break;
	case STEP23:
		reconf &= (papp->SyncState() == ApplicationStatusIF::RECONF);
		break;

	// STEP 3
	case STEP31:
		reconf &= (papp->SyncState() == ba::Schedulable::MIGRATE);
		break;
	case STEP32:
		reconf &= (papp->SyncState() == ba::Schedulable::MIGREC);
		break;
	case STEP33:
		reconf &= (papp->SyncState() == ba::Schedulable::RECONF);
		break;

	// Just for compilation warnings
	default:
		;
	};

	if (papp->CurrentAWM() && papp->NextAWM()) {
		logger->Debug("Checking [%s] @ step [%d]: sync_state [%d], curr_awm [%02d], next_awm [%02d] => %s",
				papp->StrId(),
				status,
				papp->SyncState(),
				papp->CurrentAWM()->Id(),
				papp->NextAWM()->Id(),
				reconf ? "SYNC" : "SKYP"
			    );
	}
	else if (papp->NextAWM()) {
		logger->Debug("Checking [%s] @ step [%d]: sync_state [%d], curr_awm [--], next_awm [%02d] => %s",
				papp->StrId(),
				status,
				papp->SyncState(),
				papp->NextAWM()->Id(),
				reconf ? "SYNC" : "SKYP"
			    );
	}
	else {
		logger->Debug("Checking [%s] @ step [%d]: sync_state [%d] => %s",
				papp->StrId(),
				status,
				papp->SyncState(),
				reconf ? "SYNC" : "SKYP"
			    );

	}

	return reconf;
}

SasbSyncPol::ExitCode_t
SasbSyncPol::CheckLatency(AppPtr_t papp, SyncLatency_t latency) {
	// TODO: use a smarter latency validation, e.g. considering the
	// application and the currently served queue
	DB(logger->Warn("TODO: Check for [%s] (%d[ms]) syncLatency compliance",
			papp->StrId(), latency));

	// Right now we use a dummy approach based on WORST CASE.
	// Indeed, we keep the maximum required lantecy among all the applications
	// since the last GetApplicationsQueue
	if (max_latency < latency)
		max_latency = latency;

	return SYNCP_OK;
}

SasbSyncPol::SyncLatency_t
SasbSyncPol::EstimatedSyncTime() {

	// TODO use a smarter latency estimation, e.g. based on a timer which
	// should be started at each first CheckLatency right after each new
	// GetApplicationsQueue

	// Right now we use a dummy approach based on WORT CASE.
	// Indeet we alwasy return the maximum latency collected among all the
	// applciations.
	return max_latency;
}


//----- static plugin interface

void * SasbSyncPol::Create(PF_ObjectParams *) {
	return new SasbSyncPol();
}

int32_t SasbSyncPol::Destroy(void * plugin) {
  if (!plugin)
    return -1;
  delete (SasbSyncPol *)plugin;
  return 0;
}

} // namesapce plugins

} // namespace bbque

