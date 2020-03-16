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

#include <fstream>

#include "bbque/app/schedulable.h"
#include "bbque/app/working_mode.h"
#include "bbque/resource_accounter.h"
#include "bbque/utils/logging/logger.h"

#include <boost/filesystem.hpp>

namespace bbque
{
namespace app
{


char const *Schedulable::stateStr[] = {
	"NEW",
	"READY",
	"SYNC",
	"RUNNING",
	"FROZEN",
	"THAWED",
	"RESTORING",
	"FINISHED"
};

char const *Schedulable::syncStateStr[] = {
	"STARTING",
	"RECONFIG",
	"R-MIGRATED",
	"MIGRATED",
	"BLOCKED",
	"DISABLED",

	"SYNC_NONE"
};

std::map<Schedulable::Type, std::string> Schedulable::typeStr = {
	{ Schedulable::Type::ADAPTIVE, "ADAPTIVE"},
	{ Schedulable::Type::PROCESS, "PROCESS"}
};

std::set<Schedulable::State_t> Schedulable::pending_states = {
	Schedulable::READY,
	Schedulable::RUNNING,
	Schedulable::THAWED,
	Schedulable::RESTORING
};

Schedulable::Schedulable(
	std::string const & _name,
	AppPid_t _pid,
	Schedulable::Type _type) :
	name(_name), pid(_pid), type(_type)
{
#ifdef CONFIG_BBQUE_RELIABILITY

	checkpoint_info_dir = BBQUE_CHECKPOINT_APPINFO_PATH "/";
	checkpoint_info_dir += std::to_string(pid) + "_" + name;

	try {
		if (!boost::filesystem::exists(checkpoint_info_dir)) {
			boost::filesystem::create_directory(checkpoint_info_dir);
			std::ofstream info_outf(
				checkpoint_info_dir + "/type",
				std::ofstream::out);
			info_outf << GetTypeStr() << std::endl;
			info_outf.close();
		}
	}
	catch (std::exception &ex) {

	}
#endif
}

/*******************************************************************************
 *  EXC State and SyncState Management
 ******************************************************************************/

void Schedulable::SetSyncState(SyncState_t sync)
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	schedule.syncState = sync;
}


Schedulable::ExitCode_t Schedulable::SetState(State_t next_state, SyncState_t next_sync)
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	// Switching to a sychronization state
	if (next_state == SYNC) {
		assert(next_sync != SYNC_NONE);
		if (next_sync == SYNC_NONE)
			return APP_SYNC_NOT_EXP;
		schedule.preSyncState = _State();         // Previous pre-synchronization state
		SetSyncState(next_sync);                  // Update synchronization state
		schedule.state = Schedulable::SYNC;       // Update state
		return APP_SUCCESS;
	}
	// Switching to a stable state
	else {
		assert(next_sync == SYNC_NONE);
		if (next_sync != SYNC_NONE)
			return APP_SYNC_NOT_EXP;
		schedule.preSyncState = schedule.state;   // Previous pre-synchronization state
		schedule.state = next_state;              // Updating state
		SetSyncState(SYNC_NONE);                  // Update synchronization state
	}

	// Update current and next working mode: SYNC case
	if ((next_sync == DISABLED)
	    || (next_sync == BLOCKED)
	    || (next_state == READY)) {
		schedule.awm.reset();
		schedule.next_awm.reset();
	} else if (next_state == RUNNING) {
		schedule.awm = schedule.next_awm;
		schedule.awm->IncSchedulingCount();
		++schedule.count;
		schedule.next_awm.reset();
	}

	return APP_SUCCESS;
}


Schedulable::State_t Schedulable::_State() const
{
	return schedule.state;
}

Schedulable::State_t Schedulable::State() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _State();
}

Schedulable::State_t Schedulable::_PreSyncState() const
{
	return schedule.preSyncState;
}

Schedulable::State_t Schedulable::PreSyncState() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _PreSyncState();
}

