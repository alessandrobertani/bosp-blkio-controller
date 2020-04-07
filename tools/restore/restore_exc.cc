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

#include "restore_exc.h"

#include "config.h"
#include <bbque/utils/utility.h>
#include <criu/criu.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace bbque {

namespace tools {

BbqueRestoreEXC::BbqueRestoreEXC(
		std::string const & name,
		std::string const & recipe,
		RTLIB_Services_t * rtlib,
		std::string const & cdir,
		uint32_t __pid):
	BbqueEXC(name, recipe, rtlib),
	checkpoint_dir(cdir),
	pid(__pid),
	restored(false)
{
	logger->Notice("BbqueRestoreEXC: current pid=%u pid_to_restore=%d",
	               getpid(), pid);
}


RTLIB_ExitCode_t BbqueRestoreEXC::onSetup()
{
	logger->Info("BbqueRestoreEXC::onSetup() ");

	// CRIU initialization
	int c_ret = criu_init_opts();
	if (c_ret != 0) {
		logger->Error("CRIU initialization failed [ret=%d]", c_ret);
		return RTLIB_ERROR;
	}

	logger->Info("CRIU successfully initialized");
	//	criu_set_service_address(BBQUE_PATH_VAR);
	criu_set_service_binary(BBQUE_CRIU_BINARY_PATH);
	logger->Info("CRIU service binary: [%s]", BBQUE_CRIU_BINARY_PATH);

	int fd = open(checkpoint_dir.c_str(), O_DIRECTORY);
	criu_set_images_dir_fd(fd);
	criu_set_log_level(4);
	criu_set_log_file("restore.log");
	criu_set_pid(pid);
	criu_set_ext_unix_sk(true);
	criu_set_tcp_established(true);
	criu_set_evasive_devices(true);
	criu_set_file_locks(true);
	criu_set_shell_job(true);

	return RTLIB_OK;
}

RTLIB_ExitCode_t BbqueRestoreEXC::onConfigure(int8_t awm_id)
{
	logger->Info("BbqueRestoreEXC::onConfigure() ");

	/** Report the new resource assignment  */

	return RTLIB_OK;
}

RTLIB_ExitCode_t BbqueRestoreEXC::onRun()
{
	logger->Info("BbqueRestoreEXC::onRun() ");

	if (!restored) {
		logger->Notice("BbqueRestoreEXC: restoring [pid=%d] from pid=%d",
		              pid, getpid());

		int c_ret = criu_restore_child();
		if (c_ret < 0) {
			logger->Error("BbqueRestoreEXC: [pid=%d] error=%d", pid, c_ret);
			perror("BbqueRestoreEXC");
			return RTLIB_EXC_WORKLOAD_NONE;
		}
		restored = true;

	}

	return RTLIB_OK;
}


RTLIB_ExitCode_t BbqueRestoreEXC::onMonitor()
{
	RTLIB_WorkingModeParams_t const wmp = WorkingModeParams();

	logger->Info("BbqueRestoreEXC::onMonitor(): ");

	if (restored && kill(pid, 0) == 0) {
		logger->Notice("BbqueRestoreEXC: [pid=%d] restored", pid);
		return RTLIB_EXC_WORKLOAD_NONE;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	return RTLIB_OK;
}

RTLIB_ExitCode_t BbqueRestoreEXC::onRelease()
{
	logger->Info("BbqueRestoreEXC::onRelease(): exit");

	logger->Notice("BbqueRestoreEXC: synchronizing [pid=%d] termination", pid);
	int status;
	waitpid(pid, &status, 0);
	logger->Notice("BbqueRestoreEXC: application [pid=%d] terminated", pid);

	return RTLIB_OK;
}


} // tools

} // bbque
