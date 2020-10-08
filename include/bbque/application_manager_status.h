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

#ifndef BBQUE_APPLICATION_MANAGER_STATUS_IF_H_
#define BBQUE_APPLICATION_MANAGER_STATUS_IF_H_

#include "bbque/app/application.h"

using bbque::app::Schedulable;
using bbque::app::AppPid_t;
using bbque::app::AppUid_t;
using bbque::app::AppPrio_t;
using bbque::app::AppPtr_t;


namespace bbque
{

/**
 * Map containing shared pointers to Application descriptors, where the key is
 * the application PID
 */
typedef std::map<AppUid_t, AppPtr_t > AppsUidMap_t;

/**
 * Map containing shared pointers to Application descriptors, where the key is
 * the application PID
 */
typedef std::multimap<AppPid_t, AppPtr_t > AppsMap_t;

/**
 * An entry of the Application Map
 */
typedef std::pair<AppPid_t, AppPtr_t> AppsMapEntry_t;

/**
 * An entry of the UIDS Map
 */
typedef std::pair<AppUid_t, AppPtr_t> UidsMapEntry_t;


/*******************************************************************************
 *     In-Loop Erase Safe Iterator support
 ******************************************************************************/

// Forward declaration
class AppsUidMapIt;

/**
 * @typedef AppsUidMapItRetainer_t
 * @brief Retainer list of ILES iterators
 * @see AppsUidMapIt
 */
typedef std::list<struct AppsUidMapIt*> AppsUidMapItRetainer_t;

/**
 * @class AppsUidMapIt
 * @brief "In Loop Erase Safe" ILES iterator on an AppsUidMap_t maps
 *
 * This is an iterator wrapper object which is used to implement safe iterations on
 * mutable maps, where an erase could occours on a thread while another thread
 * is visiting the elements of the same container container.
 * A proper usage of such an ILES interator requires to visit the container
 * elements using a pair of provided functions "GetFirst" and "GetNext".
 * @see @ref GetFirst, @ref GetNext
 */
class AppsUidMapIt
{

public:

	~AppsUidMapIt()
	{
		Release();
	};

private:

	/** The map to visit */
	AppsUidMap_t *map = NULL;

	/** An interator on a UIDs map */
	AppsUidMap_t::iterator it;

	/** A flag to track iterator validity */
	bool updated = false;

	/** The retantion list on which this has been inserted */
	AppsUidMapItRetainer_t *ret = NULL;

	void Init(AppsUidMap_t & m, AppsUidMapItRetainer_t & rl)
	{
		map = &m;
		ret = &rl;
		it = map->begin();
	}
	void Retain()
	{
		ret->push_front(this);
	};
	void Release()
	{
		if (ret) ret->remove(this);
		ret = NULL;
	};
	void Update()
	{
		++it;
		updated = true;
	};
	void operator++(int)
	{
		if (!updated) ++it;
		updated = false;
	};
	bool End()
	{
		return (it == map->end());
	};
	AppPtr_t Get()
	{
		return (*it).second;
	};

	friend class ApplicationManager;
};


/*******************************************************************************
 *     Application Manager Status Interface
 ******************************************************************************/

/**
 * @class ApplicationManagerStatusIF
 * @ingroup sec03_am
  * @brief The status interface for ApplicationManager.
 *
 * This defines the interface of the ApplicationManager component for querying
 * the application runtime information. Currently we are interested in
 * getting a specific application descriptor, the lowest priority level
 * managed, and maps of application descriptors, even querying by scheduling
 * status or priority level.
 */
class ApplicationManagerStatusIF
{

public:

	virtual ~ApplicationManagerStatusIF() {};

	/**
	 * @brief Exit code to return
	 */
	enum ExitCode_t {
		AM_SUCCESS = 0,           /** Success */
		AM_RESCHED_REQUIRED,      /** Reschedule required */
		AM_AWM_NULL,              /** AWM descriptor is null */
		AM_AWM_NOT_SCHEDULABLE,   /** Not enough resource to assign the AWM */
		AM_APP_BLOCKING,          /** Trying to schedule a blocking application */
		AM_APP_DISABLED,          /** Application in disabled status */
		AM_EXC_NOT_FOUND,         /** Application Execution Context not found */
		AM_EXC_INVALID_STATUS,    /** Operation failed due to invalid status */
		AM_EXC_STATUS_CHANGE_FAILED,  /** Failed change of application status */
		AM_EXC_STATUS_CHANGE_NONE,    /** Nothing done in change of application status request */
		AM_PLAT_PROXY_ERROR,      /** Error accessing the platform proxy */
		AM_DATA_CORRUPT,          /** Inconsistency in internal data structures */
		AM_SKIPPING,              /** Interrupted operation */
		AM_ABORT                  /** Forced termination */
	};

