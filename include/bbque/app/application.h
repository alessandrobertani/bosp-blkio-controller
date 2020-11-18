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

#ifndef BBQUE_APPLICATION_H
#define BBQUE_APPLICATION_H

#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>

#include "bbque/config.h"
#include "bbque/rtlib.h"
#include "bbque/app/application_conf.h"
#include "bbque/app/recipe.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/utility.h"

#include "tg/partition.h"

#define APPLICATION_NAMESPACE "bq.app"

namespace bu = bbque::utils;

namespace bbque {

class ApplicationManager;

namespace res {
class Resource;
class ResourcePath;
class ResourceConstraint;
class ResourceAssignment;
using ResourcePathPtr_t = std::shared_ptr<ResourcePath>;
using ResourceAssignmentPtr_t = std::shared_ptr<ResourceAssignment>;
using ResourceAssignmentMap_t = std::map<ResourcePathPtr_t, ResourceAssignmentPtr_t, CompareSP<ResourcePath>>;
using ResourceAssignmentMapPtr_t = std::shared_ptr<ResourceAssignmentMap_t>;
}

namespace br = bbque::res;

namespace app {

class Recipe;

/** Shared pointer to Recipe object */
typedef std::shared_ptr<Recipe> RecipePtr_t;
/** Pair contained in the resource constraints map  */
typedef std::pair<br::ResourcePathPtr_t, ConstrPtr_t> ConstrPair_t;

/**
 * @brief Collect the runtime profiling data from the RTLib
 */
struct RuntimeProfiling_t
{
	bool is_valid = false;

	/** Cycle time */
	int ctime_ms = 0;

	/** The current Goal-Gap value, must be in [-100,100] */
	int ggap_percent = 0;
	int ggap_percent_prev = 0;
	int ggap_percent_prediction = 0;

	struct
	{
		int upper_cpu = -1;
		int upper_gap = -1;
		int upper_age = -1;
		int lower_cpu = -1;
		int lower_gap = -1;
		int lower_age = -1;
	} gap_history;

	/** The current CPU Usage of an application. It should be received as a
	 * feedback from the rtlib. */

	struct
	{
		int curr = 0;
		int prev = 0;
		/** The maximum CPU Usage allocated to an application. It should be
		 * set by the scheduling policy and optionally compared with the variable
		 * `cpu_usage` */
		int predicted = 0;
		int predicted_prev = 0;
	} cpu_usage;
};

/**
 * @class ApplicationConfIF
 *
 * @brief Such descriptor includes static and dynamic information upon application
 * execution. It embeds usual information about name, priority, user, PID
 * (could be different from the one given by OS) plus a reference to the
 * recipe object, the list of enabled working modes and resource constraints.
 */
class Application : public ApplicationStatusIF, public ApplicationConfIF
{
	friend class bbque::ApplicationManager;

public:

	/**
	 * @brief Constructor with parameters name and priority class
	 * @param name Application name
	 * @param pid Process ID
	 * @param exc_id The ID of the Execution Context (assigned from the application)
	 * @param lang The application language
	 * @param container If true, this is just an "EXC container".
	 */
	Application(
		std::string const & name,
		AppPid_t pid,
		uint8_t exc_id,
		RTLIB_ProgrammingLanguage_t lang = RTLIB_LANG_CPP,
		bool container = false);

	/**
	 * @brief Default destructor
	 */
	virtual ~Application();

