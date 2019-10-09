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

#ifndef BBQUE_SCHEDULABLE_H_
#define BBQUE_SCHEDULABLE_H_

#include <cassert>
#include <memory>

#include "bbque/config.h"
#include "bbque/cpp11/mutex.h"
#include "bbque/utils/extra_data_container.h"

#define SCHEDULABLE_ID_MAX_LEN  16


namespace bbque
{
namespace app
{

typedef uint32_t AppPid_t;  /** Application identifier type */
typedef uint16_t AppPrio_t; /** Application priority type */

// Forward declarations
class WorkingMode;
class Schedulable;
using AwmPtr_t   = std::shared_ptr<WorkingMode>;
using SchedPtr_t = std::shared_ptr<Schedulable>;


class Schedulable: public utils::ExtraDataContainer
{

public:
	/**
	 * @enum ExitCode_t
	 * @brief Error codes returned by methods
	 */
	enum ExitCode_t {
	        APP_SUCCESS = 0,	/** Success */
	        APP_DISABLED,   	/** Application being DISABLED */
	        APP_FINISHED,   	/** Application being FINISHED */
	        APP_STATUS_NOT_EXP,   	/** Application in unexpected status */
	        APP_SYNC_NOT_EXP,  	/** Application in unexpected synchronization status */
	        APP_RECP_NULL,  	/** Null recipe object passed */
	        APP_WM_NOT_FOUND,	/** Application working mode not found */
	        APP_RSRC_NOT_FOUND,	/** Resource not found */
	        APP_CONS_NOT_FOUND,	/** Constraint not found */
	        APP_WM_REJECTED,	/** The working mode is not schedulable */
	        APP_WM_ENAB_CHANGED,	/** Enabled working modes list has changed */
	        APP_WM_ENAB_UNCHANGED,	/** Enabled working modes list has not changed */
	        APP_TG_SEM_ERROR,	/** Error while accessing task-graph semaphore */
	        APP_TG_FILE_ERROR,	/** Error while accessing task-graph serial file */
	        APP_ABORT         	/** Unexpected error */
	};

	/**
	 * @enum State_t
	 * @brief A possible application state.
	 *
	 * This is the set of possible state an application could be.
	 */
	typedef enum State {
	        NEW = 0, 	/** Registered within Barbeque but currently disabled */
	        READY,      	/** Registered within Barbeque and waiting to start */
	        SYNC,     	/** (Re-)scheduled but not reconfigured yet */
	        RUNNING,  	/** Running */
	        FROZEN,         /** Frozen */
	        THAWED,         /** Thawed after freezing - need new resource assignment */
	        FINISHED, 	/** Regular termination */

	        STATE_COUNT	/** This must alwasy be the last entry */
	} State_t;

	/**
	 * @enum SyncState_t
	 * @brief Required synchronization action
	 *
	 * Once a reconfiguration for an schedulable entity has been requested, one of these
	 * synchronization state is entered. Vice versa, if no reconfiguration
	 * are required, the SYNC_NONE (alias SYNC_STATE_COUNT) is assigned to
	 * the schedulable entity.
	 */
	typedef enum SyncState {
	        // NOTE These values should be reported to match (in number and order)
	        //      those defined by the RTLIB::RTLIB_ExitCode.
	        STARTING = 0, 	/** The application is entering the system */
	        RECONF ,      	/** Must change working mode */
	        MIGREC,       	/** Must migrate and change working mode */
	        MIGRATE,    	/** Must migrate into another cluster */
	        BLOCKED,    	/** Must be blocked because of resource are not more available */
	        DISABLED, 	/** Terminated */

	        SYNC_STATE_COUNT /** This must alwasy be the last entry */
	} SyncState_t;

	/**
	 * @enum Type
	 * @brief The type of schedulable object
	 */
	enum class Type
	{
	        ADAPTIVE, /// Adaptive Execution Model integrated
	        PROCESS   /// Not integrated generic process
	};


#define SYNC_NONE SYNC_STATE_COUNT

	/**
	 * @struct SchedulingInfo_t
	 *
	 * Application scheduling informations.
	 * The scheduling of an application is characterized by a pair of
	 * information: the state (@see State_t), and the working mode
	 * choosed by the scheduler/optimizer module.
	 */
	struct SchedulingInfo_t {
		mutable std::recursive_mutex mtx; /** The mutex to serialize access to scheduling info */
		State_t state;            /** The current scheduled state */
		State_t preSyncState;     /** The state before a sync has been required */
		SyncState_t syncState;    /** The current synchronization state */
		AwmPtr_t awm;             /** The current application working mode */
		AwmPtr_t next_awm;        /** The next scheduled application working mode */
		uint64_t count = 0;       /** How many times the application has been scheduled */
		bool remote = false;      /** Set true if scheduled on a remote node */

