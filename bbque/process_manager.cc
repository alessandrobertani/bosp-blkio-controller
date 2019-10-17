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

#include <cstring>
#include <ctype.h>

#include "bbque/app/working_mode.h"
#include "bbque/process_manager.h"
#include "bbque/resource_manager.h"
#include "bbque/utils/schedlog.h"

#define MODULE_NAMESPACE "bq.prm"
#define MODULE_CONFIG    "ProcessManager"

#define PRM_MAX_ARG_LENGTH 15
#define PRM_TABLE_TITLE "|                    Processes status                                     |"

using namespace bbque::app;


namespace bbque
{


ProcessManager & ProcessManager::GetInstance()
{
	static ProcessManager instance;
	return instance;
}

ProcessManager::ProcessManager():
	cm(CommandManager::GetInstance())
{
	// Get a logger
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);

	// Register commands
#define CMD_ADD_PROCESS ".add"
	cm.RegisterCommand(
	        MODULE_NAMESPACE CMD_ADD_PROCESS,
	        static_cast<CommandHandler*>(this),
	        "Add a process to manage (by executable name)");
#define CMD_REMOVE_PROCESS ".remove"
	cm.RegisterCommand(
	        MODULE_NAMESPACE CMD_REMOVE_PROCESS,
	        static_cast<CommandHandler*>(this),
	        "Remove a managed process (by executable name)");
#define CMD_SETSCHED_PROCESS ".setsched"
	cm.RegisterCommand(
	        MODULE_NAMESPACE CMD_SETSCHED_PROCESS,
	        static_cast<CommandHandler*>(this),
	        "Set a resource allocation request for a process/program");

	// Status vector size equal to numbe of possible process states
	state_procs.resize(app::Schedulable::STATE_COUNT);
}


int ProcessManager::CommandsCb(int argc, char *argv[])
{
	std::string command_name(argv[0]);
	std::string arg1;
	logger->Debug("CommandsCb: processing command <%s>", command_name.c_str());

	// bq.prm.add <process_name>
	if (command_name.compare(MODULE_NAMESPACE CMD_ADD_PROCESS) == 0) {
		if (argc < 1) {
			logger->Error("CommandsCb: <%s> : missing argument", command_name.c_str());
			return -1;
		}
		arg1.assign(argv[1]);
		logger->Info("CommandsCb: adding <%s> to managed processes", argv[1]);
		Add(arg1);
		return 0;
	}

	// bq.prm.remove <process_name>
	if (command_name.compare(MODULE_NAMESPACE CMD_REMOVE_PROCESS) == 0) {
		if (argc < 1) {
			logger->Error("CommandsCb: <%s> : missing argument", command_name.c_str());
			return -1;
		}

		arg1.assign(argv[1]);
		logger->Info("CommandsCb: removing <%s> from managed processes", argv[1]);
		Remove(arg1);
		return 0;
	}

	// bq.prm.setsched <process_name> ...
	if (command_name.compare(MODULE_NAMESPACE CMD_SETSCHED_PROCESS) == 0) {
		CommandManageSetSchedule(argc, argv);
		return 0;
	}

	logger->Error("CommandsCb: <%s> not supported by this module", command_name.c_str());
	return -1;
}


void ProcessManager::CommandManageSetSchedule(int argc, char * argv[])
{
	app::Process::ScheduleRequest sched_req;
	app::AppPid_t pid = 0;
	std::string name;
	char c;
	while ((c = getopt(argc, argv, "n:p:c:a:m:h")) != -1) {
		switch (c) {
		case 'n':
			if (optarg != nullptr) {
				name.assign(optarg);
				Add(name);
			}
			break;
		case 'p':
			if (optarg != nullptr)
				pid = atoi(optarg);
			break;
		case 'c':
			if (optarg != nullptr)
				sched_req.cpu_cores = atoi(optarg);
			break;
		case 'a':
			if (optarg != nullptr)
				sched_req.acc_cores = atoi(optarg);
			break;
		case 'm':
			if (optarg != nullptr)
				sched_req.memory_mb = atoi(optarg);
			break;
		case 'h':
		default :
			CommandManageSetScheduleHelp();
		}
	}

	if (name.empty()) {
		logger->Error("CommandsCb: wrong arguments specification");
		CommandManageSetScheduleHelp();
		return;
	}

	std::unique_lock<std::mutex> u_lock(proc_mutex);
	*(managed_procs[name].sched_req) = sched_req;
	logger->Notice("CommandsCb: <%s> (pid=%d) schedule request: cpus=%d accs=%d mem=%d",
	               name.c_str(),
	               pid,
	               managed_procs[name].sched_req->cpu_cores,
	               managed_procs[name].sched_req->acc_cores,
	               managed_procs[name].sched_req->memory_mb);
}