Schedulable::SyncState_t Schedulable::_SyncState() const
{
	return schedule.syncState;
}

Schedulable::SyncState_t Schedulable::SyncState() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _SyncState();
}

Schedulable::SyncState_t Schedulable::NextSyncState(AwmPtr_t const & next_awm) const
{
	std::unique_lock<std::recursive_mutex> schedule_ul(schedule.mtx);
	// First scheduling
	if (!schedule.awm)
		return STARTING;

	// Changing assigned resources: RECONF|MIGREC|MIGRATE
	if ((schedule.awm->Id() != next_awm->Id()) &&
	    (schedule.awm->BindingSet(br::ResourceType::CPU) !=
	     next_awm->BindingSet(br::ResourceType::CPU))) {
		return MIGREC;
	}

	if ((schedule.awm->Id() == next_awm->Id()) &&
	    (schedule.awm->BindingChanged(br::ResourceType::CPU))) {
		return MIGRATE;
	}

	if (schedule.awm->Id() != next_awm->Id()) {
		return RECONF;
	}

	// Check for inter-cluster resources re-assignement
	if (Reshuffling(next_awm)) {
		return RECONF;
	}

	// Thawed into the same AWM
	if (schedule.state == THAWED) {
		return STARTING;
	}

	// NOTE: By default no reconfiguration is assumed to be required, thus we
	// return the SYNC_STATE_COUNT which must be read as false values
	return SYNC_NONE;
}

void Schedulable::SetNextAWM(AwmPtr_t awm)
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	schedule.next_awm = awm;
}

bool Schedulable::_Disabled() const
{
	return (_SyncState() == DISABLED);
}

bool Schedulable::Disabled() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Disabled();
}

bool Schedulable::_Finished() const
{
	return (_State() == FINISHED);
}

bool Schedulable::Finished() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Finished();
}

bool Schedulable::_Active() const
{
	return ((schedule.state == READY) ||
	        (schedule.state == RUNNING));
}

bool Schedulable::Active() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Active();
}

bool Schedulable::_Running() const
{
	return ((schedule.state == RUNNING));
}

bool Schedulable::Running() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Running();
}

bool Schedulable::_Synching() const
{
	return (schedule.state == SYNC);
}

bool Schedulable::Synching() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Synching();
}

bool Schedulable::_Starting() const
{
	return (_Synching() && (_SyncState() == STARTING));
}

bool Schedulable::Starting() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Starting();
}

bool Schedulable::_Blocking() const
{
	return (_Synching() && (_SyncState() == BLOCKED));
}

bool Schedulable::Blocking() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Blocking();
}

bool Schedulable::_Restoring() const
{
	return (schedule.state == RESTORING);
}

bool Schedulable::Restoring() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _Restoring();
}

AwmPtr_t const & Schedulable::_CurrentAWM() const
{
	return schedule.awm;
}

AwmPtr_t const & Schedulable::CurrentAWM() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _CurrentAWM();
}

AwmPtr_t const & Schedulable::_NextAWM() const
{
	return schedule.next_awm;
}

AwmPtr_t const & Schedulable::NextAWM() const
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _NextAWM();
}

bool Schedulable::_SwitchingAWM() const
{
	if (schedule.state != SYNC)
		return false;
	if (schedule.awm->Id() == schedule.next_awm->Id())
		return false;
	return true;
}

bool Schedulable::SwitchingAWM() const noexcept
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return _SwitchingAWM();
}

uint64_t Schedulable::ScheduleCount() const noexcept
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	return schedule.count;
}

bool Schedulable::Reshuffling(AwmPtr_t const & next_awm) const
{
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	auto pumc = schedule.awm->GetResourceBinding();
	auto puma = next_awm->GetResourceBinding();
	state_ul.unlock();
	if (ra.IsReshuffling(pumc, puma))
		return true;
	return false;
}


} // namespace app

} // namespace bbque
