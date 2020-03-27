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

#include "bbque/scheduler_manager.h"

#include "bbque/configuration_manager.h"
#include "bbque/plugin_manager.h"
#include "bbque/modules_factory.h"
#include "bbque/system.h"

#include "bbque/utils/utility.h"

// The prefix for configuration file attributes
#define MODULE_CONFIG "SchedulerManager"

/** Metrics (class COUNTER) declaration */
#define SM_COUNTER_METRIC(NAME, DESC)\
 {SCHEDULER_MANAGER_NAMESPACE "." NAME, DESC, \
	 MetricsCollector::COUNTER, 0, NULL, 0}
/** Increase counter for the specified metric */
#define SM_COUNT_EVENT(METRICS, INDEX) \
	mc.Count(METRICS[INDEX].mh);
/** Increase counter for the specified metric */
#define SM_COUNT_EVENTS(METRICS, INDEX, AMOUNT) \
	mc.Count(METRICS[INDEX].mh, AMOUNT);

/** Metrics (class SAMPLE) declaration */
#define SM_SAMPLE_METRIC(NAME, DESC)\
 {SCHEDULER_MANAGER_NAMESPACE "." NAME, DESC, \
	 MetricsCollector::SAMPLE, 0, NULL, 0}
/** Reset the timer used to evaluate metrics */
#define SM_RESET_TIMING(TIMER) \
	TIMER.start();
/** Acquire a new completion time sample */
#define SM_GET_TIMING(METRICS, INDEX, TIMER) \
	mc.AddSample(METRICS[INDEX].mh, TIMER.getElapsedTimeMs());
/** Acquire a new EXC reconfigured sample */
#define SM_ADD_SCHED(METRICS, INDEX, COUNT) \
	mc.AddSample(METRICS[INDEX].mh, COUNT);

namespace br = bbque::res;
namespace bu = bbque::utils;
namespace bp = bbque::plugins;
namespace po = boost::program_options;