void ProcessManager::CommandManageSetScheduleHelp() const
{
	logger->Notice("%s -n=<process_name> [-p=<pid>] -c=<cpu_cores> "
	               "[-a=<accelerator_cores>] [-m=<memory_MB>]",
	               MODULE_NAMESPACE CMD_SETSCHED_PROCESS);
}


void ProcessManager::Add(std::string const & name)
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	auto it = managed_procs.find(name);
	if (it == managed_procs.end()) {
		managed_procs.emplace(name, ProcessManager::ProcessInstancesInfo());
		logger->Debug("Add: processes with name '%s' in the managed map", name.c_str());
	} else
		logger->Debug("Add: processes with name '%s' already n the managed map", name.c_str());
}


void ProcessManager::Remove(std::string const & name)
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	managed_procs.erase(name);
	logger->Debug("Remove: processes with name '%s' no longer in the managed map",
	              name.c_str());
}


bool ProcessManager::IsToManage(std::string const & name) const
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	if (managed_procs.find(name) == managed_procs.end())
		return false;
	return true;
}


void ProcessManager::NotifyStart(std::string const & name, app::AppPid_t pid)
{
	if (!IsToManage(name)) {
//		logger->Debug("NotifyStart: %s not managed", name.c_str());
		return;
	}
	logger->Info("NotifyStart: scheduling required for [%s: %d]",
	             name.c_str(), pid);
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	managed_procs[name].pid_set->emplace(pid);

	// Create a new process descriptor and enqueue it
	ProcPtr_t new_proc = std::make_shared<Process>(name, pid);
	new_proc->SetState(Schedulable::READY);
	state_procs[app::Schedulable::READY].emplace(pid, new_proc);
	all_procs.emplace(pid, new_proc);

	// Trigger a re-scheduling
	ResourceManager & rm(ResourceManager::GetInstance());
	rm.NotifyEvent(ResourceManager::BBQ_OPTS);
}


void ProcessManager::NotifyExit(std::string const & name, app::AppPid_t pid)
{
	if (!IsToManage(name)) {
		//logger->Debug("NotifyExit: %s not managed", name.c_str());
		return;
	}
	logger->Debug("NotifyExit: [%s] is a managed program", name.c_str());
	NotifyExit(pid);
}


void ProcessManager::NotifyExit(app::AppPid_t pid)
{
	logger->Debug("NotifyExit: process PID=<%d> check...", pid);
	std::unique_lock<std::mutex> u_lock(proc_mutex);

	// Retrive from the status maps...
	ProcPtr_t ending_proc = nullptr;
	for (auto state_it = state_procs.begin(); state_it != state_procs.end(); ++state_it) {
		auto & state_map(*state_it);
		auto proc_it = state_map.find(pid);
		if (proc_it != state_map.end()) {
			ending_proc = proc_it->second;
			logger->Debug("NotifyExit: process PID=<%d> found", pid);
			break;
		}
	}
	u_lock.unlock();

	if (ending_proc == nullptr) {
		logger->Warn("NotifyExit: process PID=<%d> not found", pid);
		return;
	}

	// Warning: exit can be notified also for frozen processes
	if (ending_proc->State() == Schedulable::FROZEN) {
		logger->Warn("NotifyExit: process PID=<%d> is frozen."
		             " Ignoring exit notification...",
		             pid);
		return;
	}

	// Status change
	auto ret = ChangeState(ending_proc,
	                       app::Schedulable::SYNC, app::Schedulable::DISABLED);
	if (ret != SUCCESS) {
		logger->Crit("NotifyExit: [%s] FAILED: state=%s sync=%s",
		             ending_proc->StrId(),
		             ending_proc->StateStr(ending_proc->State()),
		             ending_proc->SyncStateStr(ending_proc->SyncState()));
		return;
	}
	// Trigger a re-scheduling
	ResourceManager & rm(ResourceManager::GetInstance());
	rm.NotifyEvent(ResourceManager::BBQ_OPTS);
}

