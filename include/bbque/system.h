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

#ifndef BBQUE_SYSTEM_H_
#define BBQUE_SYSTEM_H_

#include "bbque/application_manager.h"
#include "bbque/resource_accounter.h"

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
#include "bbque/process_manager.h"
#endif

namespace ba = bbque::app;
namespace br = bbque::res;

namespace bbque {

//extern std::set<ba::Schedulable::State_t> pending_states;

/**
 * @class System
 *
 * @details
 * This class provides all the methods necessary to get an aggregated view of
 * the system status, where under "system" we put the set of applications and
 * resources managed by the RTRM.  When instanced, the class get references to
 * the ApplicationManager and ResourceAccounter instances and it provides a
 * simplified set of methods for making queries upon applications and
 * resources status.
 */
class System
{
public:

	/**
	 * @brief Get the SystemVIew instance
	 */
	static System & GetInstance()
	{
		static System instance;
		return instance;
	}

	/// ...........................: APPLICATIONS :...........................

	/**
	 * @brief Return the first app at the specified priority
	 */
	ba::AppCPtr_t GetFirstWithPrio(AppPrio_t prio, AppsUidMapIt & ait)
	{
		return am.GetFirst(prio, ait);
	}

	/**
	 * @brief Return the next app at the specified priority
	 */
	ba::AppCPtr_t GetNextWithPrio(AppPrio_t prio, AppsUidMapIt & ait)
	{
		return am.GetNext(prio, ait);
	}

	/**
	 * @brief Return the map containing all the ready applications
	 */
	ba::AppCPtr_t GetFirstReady(AppsUidMapIt & ait)
	{
		return am.GetFirst(ba::Schedulable::State_t::READY, ait);
	}

	ba::AppCPtr_t GetNextReady(AppsUidMapIt & ait)
	{
		return am.GetNext(ba::Schedulable::State_t::READY, ait);
	}

	/**
	 * @brief Map of running applications (descriptors)
	 */
	ba::AppCPtr_t GetFirstRunning(AppsUidMapIt & ait)
	{
		return am.GetFirst(ba::Schedulable::State_t::RUNNING, ait);
	}

	ba::AppCPtr_t GetNextRunning(AppsUidMapIt & ait)
	{
		return am.GetNext(ba::Schedulable::State_t::RUNNING, ait);
	}

	/**
	 * @brief Map of blocked applications (descriptors)
	 */
	ba::AppCPtr_t GetFirstBlocked(AppsUidMapIt & ait)
	{
		return am.GetFirst(ba::Schedulable::SyncState_t::BLOCKED, ait);
	}

	ba::AppCPtr_t GetNextBlocked(AppsUidMapIt & ait)
	{
		return am.GetNext(ba::Schedulable::SyncState_t::BLOCKED, ait);
	}

	/**
	 * @brief Map of frozen applications to thaw (descriptors)
	 */
	ba::AppCPtr_t GetFirstThawed(AppsUidMapIt & ait)
	{
		return am.GetFirst(ba::Schedulable::State_t::THAWED, ait);
	}

	ba::AppCPtr_t GetNextThawed(AppsUidMapIt & ait)
	{
		return am.GetNext(ba::Schedulable::State_t::THAWED, ait);
	}

	/**
	 * @brief Map of applications to restore (descriptors)
	 */
	ba::AppCPtr_t GetFirstRestoring(AppsUidMapIt & ait)
	{
		return am.GetFirst(ba::Schedulable::State_t::RESTORING, ait);
	}