	/**
	 * @brief Get the first element of UIDs map using an ILES iterator.
	 *
	 * Each time a module requires to visit the UID applications map, should
	 * use a pair of methods which ensure a proper handling of the container
	 * iterator. Indeed, for efficiency purposes, all the Barbeque containers
	 * are mutable and use fine-grained locking to better exploit the
	 * framework parallelism.
	 * Visiting a container requires to get its elements using a pair of
	 * function. This function is used to start the iteration thus getting the
	 * reference to the first element (if any).
	 *
	 * @param ait an ILES iterator instance, this is the same ILES iterator to
	 * be used to get the next elements
	 *
	 * @return a reference to the first application in the UIDs queue, or NULL
	 * if no applications are present.
	 *
	 * @see GetNext
	 */
	virtual AppPtr_t GetFirst(AppsUidMapIt & ait) = 0;

	/**
	 * @brief Get the next element of UIDs map using an ILES iterator.
	 *
	 * Visiting a Barbeque mutable container requires to get its elements
	 * using a pair of function. This function is used to continue a
	 * previously started iteration thus getting a reference to the next
	 * element (if any).
	 *
	 * @param ait an ILES iterator instance, this is the same ILES iterator
	 * used to get the first elements
	 *
	 * @return a reference to the next application in the UIDs queue, or NULL
	 * if no applications are present.
	 *
	 * @see GetFirst
	 */
	virtual AppPtr_t GetNext(AppsUidMapIt & ait) = 0;

	/**
	 * @brief Get the next element of PRIO queue using an ILES iterator.
	 *
	 * @param prio the priority queue to visit
	 * @param ait an ILES iterator instance, this is the same ILES iterator to
	 * be used to get the next elements
	 *
	 * @return a reference to the first application in the specified PRIO
	 * queue, or NULL if no applications are present.
	 */
	virtual AppPtr_t GetFirst(AppPrio_t prio, AppsUidMapIt & ait) = 0;

	/**
	 * @brief Get the next element of PRIO queue using an ILES iterator.
	 *
	 * @param prio the priority queue to visit
	 * @param ait an ILES iterator instance, this is the same ILES iterator
	 * used to get the first elements
	 *
	 * @return a reference to the next application in the UIDs queue, or NULL
	 * if no applications are present.
	 */
	virtual AppPtr_t GetNext(AppPrio_t prio, AppsUidMapIt & ait) = 0;

	/**
	 * @brief Get the next element of STATUS queue using an ILES iterator.
	 *
	 * @param state the status queue to visit
	 * @param ait an ILES iterator instance, this is the same ILES iterator to
	 * be used to get the next elements
	 *
	 * @return a reference to the first application in the specified STATUS
	 * queue, or NULL if no applications are present.
	 */
	virtual AppPtr_t GetFirst(
	        Schedulable::State_t state, AppsUidMapIt & ait) = 0;

	/**
	 * @brief Get the next element of STATUS queue using an ILES iterator.
	 *
	 * @param state the status queue to visit
	 * @param ait an ILES iterator instance, this is the same ILES iterator
	 * used to get the first elements
	 *
	 * @return a reference to the next application in the STATUS queue, or NULL
	 * if no applications are present.
	 */
	virtual AppPtr_t GetNext(
	        Schedulable::State_t state, AppsUidMapIt & ait) = 0;

	/**
	 * @brief Get the next element of SYNC queue using an ILES iterator.
	 *
	 * @param state the sync queue to visit
	 * @param ait an ILES iterator instance, this is the same ILES iterator to
	 * be used to get the next elements
	 *
	 * @return a reference to the first application in the specified SYNC
	 * queue, or NULL if no applications are present.
	 */
	virtual AppPtr_t GetFirst(
	        Schedulable::SyncState_t state, AppsUidMapIt & ait) = 0;

