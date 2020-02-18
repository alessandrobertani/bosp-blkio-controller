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

#ifndef BBQUE_PROCESS_MANAGER_H_
#define BBQUE_PROCESS_MANAGER_H_

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "bbque/app/process.h"
#include "bbque/command_manager.h"
#include "bbque/resource_accounter.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/map_iterator.h"

namespace bbque
{

using ProcPtr_t    = std::shared_ptr<app::Process>;
using ProcessMap_t = std::map<app::AppPid_t, ProcPtr_t>;
using ProcessMapIterator = utils::MapIterator<ProcPtr_t>;
using ProcessMapIteratorRetainer_t = utils::MapIteratorRetainer_t<ProcPtr_t>;

using namespace app;


class ProcessManager: public CommandHandler
{

public:

	/**
	 * @enum ExitCode_t
	 * @brief Get the ProcessManager instance
	 */
	enum ExitCode_t {
	        SUCCESS = 0,
	        PROCESS_NOT_SCHEDULED,
	        PROCESS_NOT_SCHEDULABLE,
	        PROCESS_NOT_FOUND,
	        PROCESS_MISSING_AWM,
	        PROCESS_WRONG_STATE,
	        PROCESS_SCHED_REQ_REJECTED,
	        ABORT
	};

	/**
	 * @brief Get the ProcessManager instance
	 */
	static ProcessManager & GetInstance();

	/*******************************************************************************
	 *     Process manipulation
	 ******************************************************************************/

	/**
	 * @brief Add a process to the managed map
	 * @param name the binary name
	 */
	void Add(std::string const & name);

	/**
	 * @brief Remove a process from the managed map
	 * @param name the binary name
	 */
	void Remove(std::string const & name);

	/**
	 * @brief Check if a program/process is in the management list
	 * @param name the binary name
	 */
	bool IsToManage(std::string const & name) const;

	/**
	 * @brief Notify the start of a process/program
	 * @param name the binary name
	 * @param pid the process id
	 */
	void NotifyStart(std::string const & name, app::AppPid_t pid);

	/**
	 * @brief Notify the termination of a process/program
	 * @param name the binary name
	 * @param pid the process id
	 */
	void NotifyExit(std::string const & name, app::AppPid_t pid);

	void NotifyExit(app::AppPid_t pid);

	/**
	 * @brief Update process status to FROZEN. Usually called by platform
	 * proxies once the actual freezing action has been performed.
	 * @param pid the process id
	 * @return ChangeState() return value
	 */
	ExitCode_t SetAsFrozen(app::AppPid_t pid);

	/**
	 * @brief Set the THAWED status, in order to trigger the actual thawing
	 * of the process during the synchronization stage. However it is
	 * mandatory that the scheduling policy would pick THAWED processes also.
	 *
	 * @param pid the process id
	 * @return ChangeState() return value
	 */
	ExitCode_t SetToThaw(app::AppPid_t pid);

	/*******************************************************************************
	 *     Map iterations
	 ******************************************************************************/


	/**
	 * @brief Check if there are managed processes
	 * @return true if yes, false for not
	 */
	bool HasProcesses() const;

	/**
	 * @brief Check if there are processes in a given state
	 * @param state a stable state
	 * @return true if yes, false for not
	 */
	bool HasProcesses(app::Schedulable::State_t state) const;

	/**
	 * @brief Check if there are processes in a given synchronization state
	 * @param sync_state a synchronization state
	 * @return true if yes, false for not
	 */
	bool HasProcesses(app::Schedulable::SyncState_t sync_state) const;


	/**
	 * @brief Get a process descriptor
	 * @param pid the ID number
	 * @return A pointer to a process descriptor
	 */
	ProcPtr_t const GetProcess(app::AppPid_t pid) const;

	/**
	 * @brief Check if a process is alive
	 * @param pid the process id
	 * @param release terminate if dead
	 * @return true if alive, false otherwise
	 */
	bool CheckProcess(app::AppPid_t pid, bool release);

	/**
	 * @brief Get the first process in the map
	 * @return A pointer to a process descriptor
	 */
	ProcPtr_t GetFirst(app::Schedulable::State_t state, ProcessMapIterator & it);

	/**
	 * @brief Get the next process in the map
	 * @return A pointer to a process descriptor
	 */
	ProcPtr_t GetNext(app::Schedulable::State_t state, ProcessMapIterator & it);

	/**
	 * @brief The number of processes in a given state queue
	 * @param state the process state
	 * @return An integer value
	 */
	uint32_t ProcessesCount(app::Schedulable::State_t state);

	/*******************************************************************************
	 *     Scheduling functions
	 ******************************************************************************/

	/**
	 * @brief Request to re-schedule this application into a new configuration
	 *
	 * The Optimizer call this method when an AWM is selected for this
	 * application to verify if it could be scheduled, i.e. bound resources
	 * are available, and eventually to update the application status.
	 *
	 * First the application verify resources availability. If the quality and
	 * amount of required resources could be satisfied, the application is
	 * going to be re-scheduled otherwise, it is un-scheduled.
	 *
	 * @param proc The application/EXC to schedule
	 * @param awm Next working mode scheduled for the application
	 * @param status_view The token referencing the resources state view
	 * @param bid An optional identifier for the resource binding
	 *
	 * @return The method returns an exit code representing the decision taken:
	 * AM_SUCCESS if the specified working mode can be scheduled for
	 * this application, APP_AWM_NOT_SCHEDULABLE if the working mode cannot
	 * not be scheduled. If the application is currently disabled this call
	 * returns always AM_APP_DISABLED.
	 */
	ExitCode_t ScheduleRequest(
	        ProcPtr_t proc, app::AwmPtr_t  awm,
	        br::RViewToken_t status_view, size_t b_refn);

