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

#include "bbque/rtlib/rpc/pb_fifo/rpc_pb_fifo_client.h"

#include "bbque/rtlib/rpc/rpc_messages.h"
#include "bbque/rtlib/rpc/pb_fifo/rpc_pb_message_factory.h"
#include "bbque/utils/utility.h"
#include "bbque/utils/logging/console_logger.h"
#include "bbque/config.h"

#include <sys/prctl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

namespace bu = bbque::utils;

// Setup logging
#undef  BBQUE_LOG_MODULE
#define BBQUE_LOG_MODULE "rpc.fif"

#define RPC_FIFO_SEND_SIZE(RPC_MSG, SIZE)\
	logger->Debug("Tx [" #RPC_MSG "] Request "\
	              "FIFO_HDR [sze: %hd, off: %hd, typ: %hd], "\
	              "Bytes: %" PRIu32 "...\n",\
	              rf_ ## RPC_MSG.hdr.fifo_msg_size,\
	              rf_ ## RPC_MSG.hdr.rpc_msg_offset,\
	              rf_ ## RPC_MSG.hdr.rpc_msg_type,\
	              (uint32_t)SIZE\
	             );\
	if(::write(server_fifo_fd, (void*)&rf_ ## RPC_MSG, SIZE) <= 0) {\
		logger->Error("write to BBQUE fifo FAILED [%s]\n",\
		              bbque_fifo_path.c_str());\
		return RTLIB_BBQUE_CHANNEL_WRITE_FAILED;\
	}

#define RPC_FIFO_SEND(RPC_MSG)\
	RPC_FIFO_SEND_SIZE(RPC_MSG, FIFO_PKT_SIZE(RPC_MSG))

#undef RPC_PKT_SIZE
#define RPC_PKT_SIZE RPC_PB_FIFO_PAYLOAD_SIZE

namespace bbque
{
namespace rtlib
{

BbqueRPC_PB_FIFO_Client::BbqueRPC_PB_FIFO_Client() :
	BbqueRPC()
{
	logger->Debug("Building PROTOBUF FIFO RPC channel");
}

BbqueRPC_PB_FIFO_Client::~ BbqueRPC_PB_FIFO_Client()
{
	logger = bu::ConsoleLogger::GetInstance(BBQUE_LOG_MODULE);
	logger->Debug("BbqueRPC_PB_FIFO_Client dtor");
	ChannelRelease();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::ChannelRelease()
{
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_APP_EXIT, RpcMsgToken(), application_pid, 0);
	rpc_fifo_APP_EXIT_t rf_APP_EXIT = {
		{
			FIFO_PKT_SIZE(APP_EXIT),
			FIFO_PYL_OFFSET(APP_EXIT),
			RPC_APP_EXIT,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_APP_EXIT.pyl, msg.ByteSize());
	int error;
	logger->Debug("Releasing FIFO RPC channel");
	// Sending RPC Request
	RPC_FIFO_SEND(APP_EXIT);

	// Sending the same message to the Fetch Thread
	if (::write(client_fifo_fd, (void *) &rf_APP_EXIT,
	            FIFO_PKT_SIZE(APP_EXIT)) <= 0) {
		logger->Error("Notify fetch thread FAILED, FORCED EXIT");
	} else {
		// Joining fetch thread
		ChTrd.join();
	}

	// Closing the private FIFO
	error = ::unlink(app_fifo_path.c_str());

	if (error) {
		logger->Error("FAILED unlinking the application FIFO [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		return RTLIB_BBQUE_CHANNEL_TEARDOWN_FAILED;
	}

	return RTLIB_OK;
}

void BbqueRPC_PB_FIFO_Client::RpcBbqResp(unsigned int pyl_size)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	size_t bytes;
	// Read response RPC header
	uint8_t buffer[RPC_PKT_SIZE] = {0};
	bytes = ::read(client_fifo_fd, buffer, RPC_PKT_SIZE);
	chResp.ParseFromArray(buffer, pyl_size);

	if (bytes <= 0) {
		logger->Error("FAILED read from app fifo [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		chResp.set_result(RTLIB_BBQUE_CHANNEL_READ_FAILED);
	}

	// Notify about reception of a new response
	logger->Debug("Notify response [%d]", chResp.result());
	chResp_cv.notify_one();
}

void BbqueRPC_PB_FIFO_Client::ChannelFetch()
{
	rpc_fifo_header_t hdr;
	size_t bytes;
	logger->Debug("Waiting for FIFO header...");
	// Read FIFO header
	bytes = ::read(client_fifo_fd, (void *) &hdr, FIFO_PKT_SIZE(header));

	if (bytes <= 0) {
		logger->Error("FAILED read from app fifo [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		assert(bytes == FIFO_PKT_SIZE(header));
		// Exit the read thread if we are unable to read from the Barbeque
		// FIXME an error should be notified to the application
		done = true;
		return;
	}

	logger->Debug("Rx FIFO_HDR [sze: %hd, off: %hd, typ: %hd, pyl_size: %u]",
	              hdr.fifo_msg_size, hdr.rpc_msg_offset, hdr.rpc_msg_type, hdr.pyl_size);

	// Dispatching the received message
	switch (hdr.rpc_msg_type) {
	case RPC_APP_EXIT:
		done = true;
		break;

	//--- Application Originated Messages
	case RPC_APP_RESP:
		logger->Debug("APP_RESP");
		RpcBbqResp(hdr.pyl_size);
		break;

	//--- Execution Context Originated Messages
	case RPC_EXC_RESP:
		logger->Debug("EXC_RESP");
		RpcBbqResp(hdr.pyl_size);
		break;

	//--- Barbeque Originated Messages
	case RPC_BBQ_STOP_EXECUTION:
		logger->Debug("BBQ_STOP_EXECUTION");
		break;

	case RPC_BBQ_GET_PROFILE:
		logger->Debug("BBQ_STOP_EXECUTION");
		RpcBbqGetRuntimeProfile(hdr.pyl_size);
		break;

	case RPC_BBQ_SYNCP_PRECHANGE:
		logger->Debug("BBQ_SYNCP_PRECHANGE");
		RpcBbqSyncpPreChange(hdr.pyl_size);
		break;

	case RPC_BBQ_SYNCP_SYNCCHANGE:
		logger->Debug("BBQ_SYNCP_SYNCCHANGE");
		RpcBbqSyncpSyncChange(hdr.pyl_size);
		break;

	case RPC_BBQ_SYNCP_DOCHANGE:
		logger->Debug("BBQ_SYNCP_DOCHANGE");
		RpcBbqSyncpDoChange(hdr.pyl_size);
		break;

	case RPC_BBQ_SYNCP_POSTCHANGE:
		logger->Debug("BBQ_SYNCP_POSTCHANGE");
		RpcBbqSyncpPostChange(hdr.pyl_size);
		break;

	default:
		logger->Error("Unknown BBQ response/command [%d]", hdr.rpc_msg_type);
		assert(false);
		break;
	}
}

void BbqueRPC_PB_FIFO_Client::ChannelTrd(const char * name)
{
	std::unique_lock<std::mutex> trdStatus_ul(trdStatus_mtx);

	// Set the thread name
	if (BBQUE_UNLIKELY(prctl(PR_SET_NAME, (long unsigned int) "bq.fifo", 0, 0, 0)))
		logger->Error("Set name FAILED! (Error: %s)\n", strerror(errno));

	// Setup the RTLib UID
	SetChannelThreadID(gettid(), name);
	logger->Debug("channel thread [PID: %d] CREATED", channel_thread_pid);
	// Notifying the thread has beed started
	trdStatus_cv.notify_one();

	// Waiting for channel setup to be completed
	if (! running)
		trdStatus_cv.wait(trdStatus_ul);

	logger->Debug("channel thread [PID: %d] START", channel_thread_pid);

	while (! done)
		ChannelFetch();

	logger->Debug("channel thread [PID: %d] END", channel_thread_pid);
}

#define WAIT_RPC_RESP \
	chResp.set_result(RTLIB_BBQUE_CHANNEL_TIMEOUT); \
	chResp_cv.wait_for(chCommand_ul, \
	                   std::chrono::milliseconds(BBQUE_RPC_TIMEOUT)); \
	if (chResp.result() == RTLIB_BBQUE_CHANNEL_TIMEOUT) {\
		logger->Warn("RTLIB response TIMEOUT"); \
	}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::ChannelPair(const char * name)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_APP_PAIR, RpcMsgToken(), application_pid, 0);
	msg.set_mjr_version(BBQUE_RPC_FIFO_MAJOR_VERSION);
	msg.set_mnr_version(BBQUE_RPC_FIFO_MINOR_VERSION);
	msg.set_app_name(name);
	rpc_fifo_APP_PAIR_t rf_APP_PAIR = {
		{
			FIFO_PKT_SIZE(APP_PAIR),
			FIFO_PYL_OFFSET(APP_PAIR),
			RPC_APP_PAIR,
			msg.ByteSize()
		},
		"\0",
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_APP_PAIR.pyl, msg.ByteSize());
	::strncpy(rf_APP_PAIR.rpc_fifo, app_fifo_filename, BBQUE_FIFO_NAME_LENGTH);
	logger->Debug("Pairing FIFO channels [app: %s, pid: %d]", name,
	              application_pid);
	// Sending RPC Request
	RPC_FIFO_SEND(APP_PAIR);
	logger->Debug("Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::ChannelSetup()
{
	int error;
	logger->Debug("Initializing channel");
	// Opening server FIFO
	logger->Debug("Opening bbque fifo [%s]...", bbque_fifo_path.c_str());
	server_fifo_fd = ::open(bbque_fifo_path.c_str(), O_WRONLY | O_NONBLOCK);

	if (server_fifo_fd < 0) {
		logger->Error("FAILED opening bbque fifo [%s] (Error %d: %s)",
		              bbque_fifo_path.c_str(), errno, strerror(errno));
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}

	// Setting up application FIFO complete path
	app_fifo_path += app_fifo_filename;
	logger->Debug("Creating [%s]...", app_fifo_path.c_str());
	// Creating the client side pipe
	error = ::mkfifo(app_fifo_path.c_str(), 0644);

	if (error) {
		logger->Error("FAILED creating application FIFO [%s]",
		              app_fifo_path.c_str());
		::close(server_fifo_fd);
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}

	logger->Debug("Opening R/W...");
	// Opening the client side pipe
	// NOTE: this is opened R/W to keep it opened even if server
	// should disconnect
	client_fifo_fd = ::open(app_fifo_path.c_str(), O_RDWR);

	if (client_fifo_fd < 0) {
		logger->Error("FAILED opening application FIFO [%s]",
		              app_fifo_path.c_str());
		::unlink(app_fifo_path.c_str());
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}

	// Ensuring the FIFO is R/W to everyone
	if (fchmod(client_fifo_fd, S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH)) {
		logger->Error("FAILED setting permissions on RPC FIFO [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		::unlink(app_fifo_path.c_str());
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}

	return RTLIB_OK;

}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_Init(
        const char * name)
{
	std::unique_lock<std::mutex> trdStatus_ul(trdStatus_mtx);
	RTLIB_ExitCode_t result;
	// Starting the communication thread
	done = false;
	running = false;
	ChTrd = std::thread(&BbqueRPC_PB_FIFO_Client::ChannelTrd, this, name);
	trdStatus_cv.wait(trdStatus_ul);
	// Setting up application FIFO filename
	snprintf(app_fifo_filename, BBQUE_FIFO_NAME_LENGTH,
	         "bbque_%05d_%s", application_pid, name);
	// Setting up the communication channel
	result = ChannelSetup();

	if (result != RTLIB_OK)
		return result;

	// Start the reception thread
	running = true;
	trdStatus_cv.notify_one();
	trdStatus_ul.unlock();
	// Pairing channel with server
	result = ChannelPair(name);

	if (result != RTLIB_OK) {
		::unlink(app_fifo_path.c_str());
		::close(server_fifo_fd);
		return result;
	}

	return RTLIB_OK;
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_Register(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_REGISTER, RpcMsgToken(), application_pid, prec->id);
	msg.set_exc_name(prec->name);
	msg.set_recipe(prec->parameters.recipe);
	msg.set_lang(prec->parameters.language);
	rpc_fifo_EXC_REGISTER_t rf_EXC_REGISTER = {
		{
			FIFO_PKT_SIZE(EXC_REGISTER),
			FIFO_PYL_OFFSET(EXC_REGISTER),
			RPC_EXC_REGISTER,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_REGISTER.pyl, msg.ByteSize());
	logger->Debug("Registering EXC [%d:%d:%s:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id(),
	              msg.exc_name(),
	              msg.lang());
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_REGISTER);
	logger->Debug("Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_Unregister(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_UNREGISTER, RpcMsgToken(), application_pid, prec->id);
	msg.set_exc_name(prec->name);
	rpc_fifo_EXC_UNREGISTER_t rf_EXC_UNREGISTER = {
		{
			FIFO_PKT_SIZE(EXC_UNREGISTER),
			FIFO_PYL_OFFSET(EXC_UNREGISTER),
			RPC_EXC_UNREGISTER,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_UNREGISTER.pyl, msg.ByteSize());
	logger->Debug("Unregistering EXC [%d:%d:%s]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id(),
	              msg.exc_name());
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_UNREGISTER);
	logger->Debug("Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_Enable(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_START, RpcMsgToken(), application_pid, prec->id);
	rpc_fifo_EXC_START_t rf_EXC_START = {
		{
			FIFO_PKT_SIZE(EXC_START),
			FIFO_PYL_OFFSET(EXC_START),
			RPC_EXC_START,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_START.pyl, msg.ByteSize());
	logger->Debug("Enabling EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_START);
	logger->Debug("Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_Disable(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_STOP, RpcMsgToken(), application_pid, prec->id);
	rpc_fifo_EXC_STOP_t rf_EXC_STOP = {
		{
			FIFO_PKT_SIZE(EXC_STOP),
			FIFO_PYL_OFFSET(EXC_STOP),
			RPC_EXC_STOP,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_STOP.pyl, msg.ByteSize());
	logger->Debug("Disabling EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_STOP);
	logger->Debug("Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_Set(pRegisteredEXC_t prec,
                RTLIB_Constraint_t * constraints, uint8_t count)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_SET, RpcMsgToken(), application_pid, prec->id);
	// At least 1 constraint it is expected
	assert(count);
	RTLIB_Constraint_t *c;
	// Copy constraints
	for (uint16_t i = 0; i < count; i++) {
		c = constraints + i;
		PB_constraint *pb_c = msg.add_constraints();
		pb_c->set_awm(c->awm);
		pb_c->set_operation(c->operation);
		pb_c->set_type(c->type);
	}
	assert(count == msg.constraints_size());
	rpc_fifo_EXC_SET_t rf_EXC_SET = {
		{
			FIFO_PKT_SIZE(EXC_SET),
			FIFO_PYL_OFFSET(EXC_SET),
			RPC_EXC_SET,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_SET.pyl, msg.ByteSize());
	// Sending RPC Request
	logger->Debug("_Set: Set [%d] constraints on EXC [%d:%d]...",
	              count,
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	RPC_FIFO_SEND(EXC_SET);
	// Clean-up the FIFO message
	logger->Debug("_Set: Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_Clear(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_CLEAR, RpcMsgToken(), application_pid, prec->id);
	rpc_fifo_EXC_CLEAR_t rf_EXC_CLEAR = {
		{
			FIFO_PKT_SIZE(EXC_CLEAR),
			FIFO_PYL_OFFSET(EXC_CLEAR),
			RPC_EXC_CLEAR,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_CLEAR.pyl, msg.ByteSize());
	logger->Debug("_Clear: Remove constraints for EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_CLEAR);
	logger->Debug("_Clear: Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_RTNotify(pRegisteredEXC_t prec,
						    int cps_goal_gap,
						    int cpu_usage,
						    int cycle_time_ms,
						    int cycle_count)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_RTNOTIFY, RpcMsgToken(), application_pid, prec->id);
	msg.set_cps_goal_gap(cps_goal_gap);
	msg.set_cpu_usage(cpu_usage);
	msg.set_cycle_time_ms(cycle_time_ms);
	msg.set_cycle_count(cycle_count);
	rpc_fifo_EXC_RTNOTIFY_t rf_EXC_RTNOTIFY = {
		{
			FIFO_PKT_SIZE(EXC_RTNOTIFY),
			FIFO_PYL_OFFSET(EXC_RTNOTIFY),
			RPC_EXC_RTNOTIFY,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_RTNOTIFY.pyl, msg.ByteSize());
	logger->Debug("_RTNotify: Set Goal-Gap for EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());

	// Sending RPC Request
	if (! isSyncMode(prec))
		RPC_FIFO_SEND(EXC_RTNOTIFY);

	logger->Debug("_RTNotify: Waiting BBQUE response...");
	return RTLIB_OK;
	//WAIT_RPC_RESP;
	//return (RTLIB_ExitCode_t)chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_ScheduleRequest(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_EXC_SCHEDULE, RpcMsgToken(), application_pid, prec->id);
	rpc_fifo_EXC_SCHEDULE_t rf_EXC_SCHEDULE = {
		{
			FIFO_PKT_SIZE(EXC_SCHEDULE),
			FIFO_PYL_OFFSET(EXC_SCHEDULE),
			RPC_EXC_SCHEDULE,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_EXC_SCHEDULE.pyl, msg.ByteSize());
	logger->Debug("_ScheduleRequest: Schedule request for EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_SCHEDULE);
	logger->Debug("_ScheduleRequest: Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result();
}

void BbqueRPC_PB_FIFO_Client::_Exit()
{
	ChannelRelease();
}

/******************************************************************************
 * Synchronization Protocol Messages - PreChange
 ******************************************************************************/

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_SyncpPreChangeResp(
        rpc_msg_token_t token, pRegisteredEXC_t prec, uint32_t syncLatency)
{
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_BBQ_RESP, token, application_pid, prec->id);
	msg.mutable_hdr()->set_resp_type(PB_BBQ_SYNCP_PRECHANGE_RESP);
	msg.set_sync_latency(syncLatency);
	msg.set_result(RTLIB_OK);
	rpc_fifo_BBQ_SYNCP_PRECHANGE_RESP_t rf_BBQ_SYNCP_PRECHANGE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_SYNCP_PRECHANGE_RESP),
			FIFO_PYL_OFFSET(BBQ_SYNCP_PRECHANGE_RESP),
			RPC_BBQ_RESP,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_BBQ_SYNCP_PRECHANGE_RESP.pyl, msg.ByteSize());
	logger->Debug("PreChange response EXC [%d:%d] "
	              "latency [%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id(),
	              msg.sync_latency());
	// Sending RPC Request
	RPC_FIFO_SEND(BBQ_SYNCP_PRECHANGE_RESP);
	return RTLIB_OK;
}

void BbqueRPC_PB_FIFO_Client::RpcBbqSyncpPreChange(unsigned int pyl_size)
{
	PB_rpc_msg msg;
	size_t bytes;
	// Read response RPC header
	uint8_t buffer[RPC_PKT_SIZE] = {0};
	bytes = ::read(client_fifo_fd, buffer, RPC_PKT_SIZE);
	msg.ParseFromArray(buffer, pyl_size);


	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpPreChange: FAILED read from [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		chResp.set_result(RTLIB_BBQUE_CHANNEL_READ_FAILED);
	}

	std::vector<rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t> messages;

	for (uint16_t i = 0; i < msg.nr_sys(); i++) {
		rpc_fifo_header_t hdr;
		size_t bytes;
		// Read FIFO header
		bytes = ::read(client_fifo_fd, (void *) &hdr, FIFO_PKT_SIZE(header));

		if (bytes <= 0) {
			logger->Error("RpcBbqSyncpPreChange: FAILED read from [%s] (Error %d: %s)",
			              app_fifo_path.c_str(), errno, strerror(errno));
			assert(bytes == FIFO_PKT_SIZE(header));
			return;
		}

		// Read the message
		void *pyl_buffer = ::malloc(RPC_PKT_SIZE);
		bytes = ::read(client_fifo_fd, pyl_buffer, RPC_PKT_SIZE);

		if (bytes <= 0) {
			logger->Error("RpcBbqSyncpPreChange: FAILED read from [%s] (Error %d: %s)",
			              app_fifo_path.c_str(), errno, strerror(errno));
			chResp.set_result(RTLIB_BBQUE_CHANNEL_READ_FAILED);
		}

		PB_rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM sys;
		sys.ParseFromArray(pyl_buffer, hdr.pyl_size);
		rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t struct_sys = {
			(int16_t)sys.sys_id(),
			(int16_t)sys.nr_cpus(),
			(int16_t)sys.nr_procs(),
			sys.r_proc(),
			sys.r_mem(),
#ifdef CONFIG_TARGET_OPENCL
			sys.r_gpu(), sys
			sys.r_acc(),
			(int8_t)sys. > dev()
#endif // CONFIG_TARGET_OPENCL
		};
		messages.push_back(struct_sys);
	}

	assert(messages.size() == msg.nr_sys());

	// Notify the Pre-Change
	rpc_msg_BBQ_SYNCP_PRECHANGE_t struct_msg = {
		{
			RPC_STRUCT_HDR(msg)
		},
		(uint8_t)msg.event(),
		(int8_t)msg.awm(),
#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
		msg.cpu_ids(),
		msg.cpu_ids_isolation(),
		msg.mem_ids(),
#endif // CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
		(uint16_t)msg.nr_sys()
	};
	SyncP_PreChangeNotify(struct_msg, messages);
}

/******************************************************************************
 * Synchronization Protocol Messages - SyncChange
 ******************************************************************************/

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_SyncpSyncChangeResp(
        rpc_msg_token_t token, pRegisteredEXC_t prec, RTLIB_ExitCode_t sync)
{
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_BBQ_RESP, token, application_pid, prec->id);
	msg.set_result(sync);
	rpc_fifo_BBQ_SYNCP_SYNCCHANGE_RESP_t rf_BBQ_SYNCP_SYNCCHANGE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_SYNCP_SYNCCHANGE_RESP),
			FIFO_PYL_OFFSET(BBQ_SYNCP_SYNCCHANGE_RESP),
			RPC_BBQ_RESP,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_BBQ_SYNCP_SYNCCHANGE_RESP.pyl, msg.ByteSize());
	// Check that the ExitCode can be represented by the response message
	assert(sync < 256);
	logger->Debug("_SyncpSyncChangeResp: response EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	// Sending RPC Request
	RPC_FIFO_SEND(BBQ_SYNCP_SYNCCHANGE_RESP);
	return RTLIB_OK;
}

void BbqueRPC_PB_FIFO_Client::RpcBbqSyncpSyncChange(unsigned int pyl_size)
{
	PB_rpc_msg msg;
	size_t bytes;
	// Read response RPC header
	uint8_t buffer[RPC_PKT_SIZE] = {0};
	bytes = ::read(client_fifo_fd, buffer, RPC_PKT_SIZE);
	msg.ParseFromArray(buffer, pyl_size);

	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpSyncChange: FAILED read from [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		chResp.set_result(RTLIB_BBQUE_CHANNEL_READ_FAILED);
	}

	// Notify the Sync-Change
	rpc_msg_BBQ_SYNCP_SYNCCHANGE_t struct_msg = {
		{
			(uint8_t)msg.hdr().typ(),
			msg.hdr().token(),
			msg.hdr().app_pid(),
			(uint8_t)msg.hdr().exc_id()
		}
	};
	SyncP_SyncChangeNotify(struct_msg);
}

/******************************************************************************
 * Synchronization Protocol Messages - SyncChange
 ******************************************************************************/

void BbqueRPC_PB_FIFO_Client::RpcBbqSyncpDoChange(unsigned int pyl_size)
{
	PB_rpc_msg msg;
	size_t bytes;
	// Read response RPC header
	uint8_t buffer[RPC_PKT_SIZE] = {0};
	bytes = ::read(client_fifo_fd, buffer, RPC_PKT_SIZE);
	msg.ParseFromArray(buffer, pyl_size);

	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpDoChange: FAILED read from [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		chResp.set_result(RTLIB_BBQUE_CHANNEL_READ_FAILED);
	}

	// Notify the Sync-Change
	rpc_msg_BBQ_SYNCP_DOCHANGE_t struct_msg = {
		{
			(uint8_t)msg.hdr().typ(),
			msg.hdr().token(),
			msg.hdr().app_pid(),
			(uint8_t)msg.hdr().exc_id()
		}
	};
	SyncP_DoChangeNotify(struct_msg);
}

/******************************************************************************
 * Synchronization Protocol Messages - PostChange
 ******************************************************************************/

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_SyncpPostChangeResp(
        rpc_msg_token_t token, pRegisteredEXC_t prec,
        RTLIB_ExitCode_t result)
{
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_BBQ_RESP, token, application_pid, prec->id);
	msg.set_result(result);
	rpc_fifo_BBQ_SYNCP_POSTCHANGE_RESP_t rf_BBQ_SYNCP_POSTCHANGE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_SYNCP_POSTCHANGE_RESP),
			FIFO_PYL_OFFSET(BBQ_SYNCP_POSTCHANGE_RESP),
			RPC_BBQ_RESP,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_BBQ_SYNCP_POSTCHANGE_RESP.pyl, msg.ByteSize());
	// Check that the ExitCode can be represented by the response message
	assert(result < 256);
	logger->Debug("_SyncpPostChangeResp: response EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	// Sending RPC Request
	RPC_FIFO_SEND(BBQ_SYNCP_POSTCHANGE_RESP);
	return RTLIB_OK;
}

void BbqueRPC_PB_FIFO_Client::RpcBbqSyncpPostChange(unsigned int pyl_size)
{
	PB_rpc_msg msg;
	size_t bytes;
	// Read response RPC header
	uint8_t buffer[RPC_PKT_SIZE] = {0};
	bytes = ::read(client_fifo_fd, buffer, RPC_PKT_SIZE);
	msg.ParseFromArray(buffer, pyl_size);

	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpPostChange: FAILED read from [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		chResp.set_result(RTLIB_BBQUE_CHANNEL_READ_FAILED);
	}

	// Notify the Sync-Change
	rpc_msg_BBQ_SYNCP_POSTCHANGE_t struct_msg = {
		{
			(uint8_t)msg.hdr().typ(),
			msg.hdr().token(),
			msg.hdr().app_pid(),
			(uint8_t)msg.hdr().exc_id()
		}
	};
	SyncP_PostChangeNotify(struct_msg);
}

/*******************************************************************************
 * Runtime profiling
 ******************************************************************************/

void BbqueRPC_PB_FIFO_Client::RpcBbqGetRuntimeProfile(unsigned int pyl_size)
{
	PB_rpc_msg msg;
	size_t bytes;
	// Read RPC request
	uint8_t buffer[RPC_PKT_SIZE] = {0};
	bytes = ::read(client_fifo_fd, buffer, RPC_PKT_SIZE);
	msg.ParseFromArray(buffer, pyl_size);

	if (bytes <= 0) {
		logger->Error("RpcBbqGetRuntimeProfile: FAILED read from [%s] (Error %d: %s)",
		              app_fifo_path.c_str(), errno, strerror(errno));
		chResp.set_result(RTLIB_BBQUE_CHANNEL_READ_FAILED);
	}

	// Get runtime profile
	rpc_msg_BBQ_GET_PROFILE_t struct_msg = {
		{
			(uint8_t)msg.hdr().typ(),
			msg.hdr().token(),
			msg.hdr().app_pid(),
			(uint8_t)msg.hdr().exc_id()
		},
		msg.is_ocl()
	};
	GetRuntimeProfile(struct_msg);
}

RTLIB_ExitCode_t BbqueRPC_PB_FIFO_Client::_GetRuntimeProfileResp(
        rpc_msg_token_t token,
        pRegisteredEXC_t prec,
        uint32_t exc_time,
        uint32_t mem_time)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	PB_rpc_msg msg;
	PBMessageFactory::pb_set_header(msg, RPC_BBQ_RESP, token, application_pid, prec->id);
	msg.mutable_hdr()->set_resp_type(PB_BBQ_GET_PROFILE_RESP);
	msg.set_exec_time(exc_time);
	msg.set_mem_time(mem_time);
	rpc_fifo_BBQ_GET_PROFILE_RESP_t rf_BBQ_GET_PROFILE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_GET_PROFILE_RESP),
			FIFO_PYL_OFFSET(BBQ_GET_PROFILE_RESP),
			RPC_BBQ_RESP,
			msg.ByteSize()
		},
		{0}
	};
	logger->Debug("msg_size = %d", msg.ByteSize());
	msg.SerializeToArray(rf_BBQ_GET_PROFILE_RESP.pyl, msg.ByteSize());
	// Sending RPC response
	logger->Debug("_GetRuntimeProfileResp: Setting runtime profile info for EXC [%d:%d]...",
	              msg.hdr().app_pid(),
	              msg.hdr().exc_id());
	RPC_FIFO_SEND(BBQ_GET_PROFILE_RESP);
	return (RTLIB_ExitCode_t) chResp.result();
}

} // namespace rtlib

} // namespace bbque
