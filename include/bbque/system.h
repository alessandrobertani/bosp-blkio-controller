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
class System {

public:

	/**
	 * @brief Get the SystemVIew instance
	 */
	static System & GetInstance() {
		static System instance;
		return instance;
	}

	/// ...........................: APPLICATIONS :...........................

	/**
	 * @brief Return the first app at the specified priority
	 */
	inline ba::AppCPtr_t GetFirstWithPrio(AppPrio_t prio, AppsUidMapIt & ait) {
		return am.GetFirst(prio, ait);
	}

	/**
	 * @brief Return the next app at the specified priority
	 */
	inline ba::AppCPtr_t GetNextWithPrio(AppPrio_t prio, AppsUidMapIt & ait) {
		return am.GetNext(prio, ait);
	}


	/**
	 * @brief Return the map containing all the ready applications
	 */
	inline ba::AppCPtr_t GetFirstReady(AppsUidMapIt & ait) {
		return am.GetFirst(ba::ApplicationStatusIF::READY, ait);
	}

	inline ba::AppCPtr_t GetNextReady(AppsUidMapIt & ait) {
		return am.GetNext(ba::ApplicationStatusIF::READY, ait);
	}

	/**
	 * @brief Map of running applications (descriptors)
	 */
	inline ba::AppCPtr_t GetFirstRunning(AppsUidMapIt & ait) {
		return am.GetFirst(ba::ApplicationStatusIF::RUNNING, ait);
	}

	inline ba::AppCPtr_t GetNextRunning(AppsUidMapIt & ait) {
		return am.GetNext(ba::ApplicationStatusIF::RUNNING, ait);
	}

	/**
	 * @brief Map of blocked applications (descriptors)
	 */

	inline ba::AppCPtr_t GetFirstBlocked(AppsUidMapIt & ait) {
		return am.GetFirst(ba::ApplicationStatusIF::BLOCKED, ait);
	}