ProcessManager::ExitCode_t ProcessManager::SetAsFrozen(app::AppUid_t pid)
{
	logger->Debug("SetAsFrozen: process PID=<%d> update status to FROZEN", pid);
	ProcPtr_t proc = GetProcess(pid);
	if (!proc) {
		logger->Warn("SetAsFrozen: process PID=<%d> not found", pid);
		return ExitCode_t::PROCESS_NOT_FOUND;
	}
	return ChangeState(proc, Schedulable::FROZEN);
}

ProcessManager::ExitCode_t ProcessManager::SetToThaw(app::AppUid_t pid)
{
	logger->Debug("SetToThaw: process PID=<%d> to thaw...", pid);
	ProcPtr_t proc = GetProcess(pid);
	if (!proc) {
		logger->Warn("SetToThaw: process PID=<%d> not found", pid);
		return ExitCode_t::PROCESS_NOT_FOUND;
	}

	if (proc->State() != Schedulable::FROZEN) {
		logger->Warn("SetToThaw: process PID=<%d> not FROZEN", pid);
		return ExitCode_t::PROCESS_WRONG_STATE;
	}

	Schedulable::State_t next_state = Schedulable::THAWED;
	auto ret = ChangeState(proc, next_state);
	if (ret == ExitCode_t::SUCCESS) {
		logger->Debug("SetToThaw: process PID=<%d> status updated: %s",
		              pid, proc->StateStr(next_state));
	} else {
		logger->Error("SetToThaw: process PID=<%d> status update failed",
		              pid);
	}

	return ret;
}


bool ProcessManager::HasProcesses() const
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	return !all_procs.empty();
}

bool ProcessManager::HasProcesses(app::Schedulable::State_t state) const
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	return !state_procs[state].empty();
}

bool ProcessManager::HasProcesses(app::Schedulable::SyncState_t sync_state) const
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	if (state_procs[app::Schedulable::SYNC].empty()) {
		return false;
	}

	for (auto & it_sync : state_procs[app::Schedulable::SYNC]) {
		auto & proc(it_sync.second);
		logger->Debug("HasProcesses: [%s] state=%s sync=%s",
		              proc->StrId(),
		              proc->StateStr(proc->State()),
		              proc->SyncStateStr(proc->SyncState()));
		if (proc->SyncState() == sync_state)
			return true;
	}

	return false;
}


ProcPtr_t const ProcessManager::GetProcess(AppPid_t pid) const
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	auto it = all_procs.find(pid);
	if (it != all_procs.end()) {
		return it->second;
	} else {
		logger->Debug("GetProcess: not process found with PID=%d", pid);
	}
	return ProcPtr_t();
}


ProcPtr_t ProcessManager::GetFirst(app::Schedulable::State_t state, ProcessMapIterator & map_it)
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	map_it.Init(state_procs[state], state_retain[state]);
	if (map_it.End())
		return ProcPtr_t();

	ProcPtr_t proc = map_it.Get();
	map_it.Retain(); // Add iterator to the retainers list
	logger->Debug("GetFirst: > ADD retained processes iterator [@%p => %d]",
	              &(map_it.it), proc->Pid());
	return proc;
}

ProcPtr_t ProcessManager::GetNext(app::Schedulable::State_t state, ProcessMapIterator & map_it)
{
	UNUSED(state);
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	map_it++;
	if (map_it.End()) {
		map_it.Release();  // Release the iterator retainer
		logger->Debug("GetNext: < DEL retained processes iterator [@%p => %d]",
		              &(map_it.it));
		return ProcPtr_t();
	}
	ProcPtr_t proc = map_it.Get();
	return proc;
}


