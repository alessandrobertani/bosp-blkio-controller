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

#ifndef BBQUE_RPC_UNMANAGED_CLIENT_H_
#define BBQUE_RPC_UNMANAGED_CLIENT_H_

#include "bbque/rtlib.h"

namespace bbque
{
namespace rtlib
{

/**
 * @class BbqueRPC_UNMANAGED_Client
 *
 * @brief Client side of the RPC Umnanaged channel
 *
 * An Unmanaged client is a BBQ client which is completely detached by the
 * BarbequeRTRM. This allows, for example, to run Bbque integrated
 * applications in case the BBQ daemon is not runinng. This is a convenient
 * execution mode especially useful to run testing and simulations.
 *
 * NOTE: Unmanaged clients always get assigned AWM 0.
 *
 */
class BbqueRPC_UNMANAGED_Client : public BbqueRPC
{

public:

	BbqueRPC_UNMANAGED_Client() {};

	~BbqueRPC_UNMANAGED_Client() {};

protected:

	RTLIB_ExitCode_t _Init(const char * name)
	{
		// Remove compilation warning
		SetChannelThreadID(gettid(), name);
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _Register(pRegisteredEXC_t exc)
	{
		// Remove compilation warning
		(void)exc;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _Unregister(pRegisteredEXC_t exc)
	{
		// Remove compilation warning
		(void)exc;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _Enable(pRegisteredEXC_t exc)
	{
		// Remove compilation warning
		(void)exc;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _Disable(pRegisteredEXC_t exc)
	{
		// Remove compilation warning
		(void)exc;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _ScheduleRequest(pRegisteredEXC_t exc)
	{
		// Remove compilation warning
		(void)exc;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _Set(pRegisteredEXC_t exc,
						  RTLIB_Constraint * constraints, uint8_t count)
	{
		// Remove compilation warning
		(void)exc;
		(void)count;
		(void)constraints;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _Clear(pRegisteredEXC_t exc)
	{
		// Remove compilation warning
		(void)exc;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _RTNotify(pRegisteredEXC_t exc, int gap,
							   int cusage, int ctime_ms)
	{
		// Remove compilation warning
		(void)exc;
		(void)gap;
		(void)cusage;
		(void)ctime_ms;
		return RTLIB_OK;
	}

	void _Exit() {}

	/******************************************************************************
	 * Runtime profiling
	 ******************************************************************************/

	RTLIB_ExitCode_t _GetRuntimeProfileResp(
		rpc_msg_token_t token,
		pRegisteredEXC_t exc,
		uint32_t exc_time,
		uint32_t mem_time)
	{
		// Remove compilation warning
		(void)token;
		(void)exc;
		(void)exc_time;
		(void)mem_time;
		return RTLIB_OK;
	}

	/******************************************************************************
	 * Synchronization Protocol Messages
	 ******************************************************************************/

	RTLIB_ExitCode_t _SyncpPreChangeResp(
		rpc_msg_token_t token,
		pRegisteredEXC_t exc,
		uint32_t syncLatency)
	{
		// Remove compilation warning
		(void)exc;
		(void)token;
		(void)syncLatency;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _SyncpSyncChangeResp(
		rpc_msg_token_t token,
		pRegisteredEXC_t exc,
		RTLIB_ExitCode_t sync)
	{
		// Remove compilation warning
		(void)exc;
		(void)token;
		(void)sync;
		return RTLIB_OK;
	}

	RTLIB_ExitCode_t _SyncpPostChangeResp(
		rpc_msg_token_t token,
		pRegisteredEXC_t exc,
		RTLIB_ExitCode_t result)
	{
		// Remove compilation warning
		(void)exc;
		(void)token;
		(void)result;
		return RTLIB_OK;
	}

};

} // namespace rtlib

} // namespace bbque

#endif // BBQUE_RPC_UNMANAGED_CLIENT_H_