		/**
		 * The metrics value set by the scheduling policy. The purpose of this
		 * attribute is to provide a support for the evaluation of the schedule
		 * results.
		 */
		float value;

		/** Overloading of operator != for structure comparisons */
		bool operator!=(SchedulingInfo_t const &other) const {
			return ((this->state != other.state) ||
			        (this->preSyncState != other.preSyncState) ||
			        (this->syncState != other.syncState) ||
			        (this->awm != other.awm));
		};
	};

#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
	struct CGroupSetupData_t {
		unsigned long cpu_ids;
		unsigned long cpus_ids_isolation;
		unsigned long mem_ids;
	};

	void SetCGroupSetupData(
	        unsigned long cpu_ids, unsigned long mem_ids,
	        unsigned long cpu_ids_isolation) {
		cgroup_data.cpu_ids = cpu_ids;
		cgroup_data.cpus_ids_isolation = cpu_ids_isolation;
		cgroup_data.mem_ids = mem_ids;
	}

	CGroupSetupData_t GetCGroupSetupData() {
		return cgroup_data;
	}
#endif

	/**
	 * @brief Get the name of the application
	 * @return The name string
	 */
	virtual std::string const & Name() const noexcept {
		return name;
	}

	/**
	 * @brief Get the process ID of the application
	 * @return PID value
	 */
	virtual AppPid_t Pid() const noexcept {
		return pid;
	}

	/**
	 * @brief Get the unique ID of the application
	 *
	 * If the application is a process, it will return the same value of
	 * Pid()
	 * @return PID value
	 */
	virtual AppPid_t Uid() const {
		return pid;
	}

	/**
	 * @brief Get a string ID for this Execution Context
	 * This method build a string ID according to this format:
	 * PID:TASK_NAME:EXC_ID
	 * @return String ID
	 */
	virtual const char *StrId() const  {
		return str_id;
	}

	/**
	 * @brief Get the priority associated to
	 * @return The priority value
	 */
	virtual	AppPrio_t Priority() const noexcept {
		return priority;
	}

	/**
	 * @brief The type of schedulable object
	 * @return ADAPTIVE or PROCESS
	 */
	virtual Type GetType() const noexcept {
		return type;
	}


	/**
	 * @brief Get the schedule state
	 * @return The current scheduled state
	 */
	virtual State_t State() const;

	/**
	 * @brief Get the pre-synchronization state
	 */
	virtual State_t PreSyncState() const;

	/**
	 * @brief Get the synchronization state
	 */
	virtual SyncState_t SyncState() const;

	/**
	 * @brief Verbose application state names
	 */
	static char const *stateStr[STATE_COUNT];

	/**
	 * @brief Verbose synchronization state names
	 */
	static char const *syncStateStr[SYNC_STATE_COUNT + 1];

	/**
	 * @brief String of the given state
	 */
	static char const *StateStr(State_t state) {
		assert(state < STATE_COUNT);
		return stateStr[state];
	}

	/**
	 * @brief String of the given synchronization state
	 */
	static char const *SyncStateStr(SyncState_t state) {
		assert(state < SYNC_STATE_COUNT + 1);
		if (state > SYNC_STATE_COUNT)
			return syncStateStr[SYNC_STATE_COUNT];
		return syncStateStr[state];
	}

	/**
	 * @brief Check if this EXC is currently DISABLED
	 */
	virtual bool Disabled() const;

	/**
	 * @brief Check if this EXC is currently FINISHED
	 */
	virtual bool Finished() const;

	/**
	 * @brief Check if this EXC is currently READY of RUNNING
	 */
	virtual bool Active() const;

	/**
	 * @brief Check if this EXC is currently RUNNING
	 */
	virtual bool Running() const;

	/**
	 * @brief Check if this EXC is currently in SYNC state
	 */
	virtual bool Synching() const;

	/**
	 * @brief Check if this EXC is currently STARTING
	 */
	virtual bool Starting() const;

	/**
	 * @brief Check if this EXC is being BLOCKED
	 */
	virtual bool Blocking() const;


	/**
	 * @brief Get the current working mode
	 * @return A shared pointer to the current application working mode
	 */
	virtual AwmPtr_t const & CurrentAWM() const;

	/**
	 * @brief Get next working mode to switch in when the application is
	 * re-scheduld
	 * @return A shared pointer to working mode descriptor (optimizer
	 * interface)
	 */
	virtual AwmPtr_t const & NextAWM() const;