	/**
	 * @brief Get the next element of SYNC queue using an ILES iterator.
	 *
	 * @param state the sync queue to visit
	 * @param ait an ILES iterator instance, this is the same ILES iterator
	 * used to get the first elements
	 *
	 * @return a reference to the next application in the SYNC queue, or NULL
	 * if no applications are present.
	 */
	virtual AppPtr_t GetNext(
	        Schedulable::SyncState_t state, AppsUidMapIt & ait) = 0;

	/**
	 * @brief Check if the specified PRIO queue has applications
	 */
	virtual bool HasApplications(AppPrio_t prio) const = 0;

	/**
	 * @brief Check if the specified STATUS queue has applications
	 */
	virtual bool HasApplications(Schedulable::State_t state) const = 0;

	/**
	 * @brief Check if the specified SYNC queue has applications
	 */
	virtual bool HasApplications(Schedulable::SyncState_t state) const = 0;

	/**
	 * @brief Check if the specified language queue has applications
	 */
	virtual bool HasApplications(RTLIB_ProgrammingLanguage_t lang) const = 0;


	/**
	 * @brief The total number of applications
	 */
	virtual uint16_t AppsCount() const = 0;

	/**
	 * @brief The number of applications having the given PRIORITY
	 */
	virtual uint16_t AppsCount(AppPrio_t prio) const = 0;

	/**
	 * @brief The number of applications in the specified STATE
	 */
	virtual uint16_t AppsCount(Schedulable::State_t state) const = 0;

	/**
	 * @brief The number of applications in the specified SYNC_STATE
	 */
	virtual uint16_t AppsCount(Schedulable::SyncState_t state) const = 0;

	/**
	 * @brief The number of applications of the specified language type
	 */
	virtual uint16_t AppsCount(RTLIB_ProgrammingLanguage_t lang) const = 0;

	/**
	 * @brief One of the highest PRIORITY applications in the the
	 * specified state.
	 *
	 * @return a pointer to one of the highest priority application, which
	 * is currently on the specified state, or NULL if not applications
	 * are on this state.
	 */
	virtual AppPtr_t HighestPrio(Schedulable::State_t state) = 0;

	/**
	 * @brief One of the highest PRIORITY applications in the the
	 * specified synchronization state.
	 *
	 * @return a pointer to one of the highest priority application, which
	 * is currently on the specified state, or NULL if not applications
	 * are on this state.
	 */
	virtual AppPtr_t HighestPrio(Schedulable::SyncState_t syncState) = 0;

	/**
	 * @brief Retrieve an application descriptor (shared pointer) by PID and
	 * Excution Context
	 * @param pid Application PID
	 * @param exc_id Execution Contetx ID
	 */
	virtual AppPtr_t const GetApplication(AppPid_t pid, uint8_t exc_id) = 0;

	/**
	 * @brief Retrieve an application descriptor (shared pointer) by UID
	 *
	 * @param uid Application UID
	 */
	virtual AppPtr_t const GetApplication(AppUid_t uid) = 0;

	/**
	 * @brief Lowest application priority
	 * @return The maximum integer value for the (lowest) priority level
	 */
	virtual app::AppPrio_t LowestPriority() const = 0;

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

	/**
	 * @brief Load the task-graphs of all the active applications (ready
	 * and running)
	 */
	virtual void LoadTaskGraphAll() = 0;

	/**
	 * @brief Total count of the tasks
	 */
	virtual uint32_t TasksCount() const = 0;


#endif // CONFIG_BBQUE_TG_PROG_MODEL


	/*******************************************************************************
	 *     Status logging
	 ******************************************************************************/

	/**
	 * @brief Dump a logline to report on current Status queue counts
	 */
	virtual void PrintStatusQ() const = 0;

	/**
	 * @brief Dump a logline to report on current Status queue counts
	 */
	virtual void PrintSyncQ() const = 0;

	/**
	 * @brief Dump a logline to report all applications status
	 *
	 * @param verbose print in INFO logleve is ture, in DEBUG if false
	 */
	virtual void PrintStatus(bool verbose) = 0;

};

} // namespace bbque

#endif // BBQUE_APPLICATION_MANAGER_STATUS_IF_H_