	/**
	 * @see ApplicationStatusIF
	 */
	uint8_t ExcId() const noexcept
	{
		return exc_id;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	RTLIB_ProgrammingLanguage_t Language() const noexcept
	{
		return language;
	}


	/**
	 * @brief Set the priority of the application
	 */
	void SetPriority(AppPrio_t prio);

	/**
	 * @see ApplicationStatusIF
	 */
	float Value() const noexcept
	{
		return schedule.value;
	}

	/**
	 * @see ApplicationConfIF
	 */
	void SetValue(float sched_metrics) noexcept
	{
		schedule.value = sched_metrics;
	}

	/**
	 * @brief This returns all the informations loaded from the recipe and
	 * stored in a specific object
	 * @return A shared pointer to the recipe object
	 */
	RecipePtr_t GetRecipe() noexcept
	{
		return recipe;
	}

	/**
	 * @brief Set the current recipe used by the application.
	 *
	 * The recipe should be loaded by the application manager using a
	 * specific plugin.
	 *
	 * @param recipe Recipe object shared pointer
	 * @param papp Shared pointer to the current Application.
	 *
	 * @return APP_SUCCESS if success, APP_RECP_NULL if a null Recipe object
	 * is passed
	 *
	 * @note papp should be provided by ApplicationManager, which instances
	 * the new Application descriptor
	 */
	ExitCode_t SetRecipe(RecipePtr_t & recipe, AppPtr_t & papp);

	/**
	 * @brief Reload the set of Working Modes
	 *
	 * This is usually required whenever there has been a change in the status
	 * of the resources (e.g., total availability) due to an HW fault or some
	 * low-level actions of power/thermal management, once a re-validation of
	 * the recipes has been performed.
	 *
	 * @papp Shared pointer to the current Application (provided by the
	 * Application Manager)
	 */
	void ReloadWorkingModes(AppPtr_t & papp)
	{
		return InitWorkingModes(papp);
	}

	/**
	 * @see ApplicationStatusIF
	 */
	bool IsContainer() const noexcept
	{
		return container;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	AwmPtrList_t const & WorkingModes() noexcept
	{
		return awms.enabled_list;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	AwmPtr_t const & LowValueAWM() noexcept
	{
		return awms.enabled_list.front();
	}

	/**
	 * @see ApplicationStatusIF
	 */
	AwmPtr_t const & HighValueAWM() noexcept
	{
		return awms.enabled_list.back();
	}

	/**
	 * @see ApplicationStatusIF
	 */
	uint64_t GetResourceRequestStat(
					std::string const & rsrc_path,
					ApplicationStatusIF::ResourceUsageStatType_t ru_stat);



	// -------------- Scheduling management ----------------------------- //

	/**
	 * @brief @see Schedulable
	 */
	void SetNextAWM(AwmPtr_t awm);

	// ------------------- Synchronization functions --------------------- //

	/**
	 * @brief Commit a previously required re-scheduling request
	 *
	 * @return APP_SUCCESS on successful update of internal data
	 * structures, APP_ABORT on errors.
	 */
	ExitCode_t SyncCommit();


	/**
	 * @brief The application can continue to run
	 *
	 * This method must be called by ApplicationManager to signal that the
	 * Application (previously RUNNING) doesn't need to be reconfigured or
	 * migrated. This it can continue to run in the same AWM, using the same
	 * resources.
	 *
	 * @return APP_SUCCESS for success, APP_ABORT for failed.
	 */
	ExitCode_t SyncContinue();


	// --------------------- Working Modes ----------------------------- //


	/**
	 * @brief Get a working mode descriptor
	 *
	 * @param wmId Working mode ID
	 * @return The (enabled) working mode descriptor
	 *
	 * @note The working mode must come from the enabled list
	 */
	AwmPtr_t GetWorkingMode(uint8_t wmId);

	/**
	 * @brief Set or clear a constraint on the working modes
	 *
	 * It's important to clearly explain the behavior of the following API.
	 * To set a lower bound means that all the AWMs having an ID lesser than
	 * the one specified in the method call will be disabled. Thus they will
	 * not considered by the scheduler. Similarly, but accordingly to the
	 * reverse logics, if an upper bound is set: all the working modes with ID
	 * greater to the one specified will be disabled.
	 * An "exact value" constraint acts in addictive way. This means that the
	 * working mode specified is added to the constraint range (if not
	 * included yet).
	 *
	 * Whenever a constraint of the same type occurs, the previous constraint
	 * is replaced. This means that if the application sets a lower bound, a
	 * second lower bound assertion will "overwrite" the firsts. The same for
	 * upper bound case.
	 *
	 * @param constraint @see RTLIB_Constraint
	 * @return APP_WM_ENAB_CHANGED if the set of enabled working modes has
	 * been successfully changed, APP_WM_ENAB_UNCHANGED otherwise.
	 * Then APP_WM_NOT_FOUND is returned if the working mode ID is not
	 * correct.
	 */
	ExitCode_t SetWorkingModeConstraint(RTLIB_Constraint & constraint);

	/**
	 * @brief Remove all the working mode constraints
	 *
	 * Reset the list of enabled working modes by included the whole set of
	 * working modes loaded from the recipe.
	 */
	void ClearWorkingModeConstraints();

	/**
	 * @brief Check the validity of the AWM scheduled
	 *
	 * This call check if the current scheduled AWM has been invalidated by a
	 * constraint assertion.
	 *
	 * @return true is the AWM is no more valid, false otherwise.
	 */
	bool CurrentAWMNotValid() const noexcept
	{
		return awms.curr_inv;
	}

	/**
	 * @brief Dump on logger a list of valid AWMs
	 *
	 * The list of AWMs which are currently valid is dumped on log with the
	 * Info loglevel.
	 */
	void DumpValidAWMs() const;


	// ---------------------- Profiling management ---------------------- //

	/**
	 * @brief Get the profiling data collected at runtime by the RTLib
	 *
	 * @param mark_acknowledged
	 * @return A data structure of type RuntimeProfiling_t
	 */
	RuntimeProfiling_t GetRuntimeProfile(bool mark_acknowledged = false)
	{
		std::unique_lock<std::mutex> rtp_lock(rt_prof_mtx);
		if (mark_acknowledged)
			rt_prof.is_valid = false;
		return rt_prof;
	}

	/**
	 * @brief Save the profiling data collected at runtime by the RTLib
	 *
	 * @param rt_profile The structure containing the profiling data
	 */
	void SetRuntimeProfile(struct RuntimeProfiling_t rt_profile)
	{
		std::unique_lock<std::mutex> rtp_lock(rt_prof_mtx);
		rt_prof = rt_profile;
	}

	/**
	 * @brief Save predictions about profiling data
	 *
	 * @param cpu_usage_prediction The expected amount of CPU usage
	 * @param goal_gap_prediction The expected goal gap
	 */
	void UpdateRuntimePredictions(int cpu_usage_prediction, int goal_gap_prediction = 0)
	{
		std::unique_lock<std::mutex> rtp_lock(rt_prof_mtx);
		rt_prof.cpu_usage.predicted_prev = rt_prof.cpu_usage.predicted;
		rt_prof.cpu_usage.predicted = cpu_usage_prediction;
		rt_prof.ggap_percent_prediction = goal_gap_prediction;
	}

	// -------------------------- Task-graph management ------------------------------------- //

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

	/**
	 * @brief Restore/retrieve the task-graph description
	 */
	ExitCode_t LoadTaskGraph();

	/**
	 * @brief Return the current task-graph description
	 */
	std::shared_ptr<TaskGraph> GetTaskGraph()
	{
		return task_graph;
	}

	/**
	 * @brief Set a new task-graph description
	 * @note Typically used by the scheduling policy for resource mapping purpose
	 * @param tg Shared pointer to task-graph descriptor
	 * @param write_through If set to true (default) update also the file or memory
	 * region storing the copy shared with the RTLib
	 */
	void SetTaskGraph(std::shared_ptr<TaskGraph> tg, bool write_through = true)
	{
		task_graph = tg;
		if (write_through) UpdateTaskGraph();
	}

	/**
	 * @brief Update the task-graph description shared with the RTLib
	 */
	void UpdateTaskGraph();

	/**
	 * @brief Clear the task-graph description (it optionally forces a reload)
	 */
	void ClearTaskGraph()
	{
		task_graph = nullptr;
	}

	/**
	 * @brief Performance requirements of a task
	 * @param task_id Task identification number
	 * @return A reference to TaskRequirements stored in the recipe object
	 */
	TaskRequirements const & GetTaskRequirements(uint32_t task_id) const
	{
		return recipe->GetTaskRequirements(task_id);
	}

	/**
	 * @brief Return the current partition
	 */
	std::shared_ptr<Partition> GetPartition()
	{
		return assigned_partition;
	}

	/**
	 * @brief Set the allocated partition
	 * @note Typically used by the scheduling policy for resource mapping purpose
	 * @param partition the allocated partition object
	 */
	void SetPartition(std::shared_ptr<Partition> partition)
	{
		assigned_partition = partition;
	}

#endif // CONFIG_BBQUE_TG_PROG_MODEL


private:

	/** The logger used by the application */
	std::unique_ptr<bu::Logger> logger;

	/** The user who has launched the application */
	std::string user;

	/** The ID of this Execution Context */
	uint8_t exc_id;

	AppUid_t uid;

	/** The programming language */
	RTLIB_ProgrammingLanguage_t language;

	/** True if this is an application container */
	bool container;

	/**
	 * @brief Store profiling information collected at runtime
	 */
	RuntimeProfiling_t rt_prof;

	std::mutex rt_prof_mtx;

#ifdef CONFIG_BBQUE_TG_PROG_MODEL
	/**
	 * Task-graph serialization file path
	 */
	std::string tg_path;

	/**
	 * Task-graph named semaphore
	 */
	std::string tg_sem_name;

	sem_t * tg_sem = nullptr;

	/**
	 * Task-graph descriptor (shared pointer to)
	 */
	std::shared_ptr<TaskGraph> task_graph;

#endif // CONFIG_BBQUE_TG_PROG_MODEL

	/**
	 * Assigned partition
	 */
	std::shared_ptr<Partition> assigned_partition = nullptr;

	/**
	 * Recipe pointer for the current application instance.
	 * At runtime we could manage many instances of the same application using
	 * different recipes.
	 */
	RecipePtr_t recipe;

	/** @struct WorkingModesInfo
	 *
	 * Store the information needed to support the management of the
	 * application working modes.
	 */
	struct WorkingModesInfo
	{
		AwmPtrVect_t recipe_vect;  // All the working modes
		AwmPtrList_t enabled_list; // Enabled working modes
		std::bitset<MAX_NUM_AWM> enabled_bset; // Enabled AWM bitset
		uint8_t low_id;         // Lower AWM ID
		uint8_t upp_id;         // Upper AWM ID
		uint8_t max_id;         // Max ID value
		bool curr_inv;          // Current AWM invalidated by constraint
	} awms;

	/**
	 * The following map keeps track of the constraints on resources.
	 * It is used by function UsageOutOfBounds() (see below) to check if a working
	 * mode includes a resource assignments that violates a bounds contained in this
	 * map.
	 **/
	ConstrMap_t rsrc_constraints;

	/**
	 * @brief Init working modes by reading the recipe
	 *
	 * @param papp Pointer to the current Application, allocated by
	 * ApplicationManager
	 */
	void InitWorkingModes(AppPtr_t & papp);

	/**
	 * @brief Init constraints by reading the recipe
	 *
	 * The method reads the "static" constraints on resources.
	 */
	void InitResourceConstraints();


	/**
	 * @brief Assert a specific resource constraint.
	 *
	 * If exists yet it is overwritten. This could disable some working modes.
	 *
	 * @param res_path Resource path
	 * @param type The constraint type (@see ContraintType)
	 * @param value The constraint value
	 *
	 * @return APP_SUCCESS if success, APP_RSRC_NOT_FOUND if the resource
	 * specified does not exist.
	 */
	ExitCode_t SetResourceConstraint(
					br::ResourcePathPtr_t r_path,
					br::ResourceConstraint::BoundType_t b_type,
					uint64_t value);

	/**
	 * @brief Remove a constraint upon a specific resource.
	 *
	 * This could re-enable some working modes.
	 *
	 * @param res_path A pointer to the resource object
	 * @param type The constraint type (@see ContraintType)
	 *
	 * @return APP_SUCCESS if success, APP_CONS_NOT_FOUND if cannot be found
	 * a previous constraint on the resource
	 */
	ExitCode_t ClearResourceConstraint(
					br::ResourcePathPtr_t r_path,
					br::ResourceConstraint::BoundType_t b_type);


	/**
	 * @brief Set a constraint on the working modes list
	 *
	 * Three types of constraints are provided, according to @see
	 * RTLIB_Constraint: lower bound, upper bound, exact value.
	 *
	 * @param constraint @see RTLIB_Constraint
	 * @return APP_WM_ENAB_CHANGED if the enabled AWM list has changed,
	 * APP_WM_ENAB_UNCHANGED otherwise.
	 */
	ExitCode_t AddWorkingModeConstraint(RTLIB_Constraint & constraint);

	/**
	 * @brief Set lower bound info into the awm_range struct
	 *
	 * The structure awm_range is properly filled with the info needed to
	 * rebuild the list of enabled working modes, according to the asserted
	 * lower bound.
	 *
	 * @param constraint @see RTLIB_Constraint
	 */
	void SetWorkingModesLowerBound(RTLIB_Constraint & constraint);

	/**
	 * @brief Set the upper bound info into the awm_range struct
	 *
	 * The structure awm_range is properly filled with the info needed to
	 * rebuild the list of enabled working modes, according to the asserted
	 * upper bound.
	 *
	 * @param constraint @see RTLIB_Constraint
	 */
	void SetWorkingModesUpperBound(RTLIB_Constraint & constraint);

	/**
	 * @brief Remove a constraint on a working modes list
	 *
	 * If the constraint is a lower bound, this is placed equal to 0. If it
	 * is an upper bound it is set to the max AWM ID. The bitset is then
	 * restored accordingly.
	 */
	ExitCode_t RemoveWorkingModeConstraint(RTLIB_Constraint & constraint);

	/**
	 * @brief Check if an AWM violates resource constraints
	 *
	 * @param awm Shared pointer to the AWM object
	 *
	 * @return true if it violates, false otherwise
	 */
	bool UsageOutOfBounds(AwmPtr_t & awm);

	/**
	 * @brief Rebuild the list of enabled working modes
	 *
	 * This is needed whenever a constraint has been asserted or removed.
	 * The bitmap of the enabled is scanned, each set bit is related to the ID
	 * of the AWM to consider enabled. If the resources required by the AWM
	 * do not violate any resource constraint, the AWM is inserted into the
	 * list. Finally the list is ordered by "AWM value".
	 */
	void RebuildEnabledWorkingModes();

	/**
	 * @brief Finalize the changes in the enabled working modes list
	 *
	 * When the list of enabled working modes changes, due to constraint
	 * assertions, a couple of operations are required:
	 * 1) we must signal if the currently scheduled AWM has been invalidated.
	 * 2) the list must be re-ordered by AWM value
	 * This is what this method does.
	 */
	void FinalizeEnabledWorkingModes();

	/**
	 * @brief Clear the lower bound info from awm_range struct
	 */
	void ClearWorkingModesLowerBound();

	/**
	 * @brief Clear the lower bound info from awm_range struct
	 */
	void ClearWorkingModesUpperBound();

	/**
	 * @brief Update enabled working modes list
	 *
	 * Whenever a resource constraint is set or removed, the method is called
	 * in order to check if there are some working mode to disable or
	 * re-enable.  The method rebuilds the list of enabled working modes by
	 * checking if there are some working modes requiring a resource usage
	 * which is out of the bounds set by a resource constraint assertion.
	 */
	void UpdateEnabledWorkingModes();

};

} // namespace app

} // namespace bbque

#endif // BBQUE_APPLICATION_H