	inline ba::AppCPtr_t GetNextBlocked(AppsUidMapIt & ait) {
		return am.GetNext(ba::ApplicationStatusIF::BLOCKED, ait);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	inline bool HasApplications(AppPrio_t prio) {
		return am.HasApplications(prio);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	inline bool HasApplications(ba::ApplicationStatusIF::State_t state) {
		return am.HasApplications(state);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	inline bool HasApplications(ba::ApplicationStatusIF::SyncState_t sync_state) {
		return am.HasApplications(sync_state);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	inline uint16_t ApplicationsCount(AppPrio_t prio) {
		return am.AppsCount(prio);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	inline uint16_t ApplicationsCount(ba::ApplicationStatusIF::State_t state) {
		return am.AppsCount(state);
	}

	/**
	 * @see ApplicationManagerStatusIF
	 */
	inline uint16_t ApplicationsCount(ba::ApplicationStatusIF::SyncState_t state) {
		return am.AppsCount(state);
	}

	/**
	 * @brief Maximum integer value for the minimum application priority
	 */
	inline uint16_t ApplicationLowestPriority() const {
		return am.LowestPriority();
	}

	/**
	 * @brief Load all the application task-graphs
	 */
	inline void LoadTaskGraphs() {
#ifdef CONFIG_BBQUE_TG_PROG_MODEL
		return am.LoadTaskGraphAll();
#endif // CONFIG_BBQUE_TG_PROG_MODEL
	}


	/**************************************************************************
	 *  Schedulables management                                               *
	 **************************************************************************/

	inline uint32_t SchedulablesCount(ba::Schedulable::State_t state) {
		return (am.AppsCount(state)
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
			+ prm.ProcessesCount(state)
#endif
		);
	}

	inline bool HasSchedulables(ba::Schedulable::State_t state) {
		return (am.HasApplications(state)
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
			|| prm.HasProcesses(state)
#endif
		);
	}

	inline bool HasSchedulables(ba::Schedulable::SyncState_t sync_state) {
		return (am.HasApplications(sync_state)
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
			|| prm.HasProcesses(sync_state)
#endif
		);
	}


	/**************************************************************************
	 *  Resource management                                                   *
	 **************************************************************************/

	/**
	 * @see ResourceAccounterStatusIF::Available()
	 */
	inline uint64_t ResourceAvailable(std::string const & path,
			br::RViewToken_t status_view = 0, ba::SchedPtr_t papp = ba::SchedPtr_t()) const {
		return ra.Available(path, status_view, papp);
	}

	inline uint64_t ResourceAvailable(ResourcePathPtr_t ppath,
			br::RViewToken_t status_view = 0, ba::SchedPtr_t papp = SchedPtr_t()) const {
		return ra.Available(ppath, ResourceAccounter::UNDEFINED, status_view, papp);
	}

	inline uint64_t ResourceAvailable(br::ResourcePtrList_t & rsrc_list,
			br::RViewToken_t status_view = 0, ba::SchedPtr_t papp = ba::SchedPtr_t()) const {
		return ra.Available(rsrc_list, status_view, papp);
	}

	/**
	 * @see ResourceAccounterStatusIF::Total()
	 */
	inline uint64_t ResourceTotal(std::string const & path) const {
		return ra.Total(path);
	}

	inline uint64_t ResourceTotal(ResourcePathPtr_t ppath) const {
		return ra.Total(ppath, ResourceAccounter::UNDEFINED);
	}

	inline uint64_t ResourceTotal(br::ResourcePtrList_t & rsrc_list) const {
		return ra.Total(rsrc_list);
	}

	/**
	 * @see ResourceAccounterStatusIF::Used()
	 */
	inline uint64_t ResourceUsed(std::string const & path,
			br::RViewToken_t status_view = 0) const {
		return ra.Used(path, status_view);
	}

	inline uint64_t ResourceUsed(ResourcePathPtr_t ppath,
			br::RViewToken_t status_view = 0) const {
		return ra.Used(ppath, ResourceAccounter::UNDEFINED, status_view);
	}

	inline uint64_t ResourceUsed(br::ResourcePtrList_t & rsrc_list,
			br::RViewToken_t status_view = 0) const {
		return ra.Used(rsrc_list, status_view);
	}


	/**
	 * @see ResourceAccounterStatusIF::Used()
	 */
	uint64_t ResourceUsedBy(std::string const & path,
	                        ba::SchedPtr_t papp,
	                        br::RViewToken_t status_view = 0) const {
		return ra.UsedBy(path, papp, status_view);
	}

	uint64_t ResourceUsedBy(ResourcePathPtr_t ppath,
	                        ba::SchedPtr_t papp,
	                        br::RViewToken_t status_view = 0) const {
		return ra.UsedBy(ppath, papp, ResourceAccounter::UNDEFINED, status_view);
	}

	uint64_t ResourceUsedBy(br::ResourcePtrList_t & rsrc_list,
	                        ba::SchedPtr_t papp,
	                        br::RViewToken_t status_view = 0) const {
		return ra.UsedBy(rsrc_list, papp, status_view);
	}


	/**
	 * @see ResourceAccounterStatusIF::Count()
	 */
	inline int16_t ResourceCount(br::ResourcePath & path) const;

	/**
	 * @see ResourceAccounterStatusIF::CountPerType()
	 */
	inline uint16_t ResourceCountPerType(br::ResourceType type) const {
		return ra.CountPerType(type);
	}

	/**
	 * @see ResourceAccounterStatusIF::CountTypes()
	 */
	inline uint16_t ResourceCountTypes() const {
		return ra.CountTypes();
	}

	/**
	 * @see ResourceAccounterStatusIF::GetTypes()
	 */
	inline std::map<br::ResourceType, std::set<BBQUE_RID_TYPE>> const ResourceTypes() const {
		return ra.GetTypes();
	}

	/**
	 * @see ResourceAccounterStatusIF::GetPath()
	 */
	inline br::ResourcePathPtr_t const GetResourcePath(
			std::string const & path) {
		return ra.GetPath(path);
	}

	/**
	 * @see ResourceAccounterStatusIF::GetResource()
	 */
	inline br::ResourcePtr_t GetResource(std::string const & path) const {
		return ra.GetResource(path);
	}

	inline br::ResourcePtr_t GetResource(ResourcePathPtr_t ppath) const {
		return ra.GetResource(ppath);
	}

	/**
	 * @see ResourceAccounterStatusIF::GetResources()
	 */
	inline br::ResourcePtrList_t GetResources(std::string const & temp_path) const {
		return ra.GetResources(temp_path);
	}

	inline br::ResourcePtrList_t GetResources(ResourcePathPtr_t ppath) const {
		return ra.GetResources(ppath);
	}

	/**
	 * @see ResourceAccounterStatusIF::ExistResources()
	 */
	inline bool ExistResource(std::string const & path) const {
		return ra.ExistResource(path);
	}

	inline bool ExistResource(ResourcePathPtr_t ppath) const {
		return ra.ExistResource(ppath);
	}


	/**
	 * @see ResourceAccounterConfIF::GetView()
	 */
	inline ResourceAccounterStatusIF::ExitCode_t GetResourceStateView(
			std::string req_id, br::RViewToken_t & tok) {
		return ra.GetView(req_id, tok);
	}

	/**
	 * @see ResourceAccounterConfIF::PutView()
	 */
	inline ResourceAccounterStatusIF::ExitCode_t PutResourceStateView(
			br::RViewToken_t tok) {
		return ra.PutView(tok);
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
