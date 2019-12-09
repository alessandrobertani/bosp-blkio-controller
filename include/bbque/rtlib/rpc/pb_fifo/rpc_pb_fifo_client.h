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

#ifndef BBQUE_RPC_PB_FIFO_CLIENT_H_
#define BBQUE_RPC_PB_FIFO_CLIENT_H_

#include "bbque/rtlib.h"

#include "bbque/rtlib/bbque_rpc.h"
#include "bbque/rtlib/rpc/rpc_messages.h"
#include "rpc_messages.pb.h"
#include "bbque/rtlib/rpc/pb_fifo/rpc_pb_fifo_server.h"
#include "bbque/cpp11/condition_variable.h"
#include "bbque/cpp11/thread.h"

#include <sys/epoll.h>

namespace bbque
{
namespace rtlib
{

/**
 * @class BbqueRPC_PB_FIFO_Client
 *
 * @brief Client side of the RPC PROTOBUF FIFO channel
 *
 * Definition of the RPC protocol based on UNIX FIFOs to implement the
 * Barbeque communication channel. This defines the communication protocol in
 * terms of message format and functionalities.
 * The communication protocol must be aligend with the RTLib supported
 * services.
 *
 * @see bbque/rtlib.h
 * @see bbque/rtlib/rpc/rpc_messages.proto
 */
class BbqueRPC_PB_FIFO_Client : public BbqueRPC
{

public:

	BbqueRPC_PB_FIFO_Client();

	~BbqueRPC_PB_FIFO_Client();

protected:

	RTLIB_ExitCode_t _Init(const char * name);

	RTLIB_ExitCode_t _Register(pRegisteredEXC_t exc);

	RTLIB_ExitCode_t _Unregister(pRegisteredEXC_t exc);

	RTLIB_ExitCode_t _Enable(pRegisteredEXC_t exc);

	RTLIB_ExitCode_t _Disable(pRegisteredEXC_t exc);

	RTLIB_ExitCode_t _ScheduleRequest(pRegisteredEXC_t exc);

	RTLIB_ExitCode_t _Set(pRegisteredEXC_t exc,
	                      RTLIB_Constraint * constraints, uint8_t count);

	RTLIB_ExitCode_t _Clear(pRegisteredEXC_t exc);

	RTLIB_ExitCode_t _RTNotify(
	        pRegisteredEXC_t exc, int gap, int cusage, int ctime_ms);

	void _Exit();

	inline uint32_t RpcMsgToken()
	{
		return channel_thread_pid;
	}

	/******************************************************************************
	 * Runtime profile timing
	 ******************************************************************************/

	RTLIB_ExitCode_t _GetRuntimeProfileResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc,
	        uint32_t exc_time,
	        uint32_t mem_time);

	/******************************************************************************
	 * Synchronization Protocol Messages
	 ******************************************************************************/

	RTLIB_ExitCode_t _SyncpPreChangeResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc,
	        uint32_t syncLatency);

	RTLIB_ExitCode_t _SyncpSyncChangeResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc,
	        RTLIB_ExitCode_t sync);

	RTLIB_ExitCode_t _SyncpPostChangeResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc,
	        RTLIB_ExitCode_t result);

private:

	char app_fifo_filename[BBQUE_FIFO_NAME_LENGTH];

	std::string app_fifo_path = BBQUE_PATH_VAR "/";

	std::string bbque_fifo_path = BBQUE_PATH_VAR "/" BBQUE_PUBLIC_FIFO;

	int client_fifo_fd = 0;

	int server_fifo_fd = 0;

	bool done = false;

	bool running = false;

	std::thread ChTrd;

	std::mutex trdStatus_mtx;

	std::condition_variable trdStatus_cv;

	/**
	 * @brief Serialize sending of command using the library
	 *
	 * The current implementation of the library allows to send a single
	 * command at each time for single library instance. This is required do
	 * properly handle responses from Barbque.
	 * This mutex should be used to protect the chResp responce attribute,
	 * which is always set to the last received response from Barbques.
	 *
	 * @see chResp
	 */
	std::mutex chCommand_mtx;

	/**
	 * @brief Signal the reception of a response from Barbeque
	 *
	 * Each time a new message has been received from Barbeque by the channel
	 * fetch thread, this variable is notified. Thus, commands could wait for
	 * a response by susepnding on it.
	 */
	std::condition_variable chResp_cv;

	/**
	 * @brief The last response reveiced by Barbeque
	 *
	 * This attribute should be always protected by the chCommand_mtx
	 */
	PB_rpc_msg chResp;

	RTLIB_ExitCode_t ChannelRelease();

	RTLIB_ExitCode_t ChannelSetup();

	RTLIB_ExitCode_t ChannelPair(const char * name);

	void ChannelFetch();

	void ChannelTrd(const char * name);

	void RpcBbqResp(unsigned int pyl_size);

	/**
	 * @brief Get from FIFO a PreChange RPC message
	 */
	void RpcBbqSyncpPreChange(unsigned int pyl_size);

	/**
	 * @brief Get from FIFO a SyncChange RPC message
	 */
	void RpcBbqSyncpSyncChange(unsigned int pyl_size);

	/**
	 * @brief Get from FIFO a DoChange RPC message
	 */
	void RpcBbqSyncpDoChange(unsigned int pyl_size);

	/**
	 * @brief Get from FIFO a PostChange RPC message
	 */
	void RpcBbqSyncpPostChange(unsigned int pyl_size);

	/**
	 * @brief Get from FIFO a runtime profile request RPC message
	 */
	void RpcBbqGetRuntimeProfile(unsigned int pyl_size);

};

} // namespace rtlib

} // namespace bbque

#endif // BBQUE_RPC_FIFO_CLIENT_H_
