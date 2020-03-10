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

#ifndef BBQUE_RESTORE_EXC_EXC_H_
#define BBQUE_RESTORE_EXC_EXC_H_

#include <string>
#include <bbque/bbque_exc.h>
#include <bbque/utils/logging/logger.h>

using bbque::rtlib::BbqueEXC;

extern std::unique_ptr<bbque::utils::Logger> logger;

namespace bbque {

namespace tools {

class BbqueRestoreEXC: public BbqueEXC {

public:

	BbqueRestoreEXC(
		std::string const & name,
		std::string const & recipe,
		RTLIB_Services_t  * rtlib,
		std::string const & checkpoint_dir,
		uint32_t pid);

	virtual	~BbqueRestoreEXC() { }

private:

	RTLIB_ExitCode_t onSetup();
	RTLIB_ExitCode_t onConfigure(int8_t awm_id);
	RTLIB_ExitCode_t onRun();
	RTLIB_ExitCode_t onMonitor();
	RTLIB_ExitCode_t onRelease();

	std::string checkpoint_dir;

	uint32_t pid;

	bool restored;

};

} // namespace tools

} // namespace bbque

#endif // BBQUE_RESTORE_EXC_EXC_H_
