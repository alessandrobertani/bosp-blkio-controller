/*
 * Copyright (C) 2020  Politecnico di Milano
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

#ifndef BBQUE_CHECKPOINT_RESTORE_H_
#define BBQUE_CHECKPOINT_RESTORE_H_


#include <cstdint>
#include <cstring>
#include <string>
#include <boost/filesystem.hpp>

#include "bbque/config.h"
#include "bbque/app/schedulable.h"

namespace bbque
{

namespace pp
{

/**
 * @class ReliabilityActionsIF
 * @file reliability_actions_if.h
 * @brief Interface for performing checkpoint/restore (and similar) operations
 * on running applications/tasks.
 */
class ReliabilityActionsIF
{
public:

	enum class ExitCode_t
	{
		OK,
		ERROR_PROCESS_ID,
		ERROR_TASK_ID,
		ERROR_FILESYSTEM,
		ERROR_PERMISSIONS,
		ERROR_WRONG_STATE,
		ERROR_UNKNOWN
	};

	ReliabilityActionsIF() :
	    image_prefix_dir(BBQUE_CHECKPOINT_IMAGE_PATH),
	    freezer_prefix_dir(BBQUE_FREEZER_PATH)
	{
#ifdef CONFIG_BBQUE_RELIABILITY

		bbque_assert(strlen(BBQUE_CHECKPOINT_IMAGE_PATH) > 0);
		bbque_assert(strlen(BBQUE_FREEZER_PATH) > 0);

		if (!boost::filesystem::exists(image_prefix_dir)) {
			boost::filesystem::create_directories(image_prefix_dir);
		}

		if (!boost::filesystem::exists(freezer_prefix_dir)) {
			boost::filesystem::create_directory(freezer_prefix_dir);
		}
#endif
	}

	virtual ~ReliabilityActionsIF()
	{
	};

	/**
	 * @brief Perform the checkpoint (dump) of an application/process/task
	 * image
	 * @param exe_id the executable (task, process, application) id number
	 * @return
	 */
	virtual ExitCode_t Dump(uint32_t exe_id) = 0;

	/**
	 * @brief Perform the checkpoint (dump) of an application/process/task
	 * @param psched pointer to Schedulable object (Application, Process, ...)
	 * @return
	 */
	virtual ExitCode_t Dump(app::SchedPtr_t psched) = 0;

	/**
	 * @brief Perform the restore of an application/process/task
	 * @param exe_id the executable (task, process, application) id number
	 * @return
	 */
	virtual ExitCode_t Restore(uint32_t exe_id) = 0;

	/**
	 * @brief Perform the restore of an application/process/task
	 * @param psched pointer to Schedulable object (Application, Process, ...)
	 * @return
	 */
	virtual ExitCode_t Restore(app::SchedPtr_t psched) = 0;

	/**
	 * @brief Perform the restore of an application/process/task
	 * @param exe_id the executable (task, process, application) id number
	 * @param exe_name the executable name
	 * @return
	 */
	virtual ExitCode_t Restore(uint32_t exe_id, std::string exe_name) = 0;

	/**
	 * @brief Freeze the execution of a ask, process, application...
	 * @param exe_id the executable (task, process, application) id number
	 * @return
	 */
	virtual ExitCode_t Freeze(uint32_t exe_id) = 0;

	/**
	 * @brief Freeze the execution of a ask, process, application...
	 * @param psched pointer to Schedulable object (Application, Process, ...)
	 * @return
	 */
	virtual ExitCode_t Freeze(app::SchedPtr_t psched) = 0;

	/**
	 * @brief Unfreeze the execution of a ask, process, application...
	 * @param exe_id the executable (task, process, application) id number
	 * @return
	 */
	virtual ExitCode_t Thaw(uint32_t exe_id) = 0;

	/**
	 * @brief Unfreeze the execution of a ask, process, application...
	 * @param psched pointer to Schedulable object (Application, Process, ...)
	 * @return
	 */
	virtual ExitCode_t Thaw(app::SchedPtr_t papp) = 0;

protected:

	/**
	 * Prefix of the filesystem path for storing the application/process
	 * checkpoint images
	 */
	std::string image_prefix_dir;

	/**
	 * Prefix of the filesystem path for accessing the freezer interfaces
	 */
	std::string freezer_prefix_dir;
};

} // namespace pp

} // namespace bbque

#endif // BBQUE_CHECKPOINT_RESTORE_H_
