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

#ifndef BBQUE_APPLICATION_MANAGER_H_
#define BBQUE_APPLICATION_MANAGER_H_

#include <map>
#include <mutex>
#include <vector>

#include "bbque/app/application_conf.h"
#include "bbque/application_manager_conf.h"
#include "bbque/config.h"
#include "bbque/command_manager.h"
#include "bbque/plugins/recipe_loader.h"
#include "bbque/utils/deferrable.h"
#include "bbque/utils/logging/logger.h"

using bbque::app::Application;
using bbque::utils::Deferrable;
using bbque::plugins::RecipeLoaderIF;
using bbque::CommandHandler;

namespace bu = bbque::utils;

namespace bbque {

namespace app {
class Recipe;
typedef std::shared_ptr<Recipe> RecipePtr_t;
}

using bbque::app::RecipePtr_t;

// Forward declaration
class PlatformManager;

/**
 * @class ApplicationManager
 * @brief The class provides interfaces for managing the applications lifecycle.
 * @ingroup sec03_am
 *
 * This provides the interface for managing applications registration and keep
 * track of their schedule status changes. The class provides calls to
 * register applications, retrieving application descriptors, the maps of
 * application descriptors given their scheduling status or priority level.
 * Moreover to signal the scheduling change of status of an application, and
 * to know which is lowest priority level (maximum integer value) managed by
 * Barbeque RTRM.
 */
class ApplicationManager : public ApplicationManagerConfIF, public CommandHandler
{
public:

	/**
	 * @brief Get the ApplicationManager instance
	 */
	static ApplicationManager & GetInstance();

	/**
	 * @brief The destructor
	 */
	virtual ~ApplicationManager();


#ifdef CONFIG_BBQUE_RELIABILITY

	void SaveEXCReliabilityInfo(
				AppPtr_t papp,
				std::string const & recipe_name);
#endif

	/**
	 * @see ApplicationManagerConfIF
	 */
	AppPtr_t CreateEXC(
			std::string const & name, AppPid_t pid, uint8_t exc_id,
			std::string const & recipe,
			RTLIB_ProgrammingLanguage_t lang = RTLIB_LANG_CPP,
			AppPrio_t prio = BBQUE_APP_PRIO_LEVELS - 1,
			bool weak_load = false,
			bool container = false);

	/**
	 * @see ApplicationManagerConfIF
	 */
	AppPtr_t RestoreEXC(
			std::string const & name,
			AppPid_t restore_pid,
			uint8_t exc_id,
			std::string const & recipe,
			RTLIB_ProgrammingLanguage_t lang = RTLIB_LANG_CPP);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t DestroyEXC(AppPid_t pid);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t DestroyEXC(AppPtr_t papp);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t DestroyEXC(AppPid_t pid, uint8_t exc_id);


	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t SetConstraintsEXC(AppPtr_t papp,
				RTLIB_Constraint_t *constraints, uint8_t count);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t SetConstraintsEXC(AppPid_t pid, uint8_t exc_id,
				RTLIB_Constraint_t *constraints, uint8_t count);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t ClearConstraintsEXC(AppPtr_t papp);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t ClearConstraintsEXC(AppPid_t pid, uint8_t exc_id);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t EnableEXC(AppPtr_t papp);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t EnableEXC(AppPid_t pid, uint8_t exc_id);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t DisableEXC(AppPtr_t papp, bool release = false);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t DisableEXC(AppPid_t pid, uint8_t exc_id,
			bool release = false);

	/**
	 * @see ApplicationManagerConfIF
	 */
	bool CheckEXC(AppPtr_t papp, bool release = true);

	/**
	 * @see ApplicationManagerConfIF
	 */
	bool CheckEXC(AppPid_t pid, uint8_t exc_id, bool release = true);

	/**
	 * @see ApplicationManagerConfIF
	 */
	void CheckActiveEXCs();


