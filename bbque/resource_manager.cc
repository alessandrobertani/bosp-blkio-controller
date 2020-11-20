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


#include "bbque/resource_manager.h"

#include "bbque/configuration_manager.h"
#include "bbque/signals_manager.h"
#include "bbque/utils/utility.h"

#ifdef CONFIG_BBQUE_WM
#include "bbque/power_monitor.h"
#endif

#define RESOURCE_MANAGER_NAMESPACE "bq.rm"
#define MODULE_NAMESPACE RESOURCE_MANAGER_NAMESPACE

/** Metrics (class COUNTER) declaration */
#define RM_COUNTER_METRIC(NAME, DESC)\
	{RESOURCE_MANAGER_NAMESPACE "." NAME, DESC, \
		MetricsCollector::COUNTER, 0, NULL, 0}
/** Increase counter for the specified metric */
#define RM_COUNT_EVENT(METRICS, INDEX) \
	mc.Count(METRICS[INDEX].mh);
/** Increase counter for the specified metric */
#define RM_COUNT_EVENTS(METRICS, INDEX, AMOUNT) \
	mc.Count(METRICS[INDEX].mh, AMOUNT);

/** Metrics (class SAMPLE) declaration */
#define RM_SAMPLE_METRIC(NAME, DESC)\
	{RESOURCE_MANAGER_NAMESPACE "." NAME, DESC, \
		MetricsCollector::SAMPLE, 0, NULL, 0}
/** Reset the timer used to evaluate metrics */
#define RM_RESET_TIMING(TIMER) \
	TIMER.start();
/** Acquire a new time sample */
#define RM_GET_TIMING(METRICS, INDEX, TIMER) \
	mc.AddSample(METRICS[INDEX].mh, TIMER.getElapsedTimeMs());
/** Acquire a new (generic) sample */
#define RM_ADD_SAMPLE(METRICS, INDEX, SAMPLE) \
	mc.AddSample(METRICS[INDEX].mh, SAMPLE);

/** Metrics (class PERIDO) declaration */
#define RM_PERIOD_METRIC(NAME, DESC)\
	{RESOURCE_MANAGER_NAMESPACE "." NAME, DESC, \
		MetricsCollector::PERIOD, 0, NULL, 0}
/** Acquire a new time sample */
#define RM_GET_PERIOD(METRICS, INDEX, PERIOD) \
	mc.PeriodSample(METRICS[INDEX].mh, PERIOD);

namespace br = bbque::res;
namespace bu = bbque::utils;
namespace bp = bbque::plugins;
namespace po = boost::program_options;

using bp::PluginManager;
using bu::MetricsCollector;
using std::chrono::milliseconds;

