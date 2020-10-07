/*
 * Copyright (C) 2017  Politecnico di Milano
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

#ifndef BBQUE_EXC_APP_CONTROL_H_
#define BBQUE_EXC_APP_CONTROL_H_

#include <string>

#include "bbque/rtlib.h"
#include <bbque/utils/logging/logger.h>

#include "tg/task_graph.h"
#include "pmsl/exec_synchronizer.h"

#define BBQUE_APP_CTRL_MODULE    "actrl"
#define BBQUE_APP_CONFIG_FILE    BBQUE_PATH_PREFIX "/" BBQUE_PATH_CONF "/libpms.conf"

namespace bbque {

/**
 * \class ApplicationController
 *
 * \brief This class enables the possibility of integrating a programming
 * model with the run-time managed application execution model (AEM)
 * introduced by the BarbequeRTRM.
 * Hence synchronizing the execution of the application with events coming
 * from both the programming model and the resource manager side.
 */
class ApplicationController {


public:

	/**
	 * \enum ExitCode
	 * \brief Class-specific exit codes
	 */
	enum class ExitCode {
		SUCCESS,
		ERR_INIT,               /*** Initialization failed   */
		ERR_APP_RECIPE,         /*** Cannot fine recipe file */
		ERR_NOT_INITIALIZED,    /*** Initialization not performed */
		ERR_RSRC_ALLOC_FAILED,  /*** Resource allocation failed */
		ERR_TASK_ID             /*** Invalid task id number */
	};


	/**
	 * \brief Constructor
	 * \param _name Application name
	 * \param _recipe Recipe file name (withouth .recipe extension)
	 */
	ApplicationController(
		std::string _name = std::string("app_ctrl"),
		std::string _recipe = std::string("generic"));

	virtual ~ApplicationController();


	/**
	 * \brief Initialization function
	 * It must be called before performing any other operation
	 * \return
	 */
	ExitCode Init() noexcept;

	/**
	 * \brief Send a request for assigning resources to the task graph
	 * \param tg Shared pointer to the task graph oject
	 * \return
	 */
	ExitCode GetResourceAllocation(std::shared_ptr<TaskGraph> tg) noexcept;


	/**
	 * \brief Notify that a task has been launched
	 * \param task_id Task id number
	 * \return
	 */
	ExitCode NotifyTaskStart(int task_id) noexcept;

	/**
	 * \brief Notify that a task has been stopped
	 * \param task_id Task id number
	 * \return
	 */
	ExitCode NotifyTaskStop(int task_id) noexcept;


	/**
	 * \brief Notify that a buffer has been written
	 * \param buff_id Buffer id number
	 * \return
	 */
	inline void NotifyEvent(uint32_t event_id) noexcept {
		if (exec_sync == nullptr) return;
		logger->Info("NotifyEvent: %d", event_id);
		return exec_sync->NotifyEvent(event_id);
	}


	/**
	 * \brief Wait for the entire task-graph execution to finish
	 */
	inline void WaitForTermination() noexcept {
		if (exec_sync == nullptr) return;
		exec_sync->WaitCompletion();
		logger->Info("[%s] closing the controller...", app_name.c_str());
	}

private:

	/*** Application name (binary name or other) */
	std::string app_name;

	/*** The application recipe (as required by the execution model) */
	std::string recipe;

	/** Logger */
	std::shared_ptr<bbque::utils::Logger> logger = nullptr;

	/*** The object acting as synchronizer between the API and the AEM */
	std::shared_ptr<ExecutionSynchronizer> exec_sync = nullptr;

 };


 }  // namespace bbque

 #endif  // BBQUE_EXC_APP_CONTROL_H_

