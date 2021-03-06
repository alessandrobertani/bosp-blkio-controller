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
#include "bbque/utils/timer.h"
#include "bbque/utils/worker.h"

/** Do not accept shorter checkpoint period length */
#define BBQUE_MIN_CHECKPOINT_PERIOD_MS   10000

using namespace bbque::utils;

namespace bbque {

/**
 * @class ReliabilityManager
 * @brief The class is responsible of monitoring the health status
 * of the hardware resources and managing the checkpoint/restore
 * of the applications.
 */
class ReliabilityManager : utils::Worker, CommandHandler
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

	std::string checkpoint_appinfo_dir = BBQUE_CHECKPOINT_APPINFO_PATH "/";


#ifdef CONFIG_BBQUE_PERIODIC_CHECKPOINT

	Timer chk_timer;

	std::thread chk_thread;

	unsigned int chk_period_len = BBQUE_CHECKPOINT_PERIOD_LENGTH; // ms

	/**
	 * @brief Perform periodical checkpoints of the managed applications
	 * and processes
	 */
	void PeriodicCheckpointTask();

#endif

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
	 * @param pid Process id
	 */
	void Freeze(app::AppPid_t pid);

	/**
	 * @brief Thaw a previously frozen application or process
	 * @param pid Process id
	 */
	void Thaw(app::AppPid_t pid);

	/**
	 * @brief Perform the checkpoint of an application or process
	 * @param pid Process id
	 */
	void Dump(app::AppPid_t pid);

	/**
	 * @brief Perform the checkpoint of an application or process
	 * @param psched pointer to Schedulable object
	 */
	void Dump(app::SchedPtr_t psched);

	/**
	 * @brief Restore a checkpointed application or process
	 * @param pid Process id
	 * @param exe_name Executable name
	 */
	void Restore(app::AppPid_t pid, std::string exe_name);
};

}

#endif // BBQUE_RELIABILITY_MANAGER_H_
