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

#include <iostream>

#include "pmsl/app_controller.h"

namespace bbque {


ApplicationController::ApplicationController(std::string _name, std::string _recipe):
		app_name(_name), recipe(_recipe) {

	bbque::utils::Logger::SetConfigurationFile(BBQUE_APP_CONFIG_FILE);
	logger = bbque::utils::Logger::GetLogger(BBQUE_APP_CTRL_MODULE);
	logger->Info("[%s] controller created", app_name.c_str());
}

ApplicationController::~ApplicationController() {
	logger->Info("[%s] controller destroyed", app_name.c_str());
}

ApplicationController::ExitCode ApplicationController::Init() noexcept {
	RTLIB_Services_t * rtlib_handler = nullptr;

	RTLIB_Init(app_name.c_str(), &rtlib_handler);
	if (rtlib_handler == nullptr) {
		logger->Error("Init: RTLib error");
		return ExitCode::ERR_INIT;
	}

	exec_sync = std::make_shared<ExecutionSynchronizer>(app_name, recipe, rtlib_handler);
	if (exec_sync == nullptr) {
		logger->Error("Init: recipe error");
		return ExitCode::ERR_APP_RECIPE;
	}

	logger->Info("Init: controller initialized");
	return ExitCode::SUCCESS;
}

ApplicationController::ExitCode ApplicationController::GetResourceAllocation(
	std::shared_ptr<TaskGraph> tg) noexcept {
	if (exec_sync == nullptr) {
		logger->Error("GetResourceAllocation: controller not initialized");
		return ExitCode::ERR_NOT_INITIALIZED;
	}

	exec_sync->SetTaskGraph(tg);
	logger->Debug("GetResourceAllocation: task-graph set");
	exec_sync->Start();
	logger->Debug("GetResourceAllocation: execution started");
	exec_sync->WaitForResourceAllocation();
	logger->Info("GetResourceAllocation: resource allocation performed");
	return ExitCode::SUCCESS;
}

ApplicationController::ExitCode ApplicationController::NotifyTaskStart(int task_id) noexcept {
	if (exec_sync == nullptr) {
		logger->Error("NotifyTaskStart: controller not initialized");
		return ExitCode::ERR_NOT_INITIALIZED;
	}

	exec_sync->StartTask(task_id);
	logger->Info("NotifyTaskStart: <task %d> start notified", task_id);
	return ExitCode::SUCCESS;
}


ApplicationController::ExitCode ApplicationController::NotifyTaskStop(int task_id) noexcept {
	if (exec_sync == nullptr) {
		logger->Error("NotifyTaskStop: controller not initialized");
		return ExitCode::ERR_NOT_INITIALIZED;
	}

	exec_sync->StopTask(task_id);
	logger->Info("NotifyTaskStop: <task %d> stop notified", task_id);

	return ExitCode::SUCCESS;
}


} // namespace bbque


