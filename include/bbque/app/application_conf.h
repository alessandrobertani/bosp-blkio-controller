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

#ifndef BBQUE_APPLICATION_CONF_IF_H_
#define BBQUE_APPLICATION_CONF_IF_H_

#include "bbque/app/application_status.h"
#include "tg/task_graph.h"
#include "tg/partition.h"

namespace bbque {

namespace res {
typedef size_t RViewToken_t;
}

namespace app {

class ApplicationConfIF;
class Application;
class WorkingMode;

typedef std::shared_ptr<Application> AppPtr_t;
typedef AppPtr_t AppCPtr_t;

/**
 * @class ApplicationConfIF
 * @brief Interface to configure application status
 *
 * This defines the interfaces for updating runtime information of the
 * application as priority, scheduled status and next working mode
 */
class ApplicationConfIF {

public:

	/**
	 * @brief Set the scheduling metrics value
	 *
	 * The usage of this method should be in charge of the scheduling policy
	 *
	 * @param sched_metrics The scheduling metrics computed by the policy
	 */
	virtual void SetValue(float sched_metrics) = 0;

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

	/**
	 * @brief Update the task-graph description shared with the RTLib
	 */
	virtual std::shared_ptr<TaskGraph> GetTaskGraph() = 0;

	/**
	 * @brief Set a new task-graph description
	 * @note Typically used by the scheduling policy for resource mapping purpose
	 * @param tg Shared pointer to task-graph descriptor
	 * @param write_through If set to true (default) update also the file or memory
	 * region storing the copy shared with the RTLib
	 */
	virtual void SetTaskGraph(std::shared_ptr<TaskGraph> tg, bool write_through=true) = 0;

	/**
	 * @brief Return the current partition
	 */
	virtual std::shared_ptr<Partition> GetPartition() = 0;

	/**
	 * @brief Set the allocated partition
	 * @note Typically used by the scheduling policy for resource mapping purpose
	 * @param partition the allocated partition object
	 */
	virtual void SetPartition(std::shared_ptr<Partition> partition) = 0;

#endif // CONFIG_BBQUE_TG_PROG_MODEL

};

} // namespace app

} // namespace bbque

#endif // BBQUE_APPLICATION_CONF_IF_H_
