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

#ifndef BBQUE_SCHEDULER_POLICY_H_
#define BBQUE_SCHEDULER_POLICY_H_

#include "bbque/config.h"
#include "bbque/system.h"
#include "bbque/app/application_conf.h"
#include "bbque/app/working_mode.h"
#include "bbque/res/resources.h"

// The prefix for logging statements category
#define SCHEDULER_POLICY_NAMESPACE "bq.sp"
// The prefix for configuration file attributes
#define SCHEDULER_POLICY_CONFIG "SchedPol"
// The default base resource path for the binding step
#define SCHEDULER_DEFAULT_BINDING_DOMAIN "sys.cpu"

namespace ba = bbque::app;
namespace br = bbque::res;

namespace bbque
{
namespace plugins
{

/**
 * @class SchedulerPolicyIF
 * @ingroup sec05_sm
 * @brief A module interface to implement resource scheduler policies.
 *
 * This is an abstract class for interaction between the BarbequeRTRM and
 * a policy for scheduling of available resources.
 * This class could be used to implement resource scheduling alghoritms and
 * heuristics.
 */
class SchedulerPolicyIF
{

public:

	/**
	 * @brief Scheduling result
	 */
	typedef enum ExitCode {
		SCHED_DONE = 0,        /** Scheduling regular termination */
		SCHED_OK,              /** Successful return */
		SCHED_R_NOT_FOUND,     /** Looking for not existing resource */
		SCHED_R_UNAVAILABLE,   /** Not enough available resources */
		SCHED_SKIP_APP,        /** Skip the applicaton (disabled or already running) */
		SCHED_OPT_OVER,        /** No more static options to explore */
		SCHED_ERROR_QUEUE,     /** Error while using queue data structures to order objects */
		SCHED_ERROR_INIT,      /** Error during initialization */
		SCHED_ERROR_VIEW,      /** Error in using the resource state view */
		SCHED_ERROR_INCOMPLETE_ASSIGNMENT,
		SCHED_ERROR            /** Unexpected error */
	} ExitCode_t;

	/**
	 * @brief The scheduling entity to evaluate
	 *
	 * A scheduling entity is characterized by the Application/EXC to schedule, a
	 * Working Mode, and a Cluster ID referencing the resource binding
	 */
	struct EvalEntity_t {

		/**
		 * @brief Constructor
		 *
		 * @param _papp Application/EXC to schedule
		 * @param _pawm AWM to evaluate
		 * @param _bid Cluster ID for resource binding
		 */
		EvalEntity_t(ba::AppCPtr_t _papp, ba::AwmPtr_t _pawm, BBQUE_RID_TYPE _bid):
			papp(_papp),
			pawm(_pawm),
			bind_id(_bid)
		{
			_BuildStr();
		};

		/** Application/EXC to schedule */
		ba::AppCPtr_t papp;

		/** Candidate AWM */
		ba::AwmPtr_t pawm;

		/** Candidate cluster for resource binding */
		BBQUE_RID_TYPE bind_id;

		/** Type of resource for the candidate binding */
		br::ResourceType bind_type;
		/**
		 * A number through which reference the current scheduling binding
		 * in the set stored in the AWM descriptor */
		size_t bind_refn = 0;

		/** Identifier string */
		char str_id[40];

		/** Return the identifier string */
		const char * StrId() const
		{
			return str_id;
		}

		/** Build the identifier string */
		void _BuildStr()
		{
			int32_t awm_id = -1;
			if (pawm != nullptr)
				awm_id = pawm->Id();

			if ((bind_id != R_ID_NONE) && (bind_id != R_ID_ANY))
				snprintf(str_id, 40, "[%s] {AWM:%2d, B:%s%d}",
				         papp->StrId(), awm_id,
				         br::GetResourceTypeString(bind_type),
				         bind_id);
			else
				snprintf(str_id, 40, "[%s] {AWM:%2d, B: -}",
				         papp->StrId(), awm_id);
		}

		/** Set the working mode */
		void SetAWM(ba::AwmPtr_t _pawm)
		{
			pawm = _pawm;
			_BuildStr();
		}