namespace bbque {

/* Definition of metrics used by this module */
MetricsCollector::MetricsCollection_t
ResourceManager::metrics[RM_METRICS_COUNT] = {

	//----- Event counting metrics
	RM_COUNTER_METRIC("evt.tot",	"Total events"),
	RM_COUNTER_METRIC("evt.start",	"  START events"),
	RM_COUNTER_METRIC("evt.stop",	"  STOP  events"),
	RM_COUNTER_METRIC("evt.plat",	"  PALT  events"),
	RM_COUNTER_METRIC("evt.opts",	"  OPTS  events"),
	RM_COUNTER_METRIC("evt.usr1",	"  USR1  events"),
	RM_COUNTER_METRIC("evt.usr2",	"  USR2  events"),

	RM_COUNTER_METRIC("sch.tot",	"Total Scheduler activations"),
	RM_COUNTER_METRIC("sch.failed",	"  FAILED  schedules"),
	RM_COUNTER_METRIC("sch.delayed", "  DELAYED schedules"),
	RM_COUNTER_METRIC("sch.empty",	"  EMPTY   schedules"),

	RM_COUNTER_METRIC("syn.tot",	"Total Synchronization activations"),
	RM_COUNTER_METRIC("syn.failed",	"  FAILED synchronizations"),

	//----- Sampling statistics
	RM_SAMPLE_METRIC("evt.avg.time",  "Avg events processing t[ms]"),
	RM_SAMPLE_METRIC("evt.avg.start", "  START events"),
	RM_SAMPLE_METRIC("evt.avg.stop",  "  STOP  events"),
	RM_SAMPLE_METRIC("evt.avg.plat",  "  PLAT  events"),
	RM_SAMPLE_METRIC("evt.avg.opts",  "  OPTS  events"),
	RM_SAMPLE_METRIC("evt.avg.usr1",  "  USR1  events"),
	RM_SAMPLE_METRIC("evt.avg.usr2",  "  USR2  events"),

	RM_PERIOD_METRIC("evt.per",		"Avg events period t[ms]"),
	RM_PERIOD_METRIC("evt.per.start",	"  START events"),
	RM_PERIOD_METRIC("evt.per.stop",	"  STOP  events"),
	RM_PERIOD_METRIC("evt.per.plat",	"  PLAT  events"),
	RM_PERIOD_METRIC("evt.per.opts",	"  OPTS  events"),
	RM_PERIOD_METRIC("evt.per.usr1",	"  USR1  events"),
	RM_PERIOD_METRIC("evt.per.usr2",	"  USR2  events"),

	RM_PERIOD_METRIC("sch.per",   "Avg Scheduler period t[ms]"),
	RM_PERIOD_METRIC("syn.per",   "Avg Synchronization period t[ms]"),

};

ResourceManager & ResourceManager::GetInstance()
{
	static ResourceManager rtrm;

	return rtrm;
}

ResourceManager::ResourceManager() :
    ps(PlatformServices::GetInstance()),
    am(ApplicationManager::GetInstance()),
    ap(ApplicationProxy::GetInstance()),
    um(PluginManager::GetInstance()),
    ra(ResourceAccounter::GetInstance()),
    bdm(BindingManager::GetInstance()),
    mc(MetricsCollector::GetInstance()),
#ifdef CONFIG_BBQUE_RELIABILITY
    lm(ReliabilityManager::GetInstance()),
#endif
    plm(PlatformManager::GetInstance()),
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
    prm(ProcessManager::GetInstance()),
#endif
#ifdef CONFIG_BBQUE_PM
    pm(PowerManager::GetInstance()),
#endif
#ifdef CONFIG_BBQUE_ENERGY_MONITOR
    eym(EnergyMonitor::GetInstance()),
#endif
    cm(CommandManager::GetInstance()),
    sm(SchedulerManager::GetInstance()),
    ym(SynchronizationManager::GetInstance()),
#ifdef CONFIG_BBQUE_DM
    dm(DataManager::GetInstance()),
#endif
#ifdef CONFIG_BBQUE_SCHED_PROFILING
    om(ProfileManager::GetInstance()),
#endif
#ifdef CONFIG_BBQUE_EM
    em(em::EventManager::GetInstance()),
#endif
    sys(System::GetInstance()),
    optimize_dfr("rm.opt", std::bind(&ResourceManager::Optimize, this)),
    opt_interval(BBQUE_DEFAULT_RESOURCE_MANAGER_OPT_INTERVAL),
    plat_event(false)
{
	//---------- Setup all the module metrics
	mc.Register(metrics, RM_METRICS_COUNT);

	//---------- Register commands
	CommandManager &cm = CommandManager::GetInstance();
#define CMD_SYS_STATUS ".sys_status"
	cm.RegisterCommand(MODULE_NAMESPACE CMD_SYS_STATUS, static_cast<CommandHandler*> (this),
			"Dump the status of each registered EXC");
#define CMD_OPT_FORCE ".opt_force"
	cm.RegisterCommand(MODULE_NAMESPACE CMD_OPT_FORCE, static_cast<CommandHandler*> (this),
			"Force a new scheduling event");

#ifdef CONFIG_BBQUE_EM
	em::Event event(true, "rm", "", "barbeque", "__startup", 1);
	em.InitializeArchive(event);
#endif

}

ResourceManager::ExitCode_t
ResourceManager::Setup()
{
	logger = bu::Logger::GetLogger(RESOURCE_MANAGER_NAMESPACE);
	assert(logger);

	//---------- Loading configuration
	ConfigurationManager & cm = ConfigurationManager::GetInstance();
	po::options_description opts_desc("Resource Manager Options");
	opts_desc.add_options()
		("ResourceManager.opt_interval",
		po::value<uint32_t>(&opt_interval)->default_value(
								BBQUE_DEFAULT_RESOURCE_MANAGER_OPT_INTERVAL),
		"The interval [ms] of activation of the periodic optimization");
	po::variables_map opts_vm;
	cm.ParseConfigurationFile(opts_desc, opts_vm);

	//---------- Dump list of registered plugins
	const bp::PluginManager::RegistrationMap & rm = um.GetRegistrationMap();
	logger->Info("RM: Registered plugins:");
	bp::PluginManager::RegistrationMap::const_iterator i;
	for (i = rm.begin(); i != rm.end(); ++i)
		logger->Info(" * %s", (*i).first.c_str());

	//---------- Init Platform Integration Layer (PIL)
	PlatformManager::ExitCode_t result = plm.LoadPlatformConfig();
	if (result != PlatformManager::PLATFORM_OK) {
		logger->Fatal("Platform Configuration Loader FAILED!");
		return SETUP_FAILED;
	}

	result = plm.LoadPlatformData();
	if (result != PlatformManager::PLATFORM_OK) {
		logger->Fatal("Platform Integration Layer initialization FAILED!");
		return SETUP_FAILED;
	}

	// -------- Binding Manager initialization for the scheduling policy
	if (bdm.LoadBindingDomains() != BindingManager::OK) {
		logger->Fatal("Binding Manager initialization FAILED!");
		return SETUP_FAILED;
	}

#ifdef CONFIG_BBQUE_WM
	//----------- Start the Power Monitor
	PowerMonitor & wm(PowerMonitor::GetInstance());
	wm.Start();
#endif
	//---------- Start bbque services
	plm.Start();
	if (opt_interval)
		optimize_dfr.SetPeriodic(milliseconds(opt_interval));

	return OK;
}

void ResourceManager::NotifyEvent(controlEvent_t evt)
{
	// Ensure we have a valid event
	logger->Debug("NotifyEvent: received event = %d", evt);
	assert(evt < EVENTS_COUNT);

	// Set the corresponding event flag
	std::unique_lock<std::mutex> pendingEvts_ul(pendingEvts_mtx, std::defer_lock);
	pendingEvts.set(evt);

	// Notify the control loop (just if it is sleeping)
	if (pendingEvts_ul.try_lock()) {
		logger->Debug("NotifyEvent: notifying %d", evt);
		pendingEvts_cv.notify_one();
	}
	else {
		logger->Debug("NotifyEvent: NOT notifying %d", evt);
	}
}

std::map<std::string, Worker*> ResourceManager::workers_map;
std::mutex ResourceManager::workers_mtx;
std::condition_variable ResourceManager::workers_cv;

void ResourceManager::Register(std::string const & name, Worker *pw)
{
	std::unique_lock<std::mutex> workers_ul(workers_mtx);
	fprintf(stderr, FI("Registering Worker[%s]...\n"), name.c_str());
	workers_map[name] = pw;
}

void ResourceManager::Unregister(std::string const & name)
{
	std::unique_lock<std::mutex> workers_ul(workers_mtx);
	fprintf(stderr, FI("Unregistering Worker[%s]...\n"), name.c_str());
	workers_cv.notify_one();
}

void ResourceManager::WaitForReady()
{
	std::unique_lock<std::mutex> status_ul(status_mtx);
	while (!is_ready) {
		logger->Debug("WaitForReady: an optimization is in progress...");
		status_cv.wait(status_ul);
	}
}

void ResourceManager::SetReady(bool value)
{
	logger->Debug("SetReady: %s", value ? "true" : "false");
	std::unique_lock<std::mutex> status_ul(status_mtx);
	is_ready = value;
	if (value) {
		status_cv.notify_all();
		logger->Debug("SetReady: optimization terminated");
	}
}

void ResourceManager::TerminateWorkers()
{
	std::unique_lock<std::mutex> workers_ul(workers_mtx);
	std::chrono::milliseconds timeout = std::chrono::milliseconds(30);

	auto nr_active_workers = workers_map.size();

	// Signal all registered Workers to terminate
	for_each(workers_map.begin(), workers_map.end(),
		[&, this](std::pair<std::string, Worker*> entry) {
			logger->Debug("TerminateWorkers: Worker[%s]...",
				entry.first.c_str());
			workers_ul.unlock();
			entry.second->Terminate();
			workers_ul.lock();
		});

	// Wait up to 30[ms] for workers to terminate
	while (nr_active_workers > 0) {
		for_each(workers_map.begin(), workers_map.end(),
			[&, this](std::pair<std::string, Worker*> entry) {
				workers_cv.wait_for(workers_ul, timeout);
				if (!entry.second->IsRunning())
			nr_active_workers--;
				});
		logger->Debug("TerminateWorkers: active workers left = %d",
			nr_active_workers);
	}

	workers_map.clear();
	logger->Debug("TerminateWorkers: workers map is empty? %s",
		workers_map.empty() ? "Yes" : "No");
}

void ResourceManager::Optimize()
{
	static bu::Timer optimization_tmr;
	double period;

	// Locking mechanism
	SetReady(false);

	if (this->plat_event) {
		logger->Debug("Optimize: execution triggered by a platform event");
		this->plat_event = false;
	}

#ifdef CONFIG_BBQUE_ENERGY_MONITOR
	eym.StopSamplingResourceConsumption();
#endif

	// Schedule and synchronize if there are applications/processes
	if (sys.HasSchedulablesToRun()) {
		sys.PrintStatus(true);
		logger->Info("Optimize: scheduler invocation...");

		// Account for a new schedule activation
		RM_COUNT_EVENT(metrics, RM_SCHED_TOTAL);
		RM_GET_PERIOD(metrics, RM_SCHED_PERIOD, period);

#ifdef CONFIG_BBQUE_ENERGY_MONITOR
		// Energy consumption profiles of the running applications...
		UpdateEnergyConsumptionProfiles();
#endif

		//--- Scheduling
		optimization_tmr.start();
		SchedulerManager::ExitCode_t sched_result = sm.Schedule();
		optimization_tmr.stop();

		switch (sched_result) {
		case SchedulerManager::MISSING_POLICY:
		case SchedulerManager::FAILED:
			logger->Warn("Optimize: scheduling FAILED "
				"(Error: scheduling policy failed)");
			RM_COUNT_EVENT(metrics, RM_SCHED_FAILED);
			SetReady(true);
			return;
		case SchedulerManager::DELAYED:
			logger->Error("Optimize: scheduling DELAYED");
			RM_COUNT_EVENT(metrics, RM_SCHED_DELAYED);
			SetReady(true);
			return;
		default:
			assert(sched_result == SchedulerManager::DONE);
		}

		logger->Notice("Optimize: scheduling time: %11.3f[us]",
			optimization_tmr.getElapsedTimeUs());
		sys.PrintStatus(true, sys.GetScheduledResourceStateView());
	}
	else {
		logger->Debug("Optimize: no applications or processes to schedule");
	}

#ifdef CONFIG_BBQUE_PM
	// Turn back online resources previously switched off and now resumed by
	// the policy
	ra.RestoreResourcesToPowerOn();
#endif

	// Check if there is at least one application to synchronize
	if (!sys.HasSchedulables(Schedulable::SYNC)) {
		logger->Debug("Optimize: no applications in SYNC state");
		RM_COUNT_EVENT(metrics, RM_SCHED_EMPTY);
	}
	else {
		// Account for a new synchronization activation
		RM_COUNT_EVENT(metrics, RM_SYNCH_TOTAL);
		RM_GET_PERIOD(metrics, RM_SYNCH_PERIOD, period);
		if (period)
			logger->Notice("Optimize: scheduling period: %9.3f[us]",
				period);

		//--- Synchronization
		optimization_tmr.start();
		SynchronizationManager::ExitCode_t sync_result = ym.SyncSchedule();
		optimization_tmr.stop();
		if (sync_result != SynchronizationManager::OK) {
			RM_COUNT_EVENT(metrics, RM_SYNCH_FAILED);
			// FIXME here we should implement some countermeasure to
			// ensure consistency
		}

		sys.PrintStatus(true);
		logger->Notice("Optimize: synchronization time: %11.3f[us]",
			optimization_tmr.getElapsedTimeUs());
	}

#ifdef CONFIG_BBQUE_SCHED_PROFILING
	//--- Profiling
	logger->Debug(LNPROB);
	optimization_tmr.start();
	ProfileManager::ExitCode_t profResult = om.ProfileSchedule();
	optimization_tmr.stop();
	if (profResult != ProfileManager::OK) {
		logger->Warn("Optimize: scheduler profiling FAILED");
	}
	logger->Debug(LNPROE);
	logger->Debug("Optimize: profiling time: %11.3f[us]",
		optimization_tmr.getElapsedTimeUs());
#else
	logger->Debug("Optimize: scheduling profiling disabled");
#endif

#ifdef CONFIG_BBQUE_PM
	// Enforce power management configuration
	PlatformManager::ExitCode_t platResult = plm.ActuatePowerManagement();
	if (platResult != PlatformManager::ExitCode_t::PLATFORM_OK) {
		logger->Warn("Optimize: power configuration setting failed");
	}
#endif // CONFIG_BBQUE_PM

#ifdef CONFIG_BBQUE_ENERGY_MONITOR
	// (Re-)start energy monitoring
	eym.StartSamplingResourceConsumption();
#endif

	// Ready again for processing next events
	SetReady(true);

#ifdef CONFIG_BBQUE_DM
	dm.NotifyUpdate(stat::EVT_SCHEDULING);
#endif
}


#ifdef CONFIG_BBQUE_ENERGY_MONITOR

void ResourceManager::UpdateEnergyConsumptionProfiles()
{
	// Last energy consumption values for each monitored HW resource
	auto energy_values = eym.GetValues();

	// Iterate over the running applications
	logger->Debug("UpdateEnergyConsumptionProfiles: %d running application(s)",
		sys.ApplicationsCount(app::Schedulable::RUNNING));
	AppsUidMapIt app_it;
	auto papp = sys.GetFirstRunning(app_it);
	while (papp) {
		br::ResourcePtr_t gpu_rsrc;
		uint64_t gpu_used = 0;
		uint32_t gpu_load;
		float gpu_div = 1.0;
		uint64_t cpu_total = 0;
		uint64_t acc_used = 0;
		uint64_t gpu_energy_uj = 0;
		uint64_t cpu_energy_uj = 0;
		uint64_t acc_energy_uj = 0;
		float per_app_energy_uj = 0.0;


		// For each assigned resource estimate the application contribution
		for (auto const & ev_entry : energy_values) {
			auto const & resource_path(ev_entry.first);
			switch (resource_path->ParentType()) {
			case br::ResourceType::GPU:
				gpu_rsrc = sys.GetResource(resource_path);
				if (gpu_rsrc == nullptr) {
					logger->Error("UpdateEnergyConsumptionProfiles:"
						"[%s] <%s> object missing",
						papp->StrId(),
						resource_path->ToString().c_str());
					continue;
				}

				gpu_div = 1.0 / gpu_rsrc->ApplicationsCount();
				gpu_used += sys.ResourceUsedBy(resource_path, papp);
				//if (gpu_used > 0 && (pm.GetLoad(resource_path, gpu_load) == PowerManager::PMResult::OK)) {
				if (gpu_used > 0) {
					gpu_load = gpu_rsrc->GetPowerInfo(
									PowerManager::InfoType::LOAD,
									br::Resource::MEAN);
					gpu_energy_uj += (ev_entry.second * gpu_load * gpu_div);
				}
				logger->Info("UpdateEnergyConsumptionProfiles: [%s] <%s> "
					"gpu_load=%d gpu_div=%.2f E=(+%lu)",
					papp->StrId(),
					resource_path->ToString().c_str(),
					gpu_load,
					gpu_div,
					gpu_energy_uj);
				break;
			case br::ResourceType::CPU:
				// More than one CPU could have been assigned
				cpu_total += sys.ResourceTotal(resource_path);
				cpu_energy_uj += ev_entry.second;
				logger->Debug("UpdateEnergyConsumptionProfiles: [%s] <%s> "
					"cpu_total=(+%lu) E=(+%lu)",
					papp->StrId(),
					resource_path->ToString().c_str(),
					cpu_total,
					cpu_energy_uj);
				break;
			case br::ResourceType::ACCELERATOR:
				// Typically accelerators are assigned exclusively (100%)
				acc_used = sys.ResourceUsedBy(resource_path, papp);
				acc_energy_uj += acc_used > 0 ? ev_entry.second : 0;
				logger->Debug("UpdateEnergyConsumptionProfiles: [%s] <%s>=(+%lu) E=(+%lu)",
					papp->StrId(),
					resource_path->ToString().c_str(),
					acc_used,
					acc_energy_uj);
				break;
			default:
				break;
			}
		}

		// Run-time profiling information (mainly for CPU usage)
		auto prof = papp->GetRuntimeProfile();
		logger->Debug("UpdateEnergyConsumptionProfiles: [%s] CPU=[%.2f/%lu=%.2f] CPU_e=%lu uJ",
			papp->StrId(),
			prof.cpu_usage.curr,
			cpu_total,
			prof.cpu_usage.curr / cpu_total,
			cpu_energy_uj);

		// Overall energy consumption of the application
		assert(cpu_total > 0);
		per_app_energy_uj += gpu_energy_uj
			+ (prof.cpu_usage.curr / cpu_total) * cpu_energy_uj
			+ acc_energy_uj;
		logger->Info("UpdateEnergyConsumptionProfiles: [%s] E=%.0f uJ",
			papp->StrId(),
			per_app_energy_uj);

		// Put the value in the application profiling data structure
		papp->UpdateEstimatedEnergyConsumption(per_app_energy_uj);
		logger->Debug("UpdateEnergyConsumptionProfiles: [%s] energy consumption updated",
			papp->StrId());

		papp = sys.GetNextRunning(app_it);
	}

}

#endif // Energy monitor

void ResourceManager::EvtExcStart()
{
	logger->Info("EvtExcStart");
	RM_RESET_TIMING(rm_tmr);

	// This is a simple optimization triggering policy based on the
	// current priority level of READY applications.
	// When an application issue a Working Mode request it is expected to
	// be in ready state. The optimization will be triggered in a
	// time-frame which is _inverse proportional_ to the highest priority
	// ready application.
	// This should allows to have short latencies for high priority apps
	// while still allowing for reduced rescheduling on applications
	// startup burst.
	// TODO: make this policy more tunable via the configuration file
	AppPtr_t papp = am.HighestPrio(Schedulable::READY);
	if (!papp) {
		// In this case the application has exited before the start
		// event has had the change to be processed
		DB(logger->Warn("Overdue processing of a START event"));
		return;
	}

	unsigned int timeout = BBQUE_RM_OPT_EXC_START_DEFER_MS;
	optimize_dfr.Schedule(milliseconds(timeout));

	RM_GET_TIMING(metrics, RM_EVT_TIME_START, rm_tmr);
}

void ResourceManager::EvtExcStop()
{
	logger->Info("EvtExcStop");
	RM_RESET_TIMING(rm_tmr);

	// This is a simple optimization triggering policy
	unsigned int timeout = BBQUE_RM_OPT_EXC_STOP_DEFER_MS;
	optimize_dfr.Schedule(milliseconds(timeout));

	RM_GET_TIMING(metrics, RM_EVT_TIME_STOP, rm_tmr);
}

void ResourceManager::EvtBbqPlat()
{
	logger->Info("EvtBbqPlat");
	RM_RESET_TIMING(rm_tmr);
	plat_event = true;

	// Platform generated events triggers an immediate rescheduling.
	// TODO add a better policy which triggers immediate rescheduling only
	// on resources reduction. Perhaps such a policy could be plugged into
	// the PlatformProxy module.
	optimize_dfr.Schedule();

	// Collecing execution metrics
	RM_GET_TIMING(metrics, RM_EVT_TIME_PLAT, rm_tmr);
}

void ResourceManager::EvtBbqOpts()
{
	logger->Info("EvtBbqOpts");
	RM_RESET_TIMING(rm_tmr);

	// Explicit applications requests for optimization are delayed by
	// default just to increase the chance for aggregation of multiple
	// requests
	unsigned int timeout = BBQUE_RM_OPT_REQUEST_DEFER_MS;
	optimize_dfr.Schedule(milliseconds(timeout));

	RM_GET_TIMING(metrics, RM_EVT_TIME_OPTS, rm_tmr);
}

void ResourceManager::EvtBbqUsr1()
{
	logger->Info("EvtBbqUsr1");
	RM_RESET_TIMING(rm_tmr);

	sys.PrintStatus(true);
	pendingEvts.reset(BBQ_USR1);

	RM_GET_TIMING(metrics, RM_EVT_TIME_USR1, rm_tmr);
}

void ResourceManager::EvtBbqUsr2()
{
	logger->Info("EvtBbqUsr2");
	RM_RESET_TIMING(rm_tmr);

	logger->Debug("Dumping metrics collection...");
	mc.DumpMetrics();

	pendingEvts.reset(BBQ_USR2);

	RM_GET_TIMING(metrics, RM_EVT_TIME_USR2, rm_tmr);
}

void ResourceManager::EvtBbqExit()
{
#ifdef CONFIG_BBQUE_ENERGY_MONITOR
	EnergyMonitor & eym(EnergyMonitor::GetInstance());
	eym.StopSamplingResourceConsumption();
#endif

	logger->Notice("EvtBbqExit: terminating BarbequeRTRM...");
	done = true;
	pendingEvts_cv.notify_one();

	// Dumping collected stats before termination
	EvtBbqUsr1();
	EvtBbqUsr2();

	// Stop applications
	AppsUidMapIt apps_it;
	AppPtr_t papp;
	papp = am.GetFirst(apps_it);
	for ( ; papp; papp = am.GetNext(apps_it)) {
		logger->Notice("EvtBbqExit: terminating application: %s",
			papp->StrId());
		ap.StopExecution(papp);
		am.DisableEXC(papp, true);
		am.DestroyEXC(papp);
	}

	// Notify all workers
	logger->Notice("EvtBbqExit: stopping all the workers...");
	ResourceManager::TerminateWorkers();

	// Terminate platforms
	logger->Notice("EvtBbqExit: terminating the platform supports...");
	plm.Exit();
}

int ResourceManager::CommandsCb(int argc, char *argv[])
{
	UNUSED(argc);
	uint8_t cmd_offset = ::strlen(MODULE_NAMESPACE) + 1;
	logger->Debug("Processing command [%s]", argv[0] + cmd_offset);

	bool cmd_not_found = false;

	switch (argv[0][cmd_offset]) {

	case 's':
		if (strcmp(argv[0], MODULE_NAMESPACE CMD_SYS_STATUS)) {
			cmd_not_found = true;
			break;
		}

		logger->Notice("");
		logger->Notice("===========[ System Status ]=========="
			"======================================");
		logger->Notice("");
		sys.PrintStatus(true);
		break;

	case 'o':
		if (strcmp(argv[0], MODULE_NAMESPACE CMD_OPT_FORCE)) {
			cmd_not_found = true;
			break;
		}

		logger->Notice("");
		logger->Notice("========[ User Required Scheduling ]==="
			"=======================================");
		logger->Notice("");
		NotifyEvent(ResourceManager::BBQ_OPTS);
		break;

	default:
		cmd_not_found = true;
	}

	if (cmd_not_found) {
		logger->Error("Command [%s] not supported by this module", argv[0]);
		return -1;
	}

	return 0;
}

void ResourceManager::ControlLoop()
{
	std::unique_lock<std::mutex> pendingEvts_ul(pendingEvts_mtx);
	double period;

	// Wait for a new event
	while (!pendingEvts.any()) {
		logger->Debug("Control Loop: no events");
		pendingEvts_cv.wait(pendingEvts_ul);
	}

	if (done == true) {
		logger->Warn("Control Loop: returning");
		return;
	}

	// Checking for pending events, starting from higer priority ones.
	for (uint8_t evt = EVENTS_COUNT; evt; --evt) {

		logger->Debug("Control Loop:: checking events [%d:%s]",
			evt - 1, pendingEvts[evt - 1] ? "Pending" : "None");

		// Jump not pending events
		if (!pendingEvts[evt - 1])
			continue;

		// Resetting event
		pendingEvts.reset(evt - 1);

		// Account for a new event
		RM_COUNT_EVENT(metrics, RM_EVT_TOTAL);
		RM_GET_PERIOD(metrics, RM_EVT_PERIOD, period);

		// Dispatching events to handlers
		switch (evt - 1) {
		case EXC_START:
			logger->Debug("Event [EXC_START]");
			EvtExcStart();
			RM_COUNT_EVENT(metrics, RM_EVT_START);
			RM_GET_PERIOD(metrics, RM_EVT_PERIOD_START, period);
			break;
		case EXC_STOP:
			logger->Debug("Event [EXC_STOP]");
			EvtExcStop();
			RM_COUNT_EVENT(metrics, RM_EVT_STOP);
			RM_GET_PERIOD(metrics, RM_EVT_PERIOD_STOP, period);
			break;
		case BBQ_PLAT:
			// Platform reconfiguration or warning conditions
			// requiring a policy execution
			logger->Debug("Event [BBQ_PLAT]");
			EvtBbqPlat();
			RM_COUNT_EVENT(metrics, RM_EVT_PLAT);
			RM_GET_PERIOD(metrics, RM_EVT_PERIOD_PLAT, period);
			break;
		case BBQ_OPTS:
			// Application-driven policy execution request
			logger->Debug("Event [BBQ_OPTS]");
			EvtBbqOpts();
			RM_COUNT_EVENT(metrics, RM_EVT_OPTS);
			RM_GET_PERIOD(metrics, RM_EVT_PERIOD_OPTS, period);
			break;
		case BBQ_USR1:
			logger->Debug("Event [BBQ_USR1]");
			RM_COUNT_EVENT(metrics, RM_EVT_USR1);
			RM_GET_PERIOD(metrics, RM_EVT_PERIOD_USR1, period);
			EvtBbqUsr1();
			return;
		case BBQ_USR2:
			logger->Debug("Event [BBQ_USR2]");
			RM_COUNT_EVENT(metrics, RM_EVT_USR2);
			RM_GET_PERIOD(metrics, RM_EVT_PERIOD_USR2, period);
			EvtBbqUsr2();
			return;
		case BBQ_EXIT:
			logger->Debug("Event [BBQ_EXIT]");
			EvtBbqExit();
			return;
		case BBQ_ABORT:
			logger->Debug("Event [BBQ_ABORT]");
			logger->Fatal("Abortive quit");
			exit(EXIT_FAILURE);
		default:
			logger->Crit("Unhandled event [%d]", evt - 1);
		}
	}
}

ResourceManager::ExitCode_t
ResourceManager::Go()
{
	ExitCode_t result;

	result = Setup();
	if (result != OK)
		return result;

	while (!done) {
		ControlLoop();
	}

	return OK;
}

} // namespace bbque
