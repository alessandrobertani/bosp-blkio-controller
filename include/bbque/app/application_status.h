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

#ifndef BBQUE_APPLICATION_STATUS_IF_H_
#define BBQUE_APPLICATION_STATUS_IF_H_

#include <cassert>
#include <list>
#include <string>

#include "bbque/config.h"
#include "bbque/app/schedulable.h"
#include "bbque/rtlib.h"
#include "tg/requirements.h"


namespace bu = bbque::utils;

namespace bbque
{
namespace app
{


class ApplicationStatusIF;
/** Shared pointer to the class here defined */
typedef std::shared_ptr<ApplicationStatusIF> AppSPtr_t;
/** The application UID type */
typedef BBQUE_UID_TYPE AppUid_t;

/** List of WorkingMode pointers */
typedef std::list<AwmPtr_t> AwmPtrList_t;

/**
 * @brief Provide interfaces to query application information
 *
 * This defines the interface for querying Application runtime information:
 * name, priority, current  working mode and scheduled state, next working
 * mode and scheduled state, and the list of all the active working modes
 */
class ApplicationStatusIF: public app::Schedulable
{

public:

	/**
	 * @brief Type of resource usage statistics
	 */
	enum ResourceUsageStatType_t {
	        RU_STAT_MIN,
	        RU_STAT_AVG,
	        RU_STAT_MAX
	};

	ApplicationStatusIF(
		std::string const & name,
		AppPid_t pid,
		Schedulable::Type type):
	 Schedulable(name, pid, type) { }

	/**
	 * @brief Get the ID of this Execution Context
	 * @return PID value
	 */
	virtual uint8_t ExcId() const = 0;

	/**
	 * @brief Get the programming language
	 * @return The programming language enumerated value
	 */
	virtual RTLIB_ProgrammingLanguage_t Language() const = 0;

	/**
	 * @brief Get the UID of the current application
	 */
	AppUid_t Uid() const override {
		return (Pid() << BBQUE_UID_SHIFT) + ExcId();
	};

	/**
	 * @brief Get the UID of an application given its PID and EXC
	 */
	static AppUid_t Uid(AppPid_t pid, uint8_t exc_id) {
		return (pid << BBQUE_UID_SHIFT) + exc_id;
	};

	/**
	 * @brief Get the PID of an application given its UID
	 */
	static AppPid_t Uid2Pid(AppUid_t uid) {
		return (uid >> BBQUE_UID_SHIFT);
	};

	/**
	 * @brief Get the EID of an application given its UID
	 */
	static uint8_t Uid2Eid(AppUid_t uid) {
		return (uid & BBQUE_UID_MASK);
	};

	/**
	 * @brief The value set by the scheduling policy
	 *
	 * @return An index value
	 */
	virtual float Value() const = 0;

	/**
	 * @brief Check if this is an Application Container
	 * @return True if this is an application container
	 */
	virtual bool IsContainer() const = 0;

	/**
	 * @brief The enabled working modes
	 * @return All the schedulable working modes of the application
	 */
	virtual AwmPtrList_t const & WorkingModes() = 0;

	/**
	 * @brief The working mode with the lowest value
	 * @return A pointer to the working mode descriptor having the lowest
	 * value
	 */
	virtual AwmPtr_t const & LowValueAWM() = 0;

	/**
	 * @brief The working mode with the highest value
	 * @return A pointer to the working mode descriptor having the highest
	 * value
	 */
	virtual AwmPtr_t const & HighValueAWM() = 0;

	/**
	 * @brief Get Runtime Profile information for this app
	 *
	 * @param mark_outdated if true, mark runtime profile as outdated, aka
	 * already read and not useful anymore
	 *
	 * @return runtime information collected during app execution
	 */
	virtual struct RuntimeProfiling_t GetRuntimeProfile(
	        bool mark_outdated = false) = 0;

	/**
	 * @brief SetRuntime Profile information for this app
	 */
	virtual  void SetAllocationInfo(
	        int cpu_usage_prediction, int goal_gap_prediction = 0) = 0;

	/**
	 * @brief Statics about a specific resource usage requirement
	 *
	 * @param rsrc_path The path of the resource
	 * @param ru_stat The statistics type
	 *
	 * @return The minimum, maximum or average value of a resource usage,
	 * among all the enabled working modes
	 */
	virtual uint64_t GetResourceRequestStat(std::string const & rsrc_path,
	                                        ResourceUsageStatType_t ru_stat) = 0;

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

	/**
	 * @brief Performance requirements of a task (if specified in the recipe)
	 */
	virtual const TaskRequirements & GetTaskRequirements(uint32_t task_id) = 0;

#endif // CONFIG_BBQUE_TG_PROG_MODEL


};

} // namespace app

} // namespace bbque

#endif // BBQUE_APPLICATION_STATUS_IF_H_