	/*******************************************************************************
	 *     Thread-Safe Queue Access Functions
	 ******************************************************************************/

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetFirst(AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetNext(AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetFirst(AppPrio_t prio, AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetNext(AppPrio_t prio, AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetFirst(
			Schedulable::State_t state,
			AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetNext(
			Schedulable::State_t state,
			AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetFirst(
			Schedulable::SyncState_t state,
			AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t GetNext(
			Schedulable::SyncState_t state,
			AppsUidMapIt & it);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	bool HasApplications(AppPrio_t prio) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	bool HasApplications(Schedulable::State_t state) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	bool HasApplications(Schedulable::SyncState_t state) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	bool HasApplications(RTLIB_ProgrammingLanguage_t lang) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t AppsCount() const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t AppsCount(AppPrio_t prio) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t AppsCount(Schedulable::State_t state) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t AppsCount(Schedulable::SyncState_t state) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t AppsCount(RTLIB_ProgrammingLanguage_t lang) const;

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t HighestPrio(Schedulable::State_t state);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t HighestPrio(Schedulable::SyncState_t syncState);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t const GetApplication(AppPid_t pid, uint8_t exc_id);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	AppPtr_t const GetApplication(AppUid_t uid);

	/**
	 * @see ApplicationManagerStatusIF
	 */
	inline app::AppPrio_t LowestPriority() const
	{
		return BBQUE_APP_PRIO_LEVELS - 1;
	};

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
	 * @param papp The application/EXC to schedule
	 * @param awm Next working mode scheduled for the application
	 * @param status_view The token referencing the resources state view
	 * @param b_refn An optional identifier for the resource binding
	 *
	 * @return The method returns an exit code representing the decision taken:
	 * AM_SUCCESS if the specified working mode can be scheduled for
	 * this application, APP_AWM_NOT_SCHEDULABLE if the working mode cannot
	 * not be scheduled. If the application is currently disabled this call
	 * returns always AM_APP_DISABLED.
	 */
	ExitCode_t ScheduleRequest(
				app::AppCPtr_t papp, app::AwmPtr_t awm,
				br::RViewToken_t status_view, size_t b_refn);

	/**
	 * @brief Re-schedule this application according to previous scheduling
	 * policy run
	 *
	 * @param papp The application to re-schedule
	 * @param status_view The token referencing the resources state view
	 *
	 * @return The method returns AM_SUCCESS if the application can be
	 * rescheduled, AM_EXC_INVALID_STATUS if the application is not in "running"
	 * stats, APP_AWM_NOT_SCHEDULABLE if required resources are no longer available.
	 */
	ExitCode_t ScheduleRequestAsPrev(
					app::AppCPtr_t papp, br::RViewToken_t status_view);

	/**
	 * @brief Abort a schedule request
	 *
	 * @param papp The application to unschedule
	 * @param status_view The token referencing the resources state view
	 */
	ExitCode_t ScheduleRequestAbort(
					app::AppCPtr_t papp,
					br::RViewToken_t status_view);

	/**
	 * @brief Configure this application to switch to the specified AWM
	 * @param papp the application
	 * @param awm the working mode
	 * @return @see ExitCode_t
	 */
	ExitCode_t Reschedule(app::AppCPtr_t papp, app::AwmPtr_t awm);

	/**
	 * @brief Configure this application to release resources.
	 * @param papp the application
	 * @return @see ExitCode_t
	 */
	ExitCode_t Unschedule(app::AppCPtr_t papp);

	/**
	 * @brief Do not schedule the application
	 * @param papp the application
	 */
	ExitCode_t NoSchedule(app::AppCPtr_t papp);

	/**
	 * @brief Flag the application as "to synchronize"
	 *
	 * @param papp the application to synchronize
	 * @param next_state the synchronization state
	 *
	 * @return AM_SUCCESS if the synchronization request has been accepted,
	 * AM_ABORT on synchronization request errors
	 */
	ExitCode_t SetForSynchronization(
					app::AppCPtr_t papp, Schedulable::SyncState_t next_sync);

	/*******************************************************************************
	 *     Synchronization functions
	 ******************************************************************************/

	/**
	 * @brief Commit the synchronization for the specified application
	 *
	 * @param papp the application which has been synchronized
	 *
	 * @return AM_SUCCESS on commit succesfull, AM_ABORT on errors.
	 */
	ExitCode_t SyncCommit(AppPtr_t papp);

	/**
	 * @brief Abort the synchronization for the specified application
	 *
	 * @param papp the application which has been synchronized
	 */
	void SyncAbort(AppPtr_t papp);

	/**
	 * @brief Commit the "continue to run" for the specified application
	 *
	 * @param papp a pointer to the interested application
	 * @return AM_SUCCESS on success, AM_ABORT on failure
	 */
	ExitCode_t SyncContinue(AppPtr_t papp);

	/**
	 * @brief Update status of a frozen application, once an actual
	 * freezing has been performed by the platform proxies.
	 *
	 * @param uid application unique identifier
	 * @return AM_SUCCESS on success, AM_ABORT on failure
	 */
	ExitCode_t SetAsFrozen(AppUid_t uid);

	/**
	 * @brief Set the THAWED status, in order to trigger the actual thawing
	 * of the application during the synchronization stage. However it is
	 * mandatory that the scheduling policy would pick THAWED application
	 * also.
	 *
	 * @param uid application unique identifier
	 * @return AM_SUCCESS on success, AM_ABORT on failure
	 */
	ExitCode_t SetToThaw(AppUid_t uid);


	/*******************************************************************************
	 *     Run-time Profiling
	 ******************************************************************************/

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t CheckGoalGapEXC(AppPtr_t papp, struct app::RuntimeProfiling_t &profile);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t IsReschedulingRequired(AppPid_t pid, uint8_t exc_id,
					struct app::RuntimeProfiling_t &profile);

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t IsReschedulingRequired(AppPtr_t papp,
					struct app::RuntimeProfiling_t &profile);

	/**
	 * @brief Update runtime profiling information of each active
	 * application/EXC
	 *
	 * This is set set of information that can be used by the optimization
	 * policy
	 */
	int UpdateRuntimeProfiles();

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t GetRuntimeProfile(
				AppPid_t pid, uint8_t exc_id, struct app::RuntimeProfiling_t &profile);

	/**
	 * @see ApplicationManagerConfIF
	 */
	inline ExitCode_t GetRuntimeProfile(AppPtr_t papp,
					struct app::RuntimeProfiling_t &profile)
	{
		profile = papp->GetRuntimeProfile();
		return AM_SUCCESS;
	}

	/**
	 * @see ApplicationManagerConfIF
	 */
	ExitCode_t SetRuntimeProfile(AppPid_t pid,
				uint8_t exc_id, struct app::RuntimeProfiling_t profile);

	ExitCode_t SetRuntimeProfile(AppPid_t pid, uint8_t exc_id,
				int gap, int cusage, int ctime);

	/**
	 * @see ApplicationManagerConfIF
	 */
	inline ExitCode_t SetRuntimeProfile(AppPtr_t papp,
					struct app::RuntimeProfiling_t profile)
	{
		papp->SetRuntimeProfile(profile);
		return AM_SUCCESS;
	}

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

	/******************************************************************************
	 *     Task-graph functions
	 ******************************************************************************/

	/**
	 * @brief Load the task-graph of an application
	 * @param pid the processi ID of the application
	 * @param exc_id the Execution Context ID to enable
	 */
	void LoadTaskGraph(AppPid_t pid, uint8_t exc_id);

	/**
	 * @brief Load the task-graph of an application
	 * @param The application descriptor
	 */
	void LoadTaskGraph(AppPtr_t papp);

	/**
	 * @brief Load the task-graphs of all the active applications (ready and running)
	 */
	void LoadTaskGraphAll();

	/**
	 * @brief Total number of tasks from all the managed applications
	 */
	uint32_t TasksCount() const
	{
		std::lock_guard<std::mutex> lck(tg_mutex);
		return this->tasks_count;
	}

#endif // CONFIG_BBQUE_TG_PROG_MODEL

	/*******************************************************************************
	 *     Status logging
	 ******************************************************************************/

	/**
	 * @brief Dump a logline to report on current Status queue counts
	 */
	void PrintStatusQ() const;

	/**
	 * @brief Dump a logline to report on current Status queue counts
	 */
	void PrintSyncQ() const;

	/**
	 * @brief Dump a logline to report all applications status
	 *
	 * @param verbose print in INFO logleve is ture, in DEBUG if false
	 */
	void PrintStatus(bool verbose = false);

private:

	/** The logger used by the application manager */
	std::unique_ptr<bu::Logger> logger;

	/** The recipe loader module used to parse recipes */
	RecipeLoaderIF * rloader;

	/**  Command manager instance */
	CommandManager & cm;

	/** The PlatformManager, used to setup/release platform specific data */
	PlatformManager & plm;

	/** Lowest application priority value (maximum integer) */
	app::AppPrio_t lowest_prio;

	/**
	 * MultiMap of all the applications instances which entered the
	 * resource manager starting from its boot. The map key is the PID of the
	 * application instance. The value is the application descriptor of the
	 * instance.
	 */
	AppsMap_t apps;

	/**
	 * Mutex to protect concurrent access to the map of applications.
	 */
	mutable std::mutex apps_mtx;


	/**
	 * Map of all the applications instances which entered the
	 * resource manager starting from its boot. The map key is the UID of the
	 * application instance. The value is the application descriptor of the
	 * instance.
	 */
	AppsUidMap_t uids;

	/**
	 * Mutex to protect concurrent access to the map of applications UIDs.
	 */
	mutable std::recursive_mutex uids_mtx;

	/**
	 * Array of iterator retainers for "in loop erase" support on UID map
	 */
	AppsUidMapItRetainer_t uids_ret;


	/**
	 * Store all the application recipes. More than one application
	 * instance at once could run. Here we have tow cases :
	 * 	1) Each instance use a different recipes
	 * 	2) A single instance is shared between more than one instance
	 * We assume the possibility of manage both cases.
	 */
	std::map<std::string, RecipePtr_t> recipes;

	/**
	 * A mutex for serializing recipe loading. This prevents unexpected
	 * behaviors if more applications/EXC are loading the same recipe in
	 * parallel.
	 */
	mutable std::mutex recipes_mtx;

	/**
	 * Priority vector of currently scheduled applications (actives).
	 * Vector index expresses the application priority, where "0" labels
	 * "critical" applications. Indices greater than 0 are used for best effort
	 * ones. Each position in the vector points to a set of maps grouping active
	 * applications by priority.
	 */
	AppsUidMap_t prio_vec[BBQUE_APP_PRIO_LEVELS];

	/**
	 * Array of mutexes protecting the priority queues
	 */
	mutable std::mutex prio_mtx[BBQUE_APP_PRIO_LEVELS];

	/**
	 * Array of iterator retainers for "in loop erase" support on priority
	 * queues
	 */
	AppsUidMapItRetainer_t prio_ret[BBQUE_APP_PRIO_LEVELS];


	/**
	 * Array grouping the applications by status (@see ScheduleFlag).
	 * Each position points to a set of maps pointing applications
	 */
	AppsUidMap_t status_vec[Schedulable::STATE_COUNT];

	/**
	 * Array of mutexes protecting the status queues.
	 */
	mutable std::mutex status_mtx[Schedulable::STATE_COUNT];

	/**
	 * Array of iterator retainers for "in loop erase" support on STATUS
	 * queues
	 */
	AppsUidMapItRetainer_t status_ret[Schedulable::STATE_COUNT];

	/**
	 * Array grouping the applications by programming language (@see
	 * RTLIB_ProgrammingLanguage_t). Each position points to a set of maps
	 * pointing applications
	 */
	AppsUidMap_t lang_vec[RTLIB_LANG_COUNT];

	/**
	 * Array of mutexes protecting the programming language queue
	 */
	mutable std::mutex lang_mtx[RTLIB_LANG_COUNT];

	/**
	 * Array of iterator retainers for "in loop erase" support on STATUS
	 * queues
	 */
	AppsUidMapItRetainer_t lang_ret[RTLIB_LANG_COUNT];

	/**
	 * @brief Applications grouping based on next state to be scheduled.
	 *
	 * Array grouping the applications by the value of their next_sched.state
	 * (@see ScheduleFlag). Each entry is a vector of applications on the
	 * corresponding scheduled status. This view on applications could be
	 * exploited by the synchronization module to update applications.
	 */
	AppsUidMap_t sync_vec[Schedulable::SYNC_STATE_COUNT];

	/**
	 * Array of mutexes protecting the synchronization queues.
	 */
	mutable std::mutex sync_mtx[Schedulable::SYNC_STATE_COUNT];

	/**
	 * Array of iterator retainers for "in loop erase" support on SYNC
	 * queues
	 */
	AppsUidMapItRetainer_t sync_ret[Schedulable::SYNC_STATE_COUNT];

	/**
	 * @brief EXC cleaner deferrable
	 *
	 * This is used to collect and aggregate EXC cleanup requests.
	 * The cleanup will be performed by a call of the Cleanup
	 * method.
	 */
	Deferrable cleanup_dfr;


#ifdef CONFIG_BBQUE_TG_PROG_MODEL

	/**
	 * Mutex for protecting task-graph related data
	 */
	mutable std::mutex tg_mutex;

	/**
	 * Total count of the tasks of the managed applications
	 */
	uint32_t tasks_count = 0;
#endif

	/** The constructor */
	ApplicationManager();

	/** Return a pointer to a loaded recipe */
	RecipeLoaderIF::ExitCode_t LoadRecipe(std::string const & _recipe_name,
					RecipePtr_t & _recipe, bool weak_load = false);

	/**
	 * Remove the specified application from the priority maps
	 */
	ExitCode_t PriorityRemove(AppPtr_t papp);

	/**
	 * Remove the specified application from the language map
	 */
	ExitCode_t LangRemove(AppPtr_t papp);

	/**
	 * Remove the specified application from the status maps
	 */
	ExitCode_t StatusRemove(AppPtr_t papp);

	/**
	 * Remove the specified application from the apps maps
	 */
	ExitCode_t AppsRemove(AppPtr_t papp);

	/**
	 * @brief In-Loop-Erase-Safe update of iterators on applications
	 * containers
	 */
	void UpdateIterators(AppsUidMapItRetainer_t & ret, AppPtr_t papp);


	/**
	 * @brief Change the status of an application/EXC
	 * @param papp the application
	 * @param next_state next stable state
	 * @param next_sync next synchronization state
	 */
	ExitCode_t ChangeEXCState(
				AppPtr_t papp,
				app::Schedulable::State_t next_state,
				app::Schedulable::SyncState_t next_sync = app::Schedulable::SYNC_NONE);

	/**
	 * @brief Move the application from state vectors
	 *
	 * @param papp a pointer to an application
	 * @param prev previous application status
	 * @param next next application status
	 */
	ExitCode_t UpdateStatusMaps(AppPtr_t papp,
				Schedulable::State_t prev,
				Schedulable::State_t next);

	/**
	 * @brief Release a synchronization request for the specified application
	 *
	 * @param papp the application to release
	 * @param state the synchronization state to remove
	 */
	void RemoveFromSyncMap(AppPtr_t papp, Schedulable::SyncState_t state);

	/**
	 * @brief Release any synchronization request for the specified
	 * application
	 *
	 * @param papp the application to release
	 */
	void RemoveFromSyncMap(AppPtr_t papp);

	/**
	 * @brief Add a synchronization request for the specified application
	 *
	 * @param papp the application to synchronize
	 * @param state the synchronization state to add
	 */
	void AddToSyncMap(AppPtr_t papp, Schedulable::SyncState_t state);

	/**
	 * @brief Add the configured synchronization request for the specified
	 * application
	 *
	 * @param papp the application to synchronize
	 */
	void AddToSyncMap(AppPtr_t papp);


	/**
	 * @brief Clean-up the specified EXC
	 *
	 * Release all resources associated with the specified EXC.
	 */
	ExitCode_t CleanupEXC(AppPtr_t papp);

	/**
	 * @brief Clean-up all disabled EXCs
	 *
	 * Once an EXC is disabled and released, all the time consuming
	 * operations realted to releasing internal data structures (e.g.
	 * platform specific data) are performed by an anynchronous call to
	 * this method.
	 * An exeution of this method, which is managed as a deferrable task,
	 * is triggered by the DestroyEXC, this approach allows to:
	 * 1. keep short the critical path related to respond to an RTLib command
	 * 2. aggregate time consuming operations to be performed
	 * asynchronously
	 */
	void Cleanup();

	/**
	 * @brief The handler for commands defined by this module
	 */
	int CommandsCb(int argc, char *argv[]);

};

} // namespace bbque

#endif // BBQUE_APPLICATION_MANAGER_H_
