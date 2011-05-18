/**
 *       @file  application.h
 *      @brief  Application descriptor
 *
 * This defines the application descriptor.
 * Such descriptor includes static and dynamic information upon application
 * execution. It embeds usual information about name, priority, user, PID
 * (could be different from the one given by OS) plus a reference to the
 * recipe object, the list of enabled working modes and resource constraints.
 *
 *     @author  Giuseppe Massari (jumanix), joe.massanga@gmail.com
 *
 *   @internal
 *     Created  05/04/2011
 *    Revision  $Id: doxygen.templates,v 1.3 2010/07/06 09:20:12 mehner Exp $
 *    Compiler  gcc/g++
 *     Company  Politecnico di Milano
 *   Copyright  Copyright (c) 2011, Giuseppe Massari
 *
 * This source code is released for free distribution under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * ============================================================================
 */

#ifndef BBQUE_APPLICATION_H
#define BBQUE_APPLICATION_H

#include <cassert>
#include <list>
#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

#include "bbque/object.h"
#include "bbque/app/application_conf.h"
#include "bbque/app/constraints.h"
#include "bbque/app/recipe.h"
#include "bbque/app/plugin_data.h"

#define APPLICATION_NAMESPACE "ap."

namespace bbque { namespace app {

// Forward declarations
class WorkingMode;
class Application;

/** Shared pointer to Application object */
typedef std::shared_ptr<Application> AppPtr_t;

/** Shared pointer to WorkingMode object */
typedef std::shared_ptr<WorkingMode> AwmPtr_t;

/** Shared pointer to Recipe object */
typedef std::shared_ptr<Recipe> RecipePtr_t;

/** Shared pointer to Constraint object */
typedef std::shared_ptr<Constraint> ConstrPtr_t;

/** Map of Constraints pointers, with the resource path as key*/
typedef std::map<std::string, ConstrPtr_t> ConstrPtrMap_t;


/**
 * @class Application
 * @brief Application descriptor object
 *
 * When an application enter the RTRM it should specify sets of informations
 * as name, pid, priority,... working modes (resources requirements),
 * constraints. This very basic needs to let the RTRM doing policy-driven
 * choices upon resource assignments to the applications.
 */
class Application: public Object, public ApplicationConfIF {

public:

	/**
	 * @brief Constructor with parameters name and priority class
	 * @param name Application name
	 * @param user The user who has launched the application
	 * @param pid Process ID
	 * @param exc_id The ID of the Execution Context (assigned from the application)
	 */
	explicit Application(std::string const & name, AppPid_t pid, uint8_t exc_id);

	/**
	 * @brief Default destructor
	 */
	virtual ~Application();

	/**
	 * @see ApplicationStatusIF
	 */
	inline std::string const & Name() const {
		return name;
	}

	/**
	 * @brief Set the application name
	 * @param app_name The application name
	 */
	inline void SetName(std::string const & app_name) {
		name = app_name;
	}

	/**
	 * @brief Get the process ID of the application
	 * @return PID value
	 */
	inline AppPid_t Pid() const {
		return pid;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	inline uint8_t ExcId() const {
		return exc_id;
	}

	/**
	 * @brief Get a string ID for this Execution Context
	 * This method build a string ID according to this format:
	 * <PID>:<TASK_NAME>:<EXC_ID>
	 * @return String ID
	 */
	inline const char *StrId() const {
		return str_id;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	AppPrio_t Priority() const {
		return priority;
	}

	/**
	 * @see ApplicationConfIF
	 */
	void SetPriority(AppPrio_t prio);

	/**
	 * @brief This returns all the informations loaded from the recipe and
	 * stored in a specific object
	 * @return A shared pointer to the recipe object
	 */
	inline RecipePtr_t GetRecipe() {
		return recipe;
	}

	/**
	 * @brief Set the current recipe used by the application. The recipe
	 * should be loaded by the application manager using a specific plugin.
	 * @param app_recipe Recipe object shared pointer
	 */
	inline void SetRecipe(RecipePtr_t app_recipe) {
		assert(app_recipe.get() != NULL);
		recipe = app_recipe;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	inline ScheduleFlag_t CurrentState() const {
		return curr_sched.state;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	inline AwmStatusPtr_t const CurrentAWM() const {
		return curr_sched.awm;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	inline ScheduleFlag_t NextState() const {
		return next_sched.state;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	inline AwmStatusPtr_t const NextAWM() const {
		return next_sched.awm;
	}

	/**
	 * @see ApplicationStatusIF
	 */
	AwmStatusPtrList_t const & WorkingModes();

	/**
	 * @see ApplicationStatusIF
	 */
	Application::ExitCode_t SetNextSchedule(std::string const & awm_name,
			ScheduleFlag_t state);

	/**
	 * @brief Switch from the current working mode to the next one.
	 * Then update transition overhead data and the scheduled status.
	 *
	 * @param time The time measured/estimated.
	 */
	void SwitchToNextScheduled(double time);

	/**
	 * @brief Check if the optimizer has set a new scheduling to switch in
	 * @return True for yes, false otherwise.
	 */
	bool MarkedToSwitch() const {
		return switch_mark;
	}

	/**
	 * @brief Stop the application execution
	 *
	 * Finalize the end of the execution.
	 */
	void StopExecution();

	/**
	 * @brief Define a specific resource constraint. If exists yet it
	 * is overwritten. This could bring to have some AWM disabled.
	 *
	 * @param res_path Resource path
	 * @param type The constraint type (@see ContraintType)
	 * @param value The constraint value
	 * @return An error code (@see ExitCode_t)
	 */
	Application::ExitCode_t SetConstraint(std::string const & res_path,
			Constraint::BoundType_t type, uint32_t value);

	/**
	 * @brief Remove a constraint upon a specific resource.
	 * This could bring to have some AWM re-enabled.
	 *
	 * @param res_path A pointer to the resource object
	 * @param type The constraint type (@see ContraintType)
	 * @return An error code (@see ExitCode)
	 */
	Application::ExitCode_t RemoveConstraint(std::string const & res_path,
			Constraint::BoundType_t type);

private:

	/** The application name */
	std::string name;

	/** The user who has launched the application */
	std::string user;

	/** The PID assigned from the OS */
	AppPid_t pid;

	/** The ID of this Execution Context */
	uint8_t exc_id;

	/** The application string ID */
	char str_id[16];

	/** A numeric priority value */
	AppPrio_t priority;

	/** Current scheduling informations */
	SchedulingInfo_t curr_sched;

	/** Next scheduled status */
	SchedulingInfo_t next_sched;

	/** Marker set when the application has been set for schedule switching */
	bool switch_mark;

	/**
	 * Recipe pointer for the current application instance.
	 * At runtime we could manage many instances of the same application using
	 * different recipes.
	 */
	RecipePtr_t recipe;

	/** Map of pointers to enabled working modes for the Optimizer module */
	AwmStatusPtrList_t enabled_awms;

	/** Runtime contraints specified by the application  */
	ConstrPtrMap_t constraints;

	/**
	 * @brief Whenever a constraint is set or removed, the method is called in
	 * order to check if there are some working mode to disable or re-enable.
	 *
	 * @param res_path Resource path upon which the constraint has been
	 * asserted or from which has been removed
	 * @param type Constraint type (lower or upper bound)
	 * @param value The value of the constraint
	 */
	void workingModesEnabling(std::string const & res_path,
			Constraint::BoundType_t type, uint64_t value);

};

} // namespace app

} // namespace bbque

#endif // BBQUE_APPLICATION_H

