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

namespace bbque {

namespace tools {

BbqueRestoreEXC::BbqueRestoreEXC(
		std::string const & name,
		std::string const & recipe,
		RTLIB_Services_t * rtlib,
		std::string const & cdir):
	BbqueEXC(name, recipe, rtlib), checkpoint_dir(cdir)
{
	logger->Info("BbqueRestoreEXC: Unique identifier (UID)=%u", GetUniqueID());
}


RTLIB_ExitCode_t BbqueRestoreEXC::onSetup()
{
	logger->Info("BbqueRestoreEXC::onSetup() ");

	/** CRIU configuration */

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

	return RTLIB_OK;
}


RTLIB_ExitCode_t BbqueRestoreEXC::onMonitor()
{
	RTLIB_WorkingModeParams_t const wmp = WorkingModeParams();

	logger->Info("BbqueRestoreEXC::onMonitor(): ");

	// Error or clean shutdown
	return RTLIB_EXC_WORKLOAD_NONE;
}

RTLIB_ExitCode_t BbqueRestoreEXC::onRelease()
{
	logger->Info("BbqueRestoreEXC::onRelease(): exit");

	return RTLIB_OK;
}


} // tools

} // bbque