	/**
	 * @brief Re-schedule this application according to previous scheduling
	 * policy run
	 *
	 * @param proc The application to re-schedule
	 * @param status_view The token referencing the resources state view
	 *
	 * @return The method returns AM_SUCCESS if the application can be
	 * rescheduled, AM_EXC_INVALID_STATUS if the application is not in
	 * "running" stats, APP_AWM_NOT_SCHEDULABLE if required resources are
	 * no longer
	 * available.
	 */
	ExitCode_t ScheduleRequestAsPrev(
	        ProcPtr_t proc, br::RViewToken_t status_view) {
		return ScheduleRequest(proc, proc->CurrentAWM(), status_view, 0);
	}

	/**
	 * @brief Configure this application to switch to the specified AWM
	 * @param proc the application
	 * @param awm the working mode
	 * @return @see ExitCode_t
	 */
	ExitCode_t Reschedule(ProcPtr_t proc, app::AwmPtr_t awm);

	/**
	 * @brief Configure this application to release resources.
	 * @param proc the application
	 * @return @see ExitCode_t
	 */
	ExitCode_t Unschedule(ProcPtr_t proc);

	/**
	 * @brief Do not schedule the application
	 * @param proc the application
	 */
	ExitCode_t NoSchedule(ProcPtr_t proc) {
		return ChangeState(proc,
		                   Schedulable::SYNC, Schedulable::BLOCKED);
	}

	/*******************************************************************************
	 *     Synchronization functions
	 ******************************************************************************/

	/**
	 * @brief Commit the synchronization for the specified application
	 *
	 * @param proc the application which has been synchronized
	 *
	 * @return SUCCESS on succesful commit, PROCESS_NOT_FOUND or
	 * PROCESS_NOT_SCHEDULED on errors.
	 */
	ExitCode_t SyncCommit(ProcPtr_t proc);

	/**
	 * @brief Abort the synchronization for the specified application
	 *
	 * @param proc the application which has been synchronized
	 */
	ExitCode_t SyncAbort(ProcPtr_t proc);

	/**
	 * @brief Commit the "continue to run" for the specified application
	 *
	 * @param proc a pointer to the interested application
	 * @return SUCCESS on succesful commit, PROCESS_NOT_FOUND or
	 * PROCESS_NOT_SCHEDULED on errors.
	 */
	ExitCode_t SyncContinue(ProcPtr_t proc);

	/**
	 * @brief Dump a logline to report all processes status
	 *
	 * @param verbose print in INFO logleve is ture, in DEBUG if false
	 */
	void PrintStatus(bool verbose);


private:

	using PidSet_t    = std::set<app::AppPid_t>;
	using PidSetPtr_t = std::shared_ptr<PidSet_t>;

	/**
	 * @brief Runtime information about the instances of managed processes
	 */
	class ProcessInstancesInfo
	{
	public:
		ProcessInstancesInfo() {
			sched_req = std::make_shared<app::Process::ScheduleRequest>();
			pid_set   = std::make_shared<PidSet_t>();
		}
		// Scheduling request
		app::Process::ScheduleRequestPtr_t sched_req;
		// Set of PIDs of active process instances
		PidSetPtr_t pid_set;
	};

	/** The logger used by the application manager */
	std::unique_ptr<bu::Logger> logger;

	/** Command manager instance */
	CommandManager & cm;


	/** Mutex to protect processes maps */
	mutable std::mutex proc_mutex;

	/** Map with pointers to all the managed processes */
	std::map<app::AppPid_t, ProcPtr_t> all_procs;

	/** The set containing the names of the managed processes */
	std::map<std::string, ProcessInstancesInfo> managed_procs;

	/** State vectors of the managed processes */
	std::vector<ProcessMap_t> state_procs;

	/** Retainers for the thread-safe manipulation of map iterators */
	ProcessMapIteratorRetainer_t state_retain[Schedulable::STATE_COUNT];

	/**
	 * @brief Constructor
	 */
	ProcessManager();

	/**
	 * @brief Change a process state
	 */
	ExitCode_t ChangeState(
	        ProcPtr_t proc,
	        Schedulable::State_t to_state,
	        Schedulable::SyncState_t next_state = Schedulable::SYNC_NONE);

	/**
	 * @brief The handler for commands defined by this module
	 */
	int CommandsCb(int argc, char *argv[]);

	/**
	 * @brief The handler for the command used to set a scheduling request
	 */
	void CommandManageSetSchedule(int argc, char * argv[]);

	/**
	 * @brief Help for the schedule request command
	 */
	void CommandManageSetScheduleHelp() const;

	/**
	 * @brief Update the iterators when an element must be removed
	 */
	void UpdateIterators(ProcessMapIteratorRetainer_t & ret, ProcPtr_t proc);

};


} // namespace bbque

#endif // BBQUE_PROCESS_MANAGER_H_