		/** Set the binding ID to track */
		void SetBindingID(BBQUE_RID_TYPE bid, br::ResourceType btype)
		{
			bind_id = bid;
			bind_type = btype;
			_BuildStr();
		}

		/**
		 * Return true if the binding domain just set is different from the
		 * previous one (given the type of resource referenced by the such
		 * domain)
		 */
		bool IsMigrating(br::ResourceType r_type) const
		{
			return (papp->CurrentAWM() &&
			        !(papp->CurrentAWM()->BindingSet(r_type).Test(bind_id)));
		}

		/**
		 * Return true if this will be the first assignment of AWM (to the
		 * Application) or if the AWM assigned is different from the
		 * previous one
		 */
		bool IsReconfiguring() const
		{
			return (!papp->CurrentAWM() ||
			        papp->CurrentAWM()->Id() != pawm->Id());
		}
	};

	/**
	 * @class Scheduling entity
	 *
	 * This embodies all the information needed in the "selection" step to require
	 * a scheduling for an application into a specific AWM, with the resource set
	 * bound into a chosen cluster
	 */
	struct SchedEntity_t: public EvalEntity_t {

		/**
		 * @brief Constructor
		 *
		 * @param _papp Application/EXC to schedule
		 * @param _pawm AWM to evaluate
		 * @param _bid Cluster ID for resource binding
		 * @param _metr The related scheduling metrics (also "application
		 * value")
		 */
		SchedEntity_t(ba::AppCPtr_t _papp,
		              ba::AwmPtr_t _pawm,
		              BBQUE_RID_TYPE _bid,
		              float _metr):
			EvalEntity_t(_papp, _pawm, _bid),
			metrics(_metr)
		{
		};

		/** Metrics computed */
		float metrics;

		bool operator()(SchedEntity_t & se)
		{
			// Metrics (primary sorting key)
			if (metrics < se.metrics)
				return false;
			if (metrics > se.metrics)
				return true;

			// Higher value AWM first
			if (pawm->Value() > se.pawm->Value())
				return true;

			return false;
		}

	};

	/** Shared pointer to a scheduling entity */
	typedef std::shared_ptr<SchedEntity_t> SchedEntityPtr_t;

	/** List of scheduling entities */
	typedef std::list<SchedEntityPtr_t> SchedEntityList_t;


	/**
	 * @brief Default destructor
	 */
	virtual ~SchedulerPolicyIF() {};

	/**
	 * @brief Return the name of the optimization policy
	 * @return The name of the optimization policy
	 */
	virtual char const * Name() = 0;

	/**
	 * @brief Schedule a new set of applciation on available resources.
	 *
	 * @param system a reference to the system interfaces for retrieving
	 * information related to both resources and applications.
	 * @param rvt a token representing the view on resource allocation, if
	 * the scheduling has been successfull.
	 */
	virtual ExitCode_t Schedule(bbque::System & system,
	                            bbque::res::RViewToken_t &rvt) = 0;


protected:

	/** System view:
	 *  This points to the class providing the functions to query information
	 *  about applications and resources
	 */
	System * sys;

	/** Reference to the current scheduling status view of the resources */
	bbque::res::RViewToken_t sched_status_view;

	/** A counter used for getting always a new clean resources view */
	uint32_t status_view_count = 0;


	/** List of scheduling entities  */
	SchedEntityList_t entities;

	/** An High-Resolution timer */
	Timer timer;


	/**
	 * @brief General scheduling policy initialization code
	 */
	virtual ExitCode_t Init()
	{
		// Get a fresh resource status view
		++status_view_count;
		std::string token_path(SCHEDULER_POLICY_NAMESPACE);
		token_path += std::to_string(status_view_count);

		ResourceAccounter &ra = ResourceAccounter::GetInstance();
		auto ra_result = ra.GetView(token_path, sched_status_view);
		assert(ra_result == ResourceAccounterStatusIF::RA_SUCCESS);
		if (ra_result != ResourceAccounterStatusIF::RA_SUCCESS)
			return SCHED_ERROR_VIEW;

		// Policy-specific initialization
		return _Init();
	}