	ba::AppCPtr_t GetNextRestoring(AppsUidMapIt & ait)
	{
		return am.GetNext(ba::Schedulable::State_t::RESTORING, ait);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	bool HasApplications(AppPrio_t prio)
	{
		return am.HasApplications(prio);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	bool HasApplications(ba::Schedulable::State_t state)
	{
		return am.HasApplications(state);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	bool HasApplications(ba::Schedulable::SyncState_t sync_state)
	{
		return am.HasApplications(sync_state);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t ApplicationsCount(AppPrio_t prio)
	{
		return am.AppsCount(prio);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t ApplicationsCount(ba::Schedulable::State_t state)
	{
		return am.AppsCount(state);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t ApplicationsCount(ba::Schedulable::SyncState_t state)
	{
		return am.AppsCount(state);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	uint16_t ApplicationLowestPriority() const
	{
		return am.LowestPriority();
	}

	void LoadTaskGraphs()
	{
#ifdef CONFIG_BBQUE_TG_PROG_MODEL
		return am.LoadTaskGraphAll();
#endif
	}

	uint32_t ApplicationsTasksCount() const
	{
#ifdef CONFIG_BBQUE_TG_PROG_MODEL
		return am.TasksCount();
#endif
		return 0;
	}

	/**************************************************************************
	 *  Schedulable(s) management (Adaptive applications + Processes)         *
	 **************************************************************************/

	uint32_t SchedulablesCount(ba::Schedulable::State_t state)
	{
		return (am.AppsCount(state)
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
			+ prm.ProcessesCount(state)
#endif
			);
	}

	uint32_t SchedulablesCount(ba::AppPrio_t prio)
	{
		auto nr_apps = am.AppsCount(prio);
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGE
		// Processes are set at prio=0 by default
		if (prio == 0) {
			nr_apps + prm.ProcessesCount(prio);
		}
#endif
		return nr_apps;
	}

	bool HasSchedulables(ba::Schedulable::State_t state) const
	{
		return (am.HasApplications(state)
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
			|| prm.HasProcesses(state)
#endif
			);
	}

	bool HasSchedulables(ba::Schedulable::SyncState_t sync_state) const
	{
		return (am.HasApplications(sync_state)
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
			|| prm.HasProcesses(sync_state)
#endif
			);
	}

	/**
	 * @brief Applications or processes to be scheduled
	 * @return true if there are some, false otherwise
	 */
	bool HasSchedulablesToRun() const
	{
		for (auto & s : ba::Schedulable::pending_states) {
			if (HasSchedulables(s))
				return true;
		}
		return false;
	}

	/**************************************************************************
	 *  Resource management                                                   *
	 **************************************************************************/

	/**
	 * @see ResourceAccounterStatusIF::Available()
	 */
	uint64_t ResourceAvailable(std::string const & path,
				br::RViewToken_t status_view = 0,
				ba::SchedPtr_t papp = ba::SchedPtr_t()) const
	{
		return ra.Available(path, status_view, papp);
	}

	uint64_t ResourceAvailable(ResourcePathPtr_t ppath,
				br::RViewToken_t status_view = 0,
				ba::SchedPtr_t papp = SchedPtr_t()) const
	{
		return ra.Available(ppath, ResourceAccounter::UNDEFINED, status_view, papp);
	}

	uint64_t ResourceAvailable(br::ResourcePtrList_t & rsrc_list,
				br::RViewToken_t status_view = 0,
				ba::SchedPtr_t papp = ba::SchedPtr_t()) const
	{
		return ra.Available(rsrc_list, status_view, papp);
	}

	/**
	 * @see ResourceAccounterStatusIF::Total()
	 */
	uint64_t ResourceTotal(std::string const & path) const
	{
		return ra.Total(path);
	}

	uint64_t ResourceTotal(ResourcePathPtr_t ppath) const
	{
		return ra.Total(ppath, ResourceAccounter::UNDEFINED);
	}

	uint64_t ResourceTotal(br::ResourcePtrList_t & rsrc_list) const
	{
		return ra.Total(rsrc_list);
	}

	/**
	 * @see ResourceAccounterStatusIF::Used()
	 */
	uint64_t ResourceUsed(std::string const & path,
			br::RViewToken_t status_view = 0) const
	{
		return ra.Used(path, status_view);
	}

	uint64_t ResourceUsed(ResourcePathPtr_t ppath,
			br::RViewToken_t status_view = 0) const
	{
		return ra.Used(ppath, ResourceAccounter::UNDEFINED, status_view);
	}

	uint64_t ResourceUsed(br::ResourcePtrList_t & rsrc_list,
			br::RViewToken_t status_view = 0) const
	{
		return ra.Used(rsrc_list, status_view);
	}

	/**
	 * @see ResourceAccounterStatusIF::Used()
	 */
	uint64_t ResourceUsedBy(std::string const & path,
				ba::SchedPtr_t papp,
				br::RViewToken_t status_view = 0) const
	{
		return ra.UsedBy(path, papp, status_view);
	}

	uint64_t ResourceUsedBy(ResourcePathPtr_t ppath,
				ba::SchedPtr_t papp,
				br::RViewToken_t status_view = 0) const
	{
		return ra.UsedBy(ppath, papp, ResourceAccounter::MIXED, status_view);
	}

	uint64_t ResourceUsedBy(br::ResourcePtrList_t & rsrc_list,
				ba::SchedPtr_t papp,
				br::RViewToken_t status_view = 0) const
	{
		return ra.UsedBy(rsrc_list, papp, status_view);
	}


	/**
	 * @see ResourceAccounterStatusIF::Count()
	 */
	int16_t ResourceCount(br::ResourcePath & path) const;

	/**
	 * @see ResourceAccounterStatusIF::CountPerType()
	 */
	uint16_t ResourceCountPerType(br::ResourceType type) const
	{
		return ra.CountPerType(type);
	}

	/**
	 * @see ResourceAccounterStatusIF::CountTypes()
	 */
	uint16_t ResourceCountTypes() const
	{
		return ra.CountTypes();
	}

	/**
	 * @see ResourceAccounterStatusIF::GetTypes()
	 */
	std::map<br::ResourceType, std::set<BBQUE_RID_TYPE>> const ResourceTypes() const
	{
		return ra.GetTypes();
	}

	/**
	 * @see ResourceAccounterStatusIF::GetPath()
	 */
	br::ResourcePathPtr_t const GetResourcePath(
						std::string const & path)
	{
		return ra.GetPath(path);
	}

	/**
	 * @see ResourceAccounterStatusIF::GetResource()
	 */
	br::ResourcePtr_t GetResource(std::string const & path) const
	{
		return ra.GetResource(path);
	}

	br::ResourcePtr_t GetResource(ResourcePathPtr_t ppath) const
	{
		return ra.GetResource(ppath);
	}

	/**
	 * @see ResourceAccounterStatusIF::GetResources()
	 */
	br::ResourcePtrList_t GetResources(std::string const & temp_path) const
	{
		return ra.GetResources(temp_path);
	}

	br::ResourcePtrList_t GetResources(ResourcePathPtr_t ppath) const
	{
		return ra.GetResources(ppath);
	}

	/**
	 * @see ResourceAccounterStatusIF::ExistResources()
	 */
	bool ExistResource(std::string const & path) const
	{
		return ra.ExistResource(path);
	}

	bool ExistResource(ResourcePathPtr_t ppath) const
	{
		return ra.ExistResource(ppath);
	}

	bool ExistResourcePathsOfArch(ArchType arch_type) const
	{
		return ra.ExistResourcePathsOfArch(arch_type);
	}

	std::list<br::ResourcePathPtr_t> const & GetResourcePathListByArch(ArchType arch_type) const
	{
		return ra.GetResourcePathListByArch(arch_type);
	}

	/**
	 * @see ResourceAccounterConfIF::GetView()
	 */
	ResourceAccounterStatusIF::ExitCode_t GetResourceStateView(
								std::string req_id, br::RViewToken_t & tok)
	{
		return ra.GetView(req_id, tok);
	}

	/**
	 * @see ResourceAccounterConfIF::PutView()
	 */
	ResourceAccounterStatusIF::ExitCode_t PutResourceStateView(
								br::RViewToken_t tok)
	{
		return ra.PutView(tok);
	}

	br::RViewToken_t GetScheduledResourceStateView() const
	{
		return ra.GetScheduledView();
	}

	/***************************************************************
	 * Utility functions
	 ***************************************************************/

	void PrintStatus(bool verbose, br::RViewToken_t sched_status_view = 0) const
	{
		ra.PrintStatus(sched_status_view, verbose);
		am.PrintStatus(verbose);
		am.PrintStatusQ();
		am.PrintSyncQ();
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
		prm.PrintStatus(verbose);
#endif
	}

private:

	/** ApplicationManager instance */
	ApplicationManagerStatusIF & am;

	/** ResourceAccounter instance */
	ResourceAccounterConfIF & ra;

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	ProcessManager & prm;
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER

	/** Constructor */
	System() :
	    am(ApplicationManager::GetInstance()),
	    ra(ResourceAccounter::GetInstance())
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	    ,
	    prm(ProcessManager::GetInstance())
#endif // CONFIG_BBQUE_LINUX_PROC_MANAGER
	{ }
};


} // namespace bbque

#endif  // BBQUE_SYSTEM_H_