uint32_t ProcessManager::ProcessesCount(app::Schedulable::State_t state)
{
	if (state >= app::Schedulable::STATE_COUNT)
		return -1;
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	return state_procs[state].size();
}




/*******************************************************************************
 *     Scheduling functions
 ******************************************************************************/

ProcessManager::ExitCode_t ProcessManager::ScheduleRequest(
        ProcPtr_t proc,
        app::AwmPtr_t  awm,
        res::RViewToken_t status_view,
        size_t b_refn)
{
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	logger->Info("ScheduleRequest: [%s] schedule request for binding @[%d] view=%ld",
	             proc->StrId(), b_refn, status_view);

	// AWM safety check
	if (!awm) {
		logger->Crit("ScheduleRequest: [%s] AWM not existing)", proc->StrId());
		assert(awm);
		return PROCESS_MISSING_AWM;
	}
	logger->Debug("ScheduleRequest: [%s] request for scheduling in AWM [%02d:%s]",
	              proc->StrId(), awm->Id(), awm->Name().c_str());

	// App is SYNC/BLOCKED for a previously failed scheduling.
	// Reset state and syncState for this new attempt.
	/*
		if (proc->Blocking()) {
			logger->Warn("ScheduleRequest: [%s] request for blocking application",
				proc->StrId());
			logger->Warn("ScheduleRequest: [%s] forcing state transition to SYNC_NONE",
				proc->StrId());
			ChangeState(proc, proc->State(), app::Schedulable::FINISHED);
		}
	*/

	// Checking for resources availability: unschedule if not
	auto ra_result = ra.BookResources(
	                         proc,
	                         awm->GetSchedResourceBinding(b_refn),
	                         status_view);
	if (ra_result != ResourceAccounter::RA_SUCCESS) {
		logger->Debug("ScheduleRequest: [%s] not enough resources...",
		              proc->StrId());
		Unschedule(proc);
		return PROCESS_NOT_SCHEDULABLE;
	}

	// Bind the resource set to the working mode
	auto wm_result = awm->SetResourceBinding(status_view, b_refn);
	if (wm_result != app::WorkingMode::WM_SUCCESS) {
		logger->Error("ScheduleRequest: [%s] something went wrong in binding map",
		              proc->StrId());
		return PROCESS_SCHED_REQ_REJECTED;
	}

	logger->Debug("ScheduleRequest: [%s] state=%s sync=%s",
	              proc->StrId(),
	              proc->StateStr(proc->State()),
	              proc->SyncStateStr(proc->SyncState()));

	// Reschedule accordingly to "awm"
	logger->Debug("ScheduleRequest: (re)scheduling [%s] into AWM [%d:%s]...",
	              proc->StrId(), awm->Id(), awm->Name().c_str());
	auto ret = Reschedule(proc, awm);
	if (ret != SUCCESS) {
		ra.ReleaseResources(proc, status_view);
		awm->ClearResourceBinding();
		return ret;
	}

	logger->Debug("ScheduleRequest: [%s] <%s / %s> completed",
	              proc->StrId(),
	              proc->StateStr(proc->State()),
	              proc->SyncStateStr(proc->SyncState()));
	return SUCCESS;
}

ProcessManager::ExitCode_t ProcessManager::Reschedule(
        ProcPtr_t proc,
        app::AwmPtr_t awm)
{
	// Next synchronization state (not exploited)
	auto next_sync = proc->NextSyncState(awm);
	logger->Debug("(Re)schedule: [%s] for %s",
	              proc->StrId(),
	              proc->SyncStateStr(next_sync));
	if (next_sync == app::Schedulable::SYNC_NONE) {
		logger->Warn("(Re)schedule: [%s] next_sync=SYNC_NONE (state=%s)",
		             proc->StrId(),
		             proc->StateStr(proc->State()));
		return SUCCESS;
	}
	logger->Debug("(Re)schedule: [%s, %s] next synchronization...",
	              proc->StrId(), app::Schedulable::SyncStateStr(next_sync));

	// Change to synchronization state
	auto ret = ChangeState(proc, app::Schedulable::SYNC, next_sync);
	if (ret != SUCCESS) {
		logger->Crit("(Re)schedule: [%s] FAILED: state=%s sync=%s",
		             proc->StrId(),
		             proc->StateStr(proc->State()),
		             proc->SyncStateStr(proc->SyncState()));
		return PROCESS_SCHED_REQ_REJECTED;
	}

	// Set the next working mode
	proc->SetNextAWM(awm);
	if (!proc->NextAWM()) {
		logger->Crit("(Re)schedule:[%s] next AWM not set!", proc->StrId());
		return PROCESS_SCHED_REQ_REJECTED;
	}

	logger->Debug("(Re)schedule: [%s] next_awm=<%d>",
	              proc->StrId(), proc->NextAWM()->Id());
	return SUCCESS;
}