namespace bbque {

/* Definition of metrics used by this module */
MetricsCollector::MetricsCollection_t
SchedulerManager::metrics[SM_METRICS_COUNT] = {
	//----- Event counting metrics
	SM_COUNTER_METRIC("runs",	"Scheduler executions count"),
	SM_COUNTER_METRIC("comp",	"Scheduler completions count"),
	SM_COUNTER_METRIC("start",	"START count"),
	SM_COUNTER_METRIC("reconf",	"RECONF count"),
	SM_COUNTER_METRIC("migrate",    "MIGRATE count"),
	SM_COUNTER_METRIC("migrec",	"MIGREC count"),
	SM_COUNTER_METRIC("block",	"BLOCK count"),
	//----- Timing metrics
	SM_SAMPLE_METRIC("time",	"Scheduler execution t[ms]"),
	SM_SAMPLE_METRIC("period",	"Scheduler activation period t[ms]"),
	//----- Counting statistics
	SM_SAMPLE_METRIC("avg.start",	"Avg START per schedule"),
	SM_SAMPLE_METRIC("avg.reconf",	"Avg RECONF per schedule"),
	SM_SAMPLE_METRIC("avg.migrec",	"Avg MIGREC per schedule"),
	SM_SAMPLE_METRIC("avg.migrate",	"Avg MIGRATE per schedule"),
	SM_SAMPLE_METRIC("avg.block",	"Avg BLOCK per schedule"),

};

SchedulerManager & SchedulerManager::GetInstance()
{
	static SchedulerManager rs;
	return rs;
}

SchedulerManager::SchedulerManager() :
    am(ApplicationManager::GetInstance()),
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
    prm(ProcessManager::GetInstance()),
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER
    mc(bu::MetricsCollector::GetInstance()),
#ifdef CONFIG_BBQUE_DM
    dm(DataManager::GetInstance()),
#endif
    sched_count(0)
{
	std::string opt_namespace((SCHEDULER_POLICY_NAMESPACE"."));
	std::string opt_policy;

	//---------- Get a logger module
	logger = bu::Logger::GetLogger(SCHEDULER_MANAGER_NAMESPACE);
	assert(logger);

	logger->Debug("Starting resource scheduler...");
	//---------- Loading module configuration
	ConfigurationManager & cm = ConfigurationManager::GetInstance();
	po::options_description opts_desc("Resource Scheduler Options");
	opts_desc.add_options()
		(MODULE_CONFIG".policy",
		po::value<std::string>(
		&opt_policy)->default_value(BBQUE_SCHEDPOL_DEFAULT),
		"The optimization policy to use");
	po::variables_map opts_vm;
	cm.ParseConfigurationFile(opts_desc, opts_vm);

	//---------- Load the required optimization plugin
	logger->Info("Loading optimization policy [%s%s]...",
		opt_namespace.c_str(), opt_policy.c_str());
	policy = ModulesFactory::GetModule<bp::SchedulerPolicyIF>(
		opt_namespace + opt_policy);
	if (!policy) {
		logger->Fatal("Optimization policy load FAILED "
			"(Error: missing plugin for [%s%s])",
			opt_namespace.c_str(), opt_policy.c_str());
		assert(policy);
	}

	//---------- Setup all the module metrics
	mc.Register(metrics, SM_METRICS_COUNT);

}

SchedulerManager::~SchedulerManager() { }

#define SM_COLLECT_STATS(STATE) \
	count = am.AppsCount(Schedulable::STATE);\
	SM_COUNT_EVENTS(metrics, SM_SCHED_ ## STATE, count);\
	SM_ADD_SCHED(metrics, SM_SCHED_AVG_ ## STATE, (double)count);

void
SchedulerManager::CollectStats()
{
	uint16_t count;

	// Account for scheduling decisions
	SM_COLLECT_STATS(STARTING);
	SM_COLLECT_STATS(RECONF);
	SM_COLLECT_STATS(MIGREC);
	SM_COLLECT_STATS(MIGRATE);
	SM_COLLECT_STATS(BLOCKED);

}

SchedulerManager::ExitCode_t
SchedulerManager::Schedule()
{

	if (!policy) {
		logger->Crit("Resource scheduling FAILED (Error: missing policy)");
		assert(policy);
		return MISSING_POLICY;
	}

	// TODO add here proper tracing/monitoring events
	// for statistics collection

	// TODO here should be plugged a scheduling decision policy
	// Such a policy should decide whether a scheduling should be run
	// or not, e.g. based on the kind of READY applications or considering
	// stability problems and scheduling overheads.
	// In case of a scheduling is not considered safe proper at this time,
	// a DELAYED exit code should be returned
	DB(logger->Warn("TODO: add scheduling activation policy"));

	// Check the life status of integrated applications
	am.CheckActiveEXCs();

	// --> Applications from now in a not consistent state
	SetState(State_t::SCHEDULING);
	++sched_count;

	// Metrics: scheduling runs count and timing
	if (sched_count > 1)
		SM_GET_TIMING(metrics, SM_SCHED_PERIOD, sm_tmr);
	SM_COUNT_EVENT(metrics, SM_SCHED_RUNS);
	SM_RESET_TIMING(sm_tmr);

	// Status view to fill with scheduling
	System &sv = System::GetInstance();
	br::RViewToken_t sched_view_id;

	logger->Notice("Scheduling [%d] START, policy [%s]",
		sched_count, policy->Name());
	SchedulerPolicyIF::ExitCode result = policy->Schedule(sv, sched_view_id);
	if (result != SchedulerPolicyIF::SCHED_DONE) {
		logger->Error("Scheduling [%d] FAILED: error=%d",
			sched_count, result);
		// --> Applications in a consistent state again
		SetState(State_t::READY);
		return FAILED;
	}

	// Clear the next AWM from the RUNNING Apps/EXC
	CommitRunningApplications();

	// Set the scheduled resource view
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	ra.SetScheduledView(sched_view_id);

	// --> Applications in a consistent state again
	SetState(State_t::READY);

	SM_GET_TIMING(metrics, SM_SCHED_TIME, sm_tmr);
	SM_RESET_TIMING(sm_tmr);
	SM_COUNT_EVENT(metrics, SM_SCHED_COMP);
	CollectStats();

#ifdef CONFIG_BBQUE_DM
	dm.NotifyUpdate(stat::EVT_SCHEDULING);
#endif

	logger->Notice("Scheduling [%d] DONE", sched_count);

	return DONE;
}

void SchedulerManager::CommitRunningApplications()
{
	// Running (AEM) applications
	AppsUidMapIt apps_it;
	AppPtr_t papp = am.GetFirst(Schedulable::RUNNING, apps_it);
	for (; papp; papp = am.GetNext(Schedulable::RUNNING, apps_it)) {
		am.SyncContinue(papp);
	}

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	// Running processes
	ProcessMapIterator proc_it;
	ProcPtr_t proc = prm.GetFirst(Schedulable::RUNNING, proc_it);
	for (; proc; proc = prm.GetNext(Schedulable::RUNNING, proc_it)) {
		prm.SyncContinue(proc);
	}
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER
}

void SchedulerManager::SetState(State_t _s)
{
	std::unique_lock<std::mutex> ul(mux);
	state = _s;
	status_cv.notify_all();
}

void SchedulerManager::WaitForReady()
{
	std::unique_lock<std::mutex> ul(mux);
	while (state != State_t::READY)
		status_cv.wait(ul);
	logger->Debug("State: READY");
	status_cv.notify_all();
}


} // namespace bbque

