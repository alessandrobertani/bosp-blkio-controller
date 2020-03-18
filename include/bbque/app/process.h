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

#ifndef BBQUE_PROCESS_H_
#define BBQUE_PROCESS_H_

#include <cstring>
#include "bbque/app/schedulable.h"
#include "bbque/utils/logging/logger.h"

namespace bbque {

class ProcessManager;

namespace app {

/**
 * @class Process
 * @brief This class is defined to instantiate descriptors of generic
 * processors (i.e. not AEM-integrated applications)
 */
class Process : public Schedulable
{
	friend class bbque::ProcessManager;

public:

	/**
	 * @struct SchedRequest_t
	 * @brief The set of resources required to schedule the process
	 */
	class ScheduleRequest
	{
	public:

		ScheduleRequest() :
		    cpu_cores(0), acc_cores(0), memory_mb(0) { }
		uint32_t cpu_cores;
		uint32_t acc_cores;
		uint32_t memory_mb;
	};

	using ScheduleRequestPtr_t = std::shared_ptr<ScheduleRequest>;

	/**
	 * @brief Constructur
	 * @param _pid the process id
	 * @param _prio the priority (optional)
	 */
	Process(std::string const & _name,
		AppPid_t _pid,
		AppPrio_t _prio = 0,
		Schedulable::State_t _state = READY,
		Schedulable::SyncState_t _sync = SYNC_NONE);

	/**
	 * @brief Destructor
	 */
	virtual ~Process() { }

	/**
	 * @brief Set the scheduling request of resources
	 * @param sched_req a ScheduleRequest_t object
	 */
	void SetScheduleRequestInfo(ScheduleRequestPtr_t sched_req)
	{
		this->sched_req = sched_req;
	}

	/**
	 * @brief Get the scheduling request of resources
	 * @param sched_req a ScheduleRequest_t object
	 */
	ScheduleRequestPtr_t const & GetScheduleRequestInfo()
	{
		return this->sched_req;
	}


private:

	std::unique_ptr<bbque::utils::Logger> logger;

	/** Request of resources for scheduling */
	ScheduleRequestPtr_t sched_req;

};


} // namespace app

} // namespace bbque

#endif // BBQUE_PROCESS_H_

