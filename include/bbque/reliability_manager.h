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

#ifndef BBQUE_RELIABILITY_MANAGER_H_
#define BBQUE_RELIABILITY_MANAGER_H_

#include "bbque/config.h"
#include "bbque/application_manager.h"
#include "bbque/command_manager.h"
#include "bbque/process_manager.h"
#include "bbque/platform_manager.h"
#include "bbque/resource_accounter.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/worker.h"

namespace bbque
{

/**
 * @class ReliabilityManager
 * @brief The class is responsible of monitoring the health status
 * of the hardware resources and managing the checkpoint/restore
 * of the applications.
 */
class ReliabilityManager: utils::Worker, CommandHandler
{

public:

	static ReliabilityManager & GetInstance();

	virtual ~ReliabilityManager();

	/**
	 * @brief Notify (to the manager) that an application
	 * has required a checkpoint
	 */
	void NotifyCheckpointRequest();


protected:

	/**
	 * @brief Monitor the health status of the HW resources
	 */
	void Task();

private:

	CommandManager & cm;

	ResourceAccounter & ra;

	ApplicationManager & am;

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER

	ProcessManager & prm;
#endif

	PlatformManager & plm;

	std::unique_ptr<utils::Logger> logger;

	/**
	 * @brief Constructor
	 */
	ReliabilityManager();

	/**
	 * @brief Notify the detection of a HW resource fault (or a
	 * "high" probability of that) to applications that may be
	 * affected.
	 */
	void NotifyFaultDetection(res::ResourcePtr_t rsrc);

	/**
	 * @brief Trigger the restore of a previously check-pointed
	 * application
	 */
	void Restore();

	/**
	 * @see CommandHandler
	 */
	int CommandsCb(int argc, char * argv[]);

	/**
	 * @brief Handler for resource performance degradation notification
	 *
	 * @param argc The command line arguments
	 * @param argv The command line arguments vector: {resource, value} pairs
	 *
	 * @return 0 if success, a positive integer value otherwise
	 */
	int ResourceDegradationHandler(int argc, char * argv[]);

	/**
	 * @brief Simualate the occurrence of a fault on a resource
	 * @param resource_path (string) resource path
	 */
	void SimulateFault(std::string const & resource_path);

	/**
	 * @brief Force the freezing of a given process/application
	 * @param uid Application or process id
	 * @param type ADATIVE or PROCESS
	 */
	void Freeze(app::AppUid_t uid, app::Schedulable::Type type);

	/**
	 * @brief Thaw a previously frozen application or process
	 * @param uid Application or process id
	 * @param type ADATIVE or PROCESS
	 */
	void Thaw(app::AppUid_t uid, app::Schedulable::Type type);

};

}

#endif // BBQUE_RELIABILITY_MANAGER_H_