	/**
	 * @brief Check if the current AWM is going to be changed
	 * @return true if the application is going to be reconfigured with
	 * 	also a change of the current AWM
	 */
	virtual bool SwitchingAWM() const noexcept;

	/**
	 * @brief Number of schedulations
	 */
	virtual uint64_t ScheduleCount() const noexcept;

	/**
	 * @brief Check if this is a reshuffling
	 *
	 * Resources reshuffling happens when two resources bindings are not
	 * the same, i.e. different kind or amount of resources, while the
	 * application is not being reconfigured or migrated.
	 *
	 * This method check if the specified AWM will produce a reshuffling.
	 *
	 * @param next_awm the AWM to compare with current
	 * @return true if the specified next_awm will produce a resources
	 * reshuffling
	 */
	virtual bool Reshuffling(AwmPtr_t const & next_awm) const;


	/** @brief Set a remote application
	 *
	 * Mark the application as remote or local
	 */
	void SetRemote(bool is_remote) noexcept { schedule.remote = is_remote; }

	/**
	 * @brief Return true if the application is executing or will be
	 *        executed remotely, false if not.
	 */
	bool IsRemote() const noexcept {
		return schedule.remote;
	}

	/**
	 * @brief Set a remote application
	 *
	 * Mark the application as remote or local
	 */
	void SetLocal(bool is_local) noexcept { schedule.remote = !is_local; }

	/**
	 * @brief Return true if the application is executing or will be
	 *        executed locally, false if not.
	 */
	bool IsLocal() const noexcept {
		return !schedule.remote;
	}


// Scheduling


// Synchronization


protected:

	/** The application name */
	std::string name;

	/** The PID assigned from the OS */
	AppPid_t pid;

	/** A numeric priority value */
	AppPrio_t priority;

	/** Type of application (AEM-integrated or process) */
	Type type;

	/** Current scheduling informations */
	SchedulingInfo_t schedule;


#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
	CGroupSetupData_t cgroup_data;
#endif

	/** A string id with information for logging */
	char str_id[SCHEDULABLE_ID_MAX_LEN];

	/**
	 * @brief Update the application state and synchronization state
	 *
	 * @param state the new application state
	 * @param sync the new synchronization state (SYNC_NONE by default)
	 */
	virtual ExitCode_t SetState(State_t state, SyncState_t sync = SYNC_NONE);

	/**
	 * @brief Update the application synchronization state
	 * @param sync the synchronization state to set
	 */
	virtual void SetSyncState(SyncState_t sync);

	/**
	 * @brief Verify if a synchronization is required to move into the
	 * specified AWM.
	 *
	 * The method is called only if the Application is currently RUNNING.
	 * Compare the WorkingMode specified with the currently used by the
	 * Application. Compare the Resources set with the one binding the
	 * resources of the current WorkingMode, in order to check if the
	 * Application is going to run using processing elements from the same
	 * clusters used in the previous execution step.
	 *
	 * @param awm the target working mode
	 *
	 * @return One of the following values:
	 * - RECONF: Application is being scheduled for using PEs from the same
	 *   clusters, but with a different WorkingMode.
	 * - MIGRATE: Application is going to run in the same WorkingMode, but
	 *   it’s going to be moved onto a different clusters set.
	 * - MIGREC: Application changes completely its execution profile. Both
	 *   WorkingMode and clusters set are different from the previous run.
	 * - SYNC_NONE: Nothing changes. Application is going to run in the same
	 *   operating point.
	 *
	 */
	virtual SyncState_t NextSyncState(AwmPtr_t const & awm) const;

	/**
	 * @brief Set the working mode assigned by the scheduling policy
	 *
	 * The usage of this method should be in charge of the
	 * ApplicationManager, when a scheduling request has been accepted
	 *
	 * @param awm the working mode
	 */
	virtual void SetNextAWM(AwmPtr_t awm);


	virtual bool _Disabled() const;

	virtual bool _Finished() const;

	virtual bool _Active() const;

	virtual  bool _Running() const;

	virtual bool _Synching() const;

	virtual bool _Starting() const;

	virtual bool _Blocking() const;

	virtual State_t _State() const;

	virtual State_t _PreSyncState() const;

	virtual SyncState_t _SyncState() const;

	virtual AwmPtr_t const & _CurrentAWM() const;

	virtual AwmPtr_t const & _NextAWM() const;

	virtual bool _SwitchingAWM() const;

};


} // namespace app

} // namespace bbque

#endif // BBQUE_SCHEDULABLE_H_