	/**
	 * @brief Politic-specific (optional) initialization function
	 */
	virtual ExitCode_t _Init()
	{
		return SCHED_OK;
	}

	/**
	 * @brief The number of slots, where a slot is defined as "resource
	 * amount divider". The idea is to provide a mechanism to assign
	 * resources to the application in a measure proportional to its
	 * priority:
	 *
	 * resource_slot_size  = resource_total / slots;
	 * resource_amount = resource_slot_size * (lowest_priority - (app_priority+1))
	 *
	 * @param system a reference to the system interfaces for retrieving
	 * information related to both resources and applications
	 * @return The number of slots
	 */
	uint32_t GetSlots()
	{
		uint32_t slots = 0;
		for (AppPrio_t prio = 0; prio <= sys->ApplicationLowestPriority(); prio++)
			slots += (sys->ApplicationLowestPriority() + 1 - prio) *
			         sys->ApplicationsCount(prio);
	/**
	 * @brief The same as GetSlots() but considering all the types of schedulable
	 * entities (adaptive applications, processes, ...)
	 * @return
	 */
	uint32_t GetSlotsForAllSchedulables()
	{
		uint32_t slots = 0;
		for (AppPrio_t prio = 0; prio <= sys->ApplicationLowestPriority(); prio++)
			slots += (sys->ApplicationLowestPriority() + 1 - prio) *
			sys->SchedulablesCount(prio);
		return slots;
	}

	/**
	 * @brief Execute a function over all the applications to schedule
	 * (READY, THAWED, RUNNING)
	 */
	ExitCode_t ForEachApplicationToScheduleDo(
	        std::function <ExitCode_t(bbque::app::AppCPtr_t) > do_func)
	{

		AppsUidMapIt app_it;
		ba::AppCPtr_t app_ptr;

		app_ptr = sys->GetFirstReady(app_it);
		for (; app_ptr; app_ptr = sys->GetNextReady(app_it)) {
			do_func(app_ptr);
		}

		app_ptr = sys->GetFirstThawed(app_it);
		for (; app_ptr; app_ptr = sys->GetNextThawed(app_it)) {
			do_func(app_ptr);
		}

		app_ptr = sys->GetFirstRestoring(app_it);
		for (; app_ptr; app_ptr = sys->GetNextRestoring(app_it)) {
			do_func(app_ptr);
		}

		app_ptr = sys->GetFirstRunning(app_it);
		for (; app_ptr; app_ptr = sys->GetNextRunning(app_it)) {
			do_func(app_ptr);
		}
		return SCHED_OK;
	}

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER

	/**
	 * @brief Execute a function over all the processes to schedule
	 * (READY, THAWED, RUNNING)
	 */
	ExitCode_t ForEachProcessToScheduleDo(
	        std::function <ExitCode_t(ProcPtr_t) > do_func)
	{

		ProcessManager & prm(ProcessManager::GetInstance());
		ProcessMapIterator proc_it;

		ProcPtr_t proc = prm.GetFirst(ba::Schedulable::READY, proc_it);
		for (; proc; proc = prm.GetNext(ba::Schedulable::READY, proc_it)) {
			do_func(proc);
		}

		proc = prm.GetFirst(ba::Schedulable::THAWED, proc_it);
		for (; proc; proc = prm.GetNext(ba::Schedulable::THAWED, proc_it)) {
			do_func(proc);
		}

		proc = prm.GetFirst(ba::Schedulable::RESTORING, proc_it);
		for (; proc; proc = prm.GetNext(ba::Schedulable::RESTORING, proc_it)) {
			do_func(proc);
		}

		proc = prm.GetFirst(ba::Schedulable::RUNNING, proc_it);
		for (; proc; proc = prm.GetNext(ba::Schedulable::RUNNING, proc_it)) {
			do_func(proc);
		}
		return SCHED_OK;
	}

#endif

};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_SCHEDULER_POLICY_H_