ProcessManager::ExitCode_t ProcessManager::Unschedule(ProcPtr_t proc)
{
	// Next synchronization state (not exploited)
	logger->Debug("Unschedule: [%s, %s]...",
	              proc->StrId(),
	              proc->StateStr(proc->State()),
	              proc->SyncStateStr(proc->SyncState()));

	// Change to synchronization state
	auto ret = ChangeState(proc,
	                       app::Schedulable::SYNC, app::Schedulable::BLOCKED);
	if (ret != SUCCESS) {
		logger->Crit("Unschedule: [%s] FAILED: state=%s sync=%s",
		             proc->StrId(),
		             proc->StateStr(proc->State()),
		             proc->SyncStateStr(proc->SyncState()));
		return PROCESS_SCHED_REQ_REJECTED;
	}

	return SUCCESS;
}

/*******************************************************************************
 *     Synchronization functions
 ******************************************************************************/

ProcessManager::ExitCode_t ProcessManager::SyncCommit(ProcPtr_t proc)
{
	ProcessManager::ExitCode_t ret = SUCCESS;
	// SYNC -> RUNNING
	if (proc->Synching() && !proc->Blocking() && !proc->Disabled()) {
		logger->Debug("SyncCommit: [%s] changing to RUNNING...",
		              proc->StrId());
		ret = ChangeState(proc,
		                  Schedulable::RUNNING,
		                  Schedulable::SYNC_NONE);
	}
	// SYNC (BLOCKED) -> READY
	else if (proc->Blocking()) {
		ret = ChangeState(proc,
		                  app::Schedulable::READY,
		                  app::Schedulable::SYNC_NONE);
		if (ret != SUCCESS) {
			logger->Crit("Unschedule: [%s] FAILED: state=%s sync=%s",
			             proc->StrId(),
			             proc->StateStr(proc->State()),
			             proc->SyncStateStr(proc->SyncState()));
			return PROCESS_SCHED_REQ_REJECTED;
		}
	}
	// DISABLED -> <remove>
	else if (proc->Disabled()) {
		logger->Debug("SyncCommit: [%s] releasing DISABLED...",
		              proc->StrId());
		for (auto state_it = state_procs.begin();
		     state_it != state_procs.end(); ++state_it) {
			auto & state_map(*state_it);
			auto proc_it = state_map.find(proc->Pid());
			if (proc_it != state_map.end()) {
				logger->Debug("SyncCommit: [%s: %d] "
				              "removing from map...",
				              proc->Name().c_str(),
				              proc->Pid());
				UpdateIterators(
				        state_retain[Schedulable::FINISHED],
				        proc);
				state_map.erase(proc_it);
			}
		}
		// remove from the global map
		all_procs.erase(proc->Pid());
	}

	if (ret != SUCCESS) {
		logger->Error("SyncCommit: [%s] failed (state=%s)",
		              proc->StrId(), proc->StateStr(proc->State()));
	}
	return ret;
}

ProcessManager::ExitCode_t ProcessManager::SyncAbort(ProcPtr_t proc)
{
	logger->Debug("SyncAbort: [%s] changing status...", proc->StrId());

	ExitCode_t ret;
	if (kill(proc->Pid(), 0) == 0) {
		// if alive... give new scheduling attempts
		logger->Debug("SyncAbort: [%s] still alive...", proc->StrId());
		ret = ChangeState(proc, Schedulable::READY);
	} else {
		// if dead... set disabled for resources release
		logger->Debug("SyncAbort: [%s] is dead...", proc->StrId());
		ret = ChangeState(proc, Schedulable::SYNC, Schedulable::DISABLED);
	}

	if (ret != SUCCESS) {
		logger->Error("SyncAbort: [%s] failed (state=%s)",
		              proc->StrId(), proc->StateStr(proc->State()));
	}
	return ret;
}

ProcessManager::ExitCode_t ProcessManager::SyncContinue(ProcPtr_t proc)
{
	logger->Debug("SyncContinue: [%s] continuing with RUNNING...", proc->StrId());
	if (proc->State() != Schedulable::RUNNING) {
		logger->Error("SyncContinue: [%s] wrong status (state=%s)",
		              proc->StrId(), proc->StateStr(proc->State()));
		return PROCESS_NOT_SCHEDULABLE;
	}
	auto ret = ChangeState(proc, Schedulable::RUNNING);
	if (ret != SUCCESS) {
		logger->Error("SyncContinue: [%s] failed (state=%s)",
		              proc->StrId(), proc->StateStr(proc->State()));
	}
	return ret;
}

ProcessManager::ExitCode_t ProcessManager::ChangeState(
        ProcPtr_t proc,
        Schedulable::State_t to_state,
        Schedulable::SyncState_t next_sync)
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);

	Schedulable::State_t from_state = proc->State();
	auto & from_map(state_procs[from_state]);
	auto proc_it = from_map.find(proc->Pid());
	if (proc_it == from_map.end()) {
		logger->Warn("ChangeState: process PID=%d not found in state=%s)",
		             proc->Pid(), proc->StateStr(proc->State()));
		return PROCESS_NOT_FOUND;
	}

	if (proc->State() == to_state) {
		logger->Debug("ChangeState: process PID=%d already in state=%s",
		              proc->Pid(), proc->StateStr(proc->State()));
		return SUCCESS;
	} else {
		auto & to_map(state_procs[to_state]);
		to_map.emplace(proc->Pid(), proc);
		UpdateIterators(state_retain[from_state], proc);
		from_map.erase(proc_it);
	}

	logger->Debug("ChangeState: FROM [%s] state=%s sync=%s",
	              proc->StrId(),
	              proc->StateStr(proc->State()),
	              proc->SyncStateStr(proc->SyncState()));

	proc->SetState(to_state, next_sync);
	logger->Debug("ChangeState: TO [%s] state=%s sync=%s",
	              proc->StrId(),
	              proc->StateStr(proc->State()),
	              proc->SyncStateStr(proc->SyncState()));

	return SUCCESS;
}

void ProcessManager::UpdateIterators(
        ProcessMapIteratorRetainer_t & ret,
        ProcPtr_t proc)
{
	ProcessMapIteratorRetainer_t::iterator it;
	ProcessMapIterator * m_it;
	logger->Debug("Checking [%d] iterators...", ret.size());
	// Lookup for iterators on the specified map which pointes to the
	// specified process
	for (it = ret.begin(); it != ret.end(); ++it) {
		m_it = (*it);
		// Ignore iterators not pointing to the application of interest
		if (m_it->it->first != proc->Pid())
			continue;

		// Update the iterator position one step backward
		logger->Debug("~ Updating iterator [@%p => %d]",
		              m_it->it, proc->Uid());
		m_it->Update(); // Move the iterator forward
	}
}


void ProcessManager::PrintStatus(bool verbose)
{
	std::unique_lock<std::mutex> u_lock(proc_mutex);
	char line[80];
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV1);
	PRINT_NOTICE_IF_VERBOSE(verbose, PRM_TABLE_TITLE);
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV2);
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_HEAD);
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV2);

	for (auto & proc_it : all_procs) {
		ProcPtr_t & proc(proc_it.second);
		utils::SchedLog::BuildSchedStateLine(proc, line, 80);
		PRINT_NOTICE_IF_VERBOSE(verbose, line);
	}

	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV1);
}



} // namespace bbque
