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

#include "bbque/rtlib/rpc/fifo/rpc_fifo_client.h"

#include "bbque/rtlib/rpc/rpc_messages.h"
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
	              "RPC_HDR [typ: %d, pid: %d, eid: %" PRIu8 "], Bytes: %" PRIu32 "...\n",\
	              rf_ ## RPC_MSG.hdr.fifo_msg_size,\
	              rf_ ## RPC_MSG.hdr.rpc_msg_offset,\
	              rf_ ## RPC_MSG.hdr.rpc_msg_type,\
	              rf_ ## RPC_MSG.pyl.hdr.typ,\
	              rf_ ## RPC_MSG.pyl.hdr.app_pid,\
	              rf_ ## RPC_MSG.pyl.hdr.exc_id,\
	              (uint32_t)SIZE\
		     );\
	if(::write(server_fifo_fd, (void*)&rf_ ## RPC_MSG, SIZE) <= 0) {\
		logger->Error("write to BBQUE fifo FAILED [%s]\n",\
		              bbque_fifo_path.c_str());\
		return RTLIB_BBQUE_CHANNEL_WRITE_FAILED;\
	}

#define RPC_FIFO_SEND(RPC_MSG)\
	RPC_FIFO_SEND_SIZE(RPC_MSG, FIFO_PKT_SIZE(RPC_MSG))

namespace bbque {
namespace rtlib {

BbqueRPC_FIFO_Client::BbqueRPC_FIFO_Client() :
    BbqueRPC()
{
	logger->Debug("Building FIFO RPC channel");
}

BbqueRPC_FIFO_Client::~BbqueRPC_FIFO_Client()
{
	logger = bu::ConsoleLogger::GetInstance(BBQUE_LOG_MODULE);
	logger->Debug("BbqueRPC_FIFO_Client dtor");
	ChannelRelease();
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::ChannelRelease()
{
	rpc_fifo_APP_EXIT_t rf_APP_EXIT = {
		{
			FIFO_PKT_SIZE(APP_EXIT),
			FIFO_PYL_OFFSET(APP_EXIT),
			RPC_APP_EXIT
		},
		{
			{
				RPC_APP_EXIT,
				RpcMsgToken(),
				application_pid,
				0
			}
		}
	};
	int error;
	logger->Debug("Releasing FIFO RPC channel");
	// Sending RPC Request
	RPC_FIFO_SEND(APP_EXIT);

	// Sending the same message to the Fetch Thread
	if (::write(client_fifo_fd, (void *) &rf_APP_EXIT,
		FIFO_PKT_SIZE(APP_EXIT)) <= 0) {
		logger->Error("Notify fetch thread FAILED, FORCED EXIT");
	}
	else {
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

void BbqueRPC_FIFO_Client::RpcBbqResp()
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	size_t bytes;
	// Read response RPC header
	bytes = ::read(client_fifo_fd, (void *) &chResp, RPC_PKT_SIZE(resp));

	if (bytes <= 0) {
		logger->Error("FAILED read from app fifo [%s] (Error %d: %s)",
			app_fifo_path.c_str(), errno, strerror(errno));
		chResp.result = RTLIB_BBQUE_CHANNEL_READ_FAILED;
	}

	// Notify about reception of a new response
	logger->Debug("Notify response [%d]", chResp.result);
	chResp_cv.notify_one();
}

void BbqueRPC_FIFO_Client::ChannelFetch()
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

	logger->Debug("Rx FIFO_HDR [sze: %hd, off: %hd, typ: %hd]",
		hdr.fifo_msg_size, hdr.rpc_msg_offset, hdr.rpc_msg_type);

	// Dispatching the received message
	switch (hdr.rpc_msg_type) {
	case RPC_APP_EXIT:
		done = true;
		break;

		//--- Application Originated Messages
	case RPC_APP_RESP:
		logger->Debug("APP_RESP");
		RpcBbqResp();
		break;

		//--- Execution Context Originated Messages
	case RPC_EXC_RESP:
		logger->Debug("EXC_RESP");
		RpcBbqResp();
		break;

		//--- Barbeque Originated Messages
	case RPC_BBQ_STOP_EXECUTION:
		logger->Debug("BBQ_STOP_EXECUTION");
		break;

	case RPC_BBQ_GET_PROFILE:
		logger->Debug("BBQ_STOP_EXECUTION");
		RpcBbqGetRuntimeProfile();
		break;

	case RPC_BBQ_SYNCP_PRECHANGE:
		logger->Debug("BBQ_SYNCP_PRECHANGE");
		RpcBbqSyncpPreChange();
		break;

	case RPC_BBQ_SYNCP_SYNCCHANGE:
		logger->Debug("BBQ_SYNCP_SYNCCHANGE");
		RpcBbqSyncpSyncChange();
		break;

	case RPC_BBQ_SYNCP_DOCHANGE:
		logger->Debug("BBQ_SYNCP_DOCHANGE");
		RpcBbqSyncpDoChange();
		break;

	case RPC_BBQ_SYNCP_POSTCHANGE:
		logger->Debug("BBQ_SYNCP_POSTCHANGE");
		RpcBbqSyncpPostChange();
		break;

	default:
		logger->Error("Unknown BBQ response/command [%d]", hdr.rpc_msg_type);
		assert(false);
		break;
	}
}

void BbqueRPC_FIFO_Client::ChannelTrd(const char * name)
{
	std::unique_lock<std::mutex> trdStatus_ul(trdStatus_mtx);

	// Set the thread name
	if (BBQUE_UNLIKELY(prctl(PR_SET_NAME, (long unsigned int) "bq.fifo", 0, 0, 0)))
		logger->Error("Set name FAILED! (Error: %s)\n", strerror(errno));

	// Setup the RTLib UID
	SetChannelThreadID(gettid(), name);
	logger->Debug("ChannelTrd [PID: %d] CREATED", channel_thread_pid);
	// Notifying the thread has beed started
	trdStatus_cv.notify_one();

	// Waiting for channel setup to be completed
	if (! running)
		trdStatus_cv.wait(trdStatus_ul);

	logger->Debug("ChannelTrd [PID: %d] START", channel_thread_pid);
	while (! done)
		ChannelFetch();
	logger->Debug("ChannelTrd [PID: %d] END", channel_thread_pid);
}

#define WAIT_RPC_RESP \
	chResp.result = RTLIB_BBQUE_CHANNEL_TIMEOUT; \
	chResp_cv.wait_for(chCommand_ul, \
			   std::chrono::milliseconds(BBQUE_RPC_TIMEOUT)); \
	if (chResp.result == RTLIB_BBQUE_CHANNEL_TIMEOUT) {\
		logger->Warn("RTLIB response TIMEOUT"); \
	}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::ChannelPair(const char * name)
{
	UNUSED(name);
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_APP_PAIR_t rf_APP_PAIR = {
		{
			FIFO_PKT_SIZE(APP_PAIR),
			FIFO_PYL_OFFSET(APP_PAIR),
			RPC_APP_PAIR
		},
		"\0",
		{
			{
				RPC_APP_PAIR,
				RpcMsgToken(),
				application_pid,
				0
			},
			BBQUE_RPC_FIFO_MAJOR_VERSION,
			BBQUE_RPC_FIFO_MINOR_VERSION,
			"\0"
		}
	};
	::strncpy(rf_APP_PAIR.rpc_fifo, app_fifo_filename, BBQUE_FIFO_NAME_LENGTH);
	::strncpy(rf_APP_PAIR.pyl.app_name, application_name, RTLIB_APP_NAME_LENGTH);
	logger->Debug("ChannelPair: pairing FIFO channels [app_name: %s, app_fifo: %s]",
		rf_APP_PAIR.pyl.app_name,
		rf_APP_PAIR.rpc_fifo);
	// Sending RPC Request
	RPC_FIFO_SEND(APP_PAIR);
	logger->Debug("ChannelPair: waiting for daemon response...");
	WAIT_RPC_RESP;
	logger->Debug("ChannelPair: daemon response: %d", chResp.result);
	return (RTLIB_ExitCode_t) chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::ChannelSetup()
{
	int error;
	logger->Debug("ChannelSetup: initialization...");

	// Opening server FIFO
	logger->Debug("ChannelSetup: opening daemon FIFO [%s]...", bbque_fifo_path.c_str());
	server_fifo_fd = ::open(bbque_fifo_path.c_str(), O_WRONLY | O_NONBLOCK);
	if (server_fifo_fd < 0) {
		logger->Error("ChannelSetup: opening daemon FIFO [%s] failed (error %d: %s)",
			bbque_fifo_path.c_str(), errno, strerror(errno));
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}
	logger->Debug("ChannelSetup: daemon FIFO open");

	// Setting up application FIFO complete path and
	// creating the client side pipe
	app_fifo_path += app_fifo_filename;
	logger->Debug("ChannelSetup: creating application FIFO [%s]...", app_fifo_path.c_str());
	error = ::mkfifo(app_fifo_path.c_str(), 0644);
	if (error) {
		logger->Error("ChannelSetup: creating application FIFO [%s] failed",
			app_fifo_path.c_str());
		::close(server_fifo_fd);
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}
	logger->Debug("ChannelSetup: application FIFO created");

	// Opening the client side pipe
	// NOTE: this is opened R/W to keep it opened even if server
	// should disconnect
	logger->Debug("ChannelSetup: opening application FIFO (R/W)...");
	client_fifo_fd = ::open(app_fifo_path.c_str(), O_RDWR);
	if (client_fifo_fd < 0) {
		logger->Error("ChannelSetup: opening application FIFO [%s] failed",
			app_fifo_path.c_str());
		::unlink(app_fifo_path.c_str());
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}
	logger->Debug("ChannelSetup: application FIFO open");

	// Ensuring the FIFO is R/W to everyone
	int client_fifo_perm = S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH;
	logger->Debug("ChannelSetup: setting application FIFO permissions [%d]...",
		client_fifo_perm);
	if (fchmod(client_fifo_fd, client_fifo_perm)) {
		logger->Error("FAILED setting permissions on FIFO [%s] (Error %d: %s)",
			app_fifo_path.c_str(), errno, strerror(errno));
		::unlink(app_fifo_path.c_str());
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}
	logger->Debug("ChannelSetup: FIFO permissions updated");

	return RTLIB_OK;

}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_Init(const char * name)
{
	std::unique_lock<std::mutex> trdStatus_ul(trdStatus_mtx);

	// Starting the communication thread
	logger->Debug("_Init: spawning channel thread...");
	done = false;
	running = false;
	ChTrd = std::thread(&BbqueRPC_FIFO_Client::ChannelTrd, this, name);
	trdStatus_cv.wait(trdStatus_ul);

	// Setting up application FIFO filename
	//	if (restore_pid != 0) {
	//		snprintf(app_fifo_filename, BBQUE_FIFO_NAME_LENGTH,
	//		         "bbque_%05d_%s", restore_pid, name);
	//	}
	snprintf(app_fifo_filename, BBQUE_FIFO_NAME_LENGTH,
		"%05d_%s", application_pid, application_name);
	logger->Info("_Init: application fifo = %s", app_fifo_filename);

	// Setting up the communication channel
	RTLIB_ExitCode_t result = ChannelSetup();
	if (result != RTLIB_OK)
		return result;

	// Start the reception thread
	logger->Debug("_Init: starting channel thread...");
	running = true;
	trdStatus_cv.notify_one();
	trdStatus_ul.unlock();

	// Pairing channel with server
	result = ChannelPair(application_name);
	if (result != RTLIB_OK) {
		::unlink(app_fifo_path.c_str());
		::close(server_fifo_fd);
		return result;
	}

	return RTLIB_OK;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_Register(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_EXC_REGISTER_t rf_EXC_REGISTER = {
		{
			FIFO_PKT_SIZE(EXC_REGISTER),
			FIFO_PYL_OFFSET(EXC_REGISTER),
			RPC_EXC_REGISTER
		},
		{
			{
				RPC_EXC_REGISTER,
				RpcMsgToken(),
				application_pid,
				prec->id
			},
			"\0",
			"\0",
			RTLIB_LANG_UNDEF
		}
	};
	// EXC and Recipe name: we need a null-termination character to
	// properly separate two char[] fields
	memset(rf_EXC_REGISTER.pyl.exc_name, '\0', RTLIB_EXC_NAME_LENGTH);
	strncpy(rf_EXC_REGISTER.pyl.exc_name, prec->name.c_str(),
		RTLIB_EXC_NAME_LENGTH - 1);
	memset(rf_EXC_REGISTER.pyl.recipe, '\0', RTLIB_EXC_NAME_LENGTH);
	strncpy(rf_EXC_REGISTER.pyl.recipe, prec->parameters.recipe,
		RTLIB_EXC_NAME_LENGTH - 1);
	rf_EXC_REGISTER.pyl.lang = prec->parameters.language;
	logger->Debug("_Register: EXC [%d:%d:%s:%d]...",
		rf_EXC_REGISTER.pyl.hdr.app_pid,
		rf_EXC_REGISTER.pyl.hdr.exc_id,
		rf_EXC_REGISTER.pyl.exc_name,
		rf_EXC_REGISTER.pyl.lang);
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_REGISTER);
	logger->Debug("_Register: waiting for daemon response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_Unregister(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_EXC_UNREGISTER_t rf_EXC_UNREGISTER = {
		{
			FIFO_PKT_SIZE(EXC_UNREGISTER),
			FIFO_PYL_OFFSET(EXC_UNREGISTER),
			RPC_EXC_UNREGISTER
		},
		{
			{
				RPC_EXC_UNREGISTER,
				RpcMsgToken(),
				application_pid,
				prec->id
			},
			"\0"
		}
	};
	::strncpy(rf_EXC_UNREGISTER.pyl.exc_name, prec->name.c_str(),
		RTLIB_EXC_NAME_LENGTH);
	logger->Debug("_Unregister: EXC [%d:%d:%s]...",
		rf_EXC_UNREGISTER.pyl.hdr.app_pid,
		rf_EXC_UNREGISTER.pyl.hdr.exc_id,
		rf_EXC_UNREGISTER.pyl.exc_name);
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_UNREGISTER);
	logger->Debug("_Unregister: waiting for daemon response....");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_Enable(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_EXC_START_t rf_EXC_START = {
		{
			FIFO_PKT_SIZE(EXC_START),
			FIFO_PYL_OFFSET(EXC_START),
			RPC_EXC_START
		},
		{
			{
				RPC_EXC_START,
				RpcMsgToken(),
				application_pid, //channel_thread_pid,
				prec->id
			},
		}
	};
	logger->Debug("_Enable: EXC [%d:%d]...",
		rf_EXC_START.pyl.hdr.app_pid,
		rf_EXC_START.pyl.hdr.exc_id);
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_START);
	logger->Debug("_Enable: waiting for daemon response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_Disable(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_EXC_STOP_t rf_EXC_STOP = {
		{
			FIFO_PKT_SIZE(EXC_STOP),
			FIFO_PYL_OFFSET(EXC_STOP),
			RPC_EXC_STOP
		},
		{
			{
				RPC_EXC_STOP,
				RpcMsgToken(),
				application_pid, //channel_thread_pid,
				prec->id
			},
		}
	};
	logger->Debug("_Disable: EXC [%d:%d]...",
		rf_EXC_STOP.pyl.hdr.app_pid,
		rf_EXC_STOP.pyl.hdr.exc_id);
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_STOP);
	logger->Debug("_Disable: waiting for daemon response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_Set(pRegisteredEXC_t prec,
					    RTLIB_Constraint_t * constraints,
					    uint8_t count)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);

	// Here the message is dynamically allocate to make room for a variable
	// number of constraints...
	rpc_fifo_EXC_SET_t * prf_EXC_SET;
	size_t msg_size;

	// At least 1 constraint it is expected
	assert(count);

	// Allocate the buffer to hold all the contraints
	msg_size = FIFO_PKT_SIZE(EXC_SET) +
		((count - 1) * sizeof (RTLIB_Constraint_t));
	prf_EXC_SET = (rpc_fifo_EXC_SET_t *)::malloc(msg_size);

	// Init FIFO header
	prf_EXC_SET->hdr.fifo_msg_size = msg_size;
	prf_EXC_SET->hdr.rpc_msg_offset = FIFO_PYL_OFFSET(EXC_SET);
	prf_EXC_SET->hdr.rpc_msg_type = RPC_EXC_SET;

	// Init RPC header
	prf_EXC_SET->pyl.hdr.typ = RPC_EXC_SET;
	prf_EXC_SET->pyl.hdr.token = RpcMsgToken();
	prf_EXC_SET->pyl.hdr.app_pid = application_pid; //channel_thread_pid;
	prf_EXC_SET->pyl.hdr.exc_id = prec->id;
	logger->Debug("_Set: Copying [%d] constraints using buffer @%p "
		"of [%" PRIu64 "] Bytes...",
		count, (void *) & (prf_EXC_SET->pyl.constraints),
		(count) * sizeof (RTLIB_Constraint_t));

	// Init RPC header
	prf_EXC_SET->pyl.count = count;
	::memcpy(&(prf_EXC_SET->pyl.constraints), constraints,
		(count) * sizeof (RTLIB_Constraint_t));

	// Sending RPC Request
	volatile rpc_fifo_EXC_SET_t & rf_EXC_SET = (*prf_EXC_SET);
	logger->Debug("_Set: Set [%d] constraints on EXC [%d:%d]...",
		count,
		rf_EXC_SET.pyl.hdr.app_pid,
		rf_EXC_SET.pyl.hdr.exc_id);
	RPC_FIFO_SEND_SIZE(EXC_SET, msg_size);

	// Clean-up the FIFO message
	::free(prf_EXC_SET);
	logger->Debug("_Set: Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_Clear(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_EXC_CLEAR_t rf_EXC_CLEAR = {
		{
			FIFO_PKT_SIZE(EXC_CLEAR),
			FIFO_PYL_OFFSET(EXC_CLEAR),
			RPC_EXC_CLEAR
		},
		{
			{
				RPC_EXC_CLEAR,
				RpcMsgToken(),
				application_pid,
				prec->id
			},
		}
	};
	logger->Debug("_Clear: Remove constraints for EXC [%d:%d]...",
		rf_EXC_CLEAR.pyl.hdr.app_pid,
		rf_EXC_CLEAR.pyl.hdr.exc_id);
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_CLEAR);
	logger->Debug("_Clear: Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_RTNotify(pRegisteredEXC_t prec,
						 int cps_ggap_perc,
						 int cpu_usage,
						 int cycle_time_ms,
						 int cycles_count)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_EXC_RTNOTIFY_t rf_EXC_RTNOTIFY = {
		{
			FIFO_PKT_SIZE(EXC_RTNOTIFY),
			FIFO_PYL_OFFSET(EXC_RTNOTIFY),
			RPC_EXC_RTNOTIFY
		},
		{
			{
				RPC_EXC_RTNOTIFY,
				RpcMsgToken(),
				application_pid,
				prec->id
			},
			cps_ggap_perc,
			cpu_usage,
			cycle_time_ms,
			cycles_count
		}
	};
	logger->Debug("_RTNotify: Set Goal-Gap for EXC [%d:%d]...",
		rf_EXC_RTNOTIFY.pyl.hdr.app_pid,
		rf_EXC_RTNOTIFY.pyl.hdr.exc_id);

	// Sending RPC Request
	if (! isSyncMode(prec))
		RPC_FIFO_SEND(EXC_RTNOTIFY);

	logger->Debug("_RTNotify: Waiting BBQUE response...");
	return RTLIB_OK;
	//WAIT_RPC_RESP;
	//return (RTLIB_ExitCode_t)chResp.result;
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_ScheduleRequest(pRegisteredEXC_t prec)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_EXC_SCHEDULE_t rf_EXC_SCHEDULE = {
		{
			FIFO_PKT_SIZE(EXC_SCHEDULE),
			FIFO_PYL_OFFSET(EXC_SCHEDULE),
			RPC_EXC_SCHEDULE
		},
		{
			{
				RPC_EXC_SCHEDULE,
				RpcMsgToken(),
				application_pid,
				prec->id
			},
		}
	};
	logger->Debug("_ScheduleRequest: Schedule request for EXC [%d:%d]...",
		rf_EXC_SCHEDULE.pyl.hdr.app_pid,
		rf_EXC_SCHEDULE.pyl.hdr.exc_id);
	// Sending RPC Request
	RPC_FIFO_SEND(EXC_SCHEDULE);
	logger->Debug("_ScheduleRequest: Waiting BBQUE response...");
	WAIT_RPC_RESP;
	return (RTLIB_ExitCode_t) chResp.result;
}

void BbqueRPC_FIFO_Client::_Exit()
{
	ChannelRelease();
}

/******************************************************************************
 * Synchronization Protocol Messages - PreChange
 ******************************************************************************/

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_SyncpPreChangeResp(rpc_msg_token_t token,
							   pRegisteredEXC_t prec,
							   uint32_t syncLatency)
{
	rpc_fifo_BBQ_SYNCP_PRECHANGE_RESP_t rf_BBQ_SYNCP_PRECHANGE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_SYNCP_PRECHANGE_RESP),
			FIFO_PYL_OFFSET(BBQ_SYNCP_PRECHANGE_RESP),
			RPC_BBQ_RESP
		},
		{
			{
				RPC_BBQ_RESP,
				token,
				application_pid,
				prec->id
			},
			syncLatency,
			RTLIB_OK
		}
	};
	logger->Debug("_SyncpPreChangeResp: EXC [%d:%d] latency [%d]...",
		rf_BBQ_SYNCP_PRECHANGE_RESP.pyl.hdr.app_pid,
		rf_BBQ_SYNCP_PRECHANGE_RESP.pyl.hdr.exc_id,
		rf_BBQ_SYNCP_PRECHANGE_RESP.pyl.syncLatency);
	// Sending RPC Request
	RPC_FIFO_SEND(BBQ_SYNCP_PRECHANGE_RESP);
	return RTLIB_OK;
}

void BbqueRPC_FIFO_Client::RpcBbqSyncpPreChange()
{
	rpc_msg_BBQ_SYNCP_PRECHANGE_t msg;
	size_t bytes;
	// Read response RPC header
	bytes = ::read(client_fifo_fd, (void *) &msg,
		RPC_PKT_SIZE(BBQ_SYNCP_PRECHANGE));

	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpPreChange: FAILED read from [%s] (Error %d: %s)",
			app_fifo_path.c_str(), errno, strerror(errno));
		chResp.result = RTLIB_BBQUE_CHANNEL_READ_FAILED;
	}

	std::vector<rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t> messages;

	for (uint_fast16_t i = 0; i < msg.nr_sys; i ++) {
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
		rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t msg_sys;
		bytes = ::read(client_fifo_fd, (void *) &msg_sys,
			RPC_PKT_SIZE(BBQ_SYNCP_PRECHANGE_SYSTEM));

		if (bytes <= 0) {
			logger->Error("RpcBbqSyncpPreChange: FAILED read from [%s] (Error %d: %s)",
				app_fifo_path.c_str(), errno, strerror(errno));
			chResp.result = RTLIB_BBQUE_CHANNEL_READ_FAILED;
		}

		messages.push_back(msg_sys);
	}

	// Notify the Pre-Change
	SyncP_PreChangeNotify(msg, messages);
}

/******************************************************************************
 * Synchronization Protocol Messages - SyncChange
 ******************************************************************************/

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_SyncpSyncChangeResp(rpc_msg_token_t token,
							    pRegisteredEXC_t prec,
							    RTLIB_ExitCode_t sync)
{
	rpc_fifo_BBQ_SYNCP_SYNCCHANGE_RESP_t rf_BBQ_SYNCP_SYNCCHANGE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_SYNCP_SYNCCHANGE_RESP),
			FIFO_PYL_OFFSET(BBQ_SYNCP_SYNCCHANGE_RESP),
			RPC_BBQ_RESP
		},
		{
			{
				RPC_BBQ_RESP,
				token,
				application_pid,
				prec->id
			},
			(uint8_t) sync
		}
	};
	// Check that the ExitCode can be represented by the response message
	assert(sync < 256);
	logger->Debug("_SyncpSyncChangeResp: response EXC [%d:%d]...",
		rf_BBQ_SYNCP_SYNCCHANGE_RESP.pyl.hdr.app_pid,
		rf_BBQ_SYNCP_SYNCCHANGE_RESP.pyl.hdr.exc_id);
	// Sending RPC Request
	RPC_FIFO_SEND(BBQ_SYNCP_SYNCCHANGE_RESP);
	return RTLIB_OK;
}

void BbqueRPC_FIFO_Client::RpcBbqSyncpSyncChange()
{
	rpc_msg_BBQ_SYNCP_SYNCCHANGE_t msg;
	size_t bytes;
	// Read response RPC header
	bytes = ::read(client_fifo_fd, (void *) &msg,
		RPC_PKT_SIZE(BBQ_SYNCP_SYNCCHANGE));

	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpSyncChange: FAILED read from [%s] (Error %d: %s)",
			app_fifo_path.c_str(), errno, strerror(errno));
		chResp.result = RTLIB_BBQUE_CHANNEL_READ_FAILED;
	}

	// Notify the Sync-Change
	SyncP_SyncChangeNotify(msg);
}

/******************************************************************************
 * Synchronization Protocol Messages - SyncChange
 ******************************************************************************/

void BbqueRPC_FIFO_Client::RpcBbqSyncpDoChange()
{
	rpc_msg_BBQ_SYNCP_DOCHANGE_t msg;
	size_t bytes;
	// Read response RPC header
	bytes = ::read(client_fifo_fd, (void *) &msg,
		RPC_PKT_SIZE(BBQ_SYNCP_DOCHANGE));

	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpDoChange: FAILED read from [%s] (Error %d: %s)",
			app_fifo_path.c_str(), errno, strerror(errno));
		chResp.result = RTLIB_BBQUE_CHANNEL_READ_FAILED;
	}

	// Notify the Sync-Change
	SyncP_DoChangeNotify(msg);
}

/******************************************************************************
 * Synchronization Protocol Messages - PostChange
 ******************************************************************************/

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_SyncpPostChangeResp(rpc_msg_token_t token,
							    pRegisteredEXC_t prec,
							    RTLIB_ExitCode_t result)
{
	rpc_fifo_BBQ_SYNCP_POSTCHANGE_RESP_t rf_BBQ_SYNCP_POSTCHANGE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_SYNCP_POSTCHANGE_RESP),
			FIFO_PYL_OFFSET(BBQ_SYNCP_POSTCHANGE_RESP),
			RPC_BBQ_RESP
		},
		{
			{
				RPC_BBQ_RESP,
				token,
				application_pid,
				prec->id
			},
			(uint8_t) result
		}
	};
	// Check that the ExitCode can be represented by the response message
	assert(result < 256);
	logger->Debug("_SyncpPostChangeResp: response EXC [%d:%d]...",
		rf_BBQ_SYNCP_POSTCHANGE_RESP.pyl.hdr.app_pid,
		rf_BBQ_SYNCP_POSTCHANGE_RESP.pyl.hdr.exc_id);
	// Sending RPC Request
	RPC_FIFO_SEND(BBQ_SYNCP_POSTCHANGE_RESP);
	return RTLIB_OK;
}

void BbqueRPC_FIFO_Client::RpcBbqSyncpPostChange()
{
	rpc_msg_BBQ_SYNCP_POSTCHANGE_t msg;
	size_t bytes;
	// Read response RPC header
	bytes = ::read(client_fifo_fd, (void *) &msg,
		RPC_PKT_SIZE(BBQ_SYNCP_POSTCHANGE));

	if (bytes <= 0) {
		logger->Error("RpcBbqSyncpPostChange: FAILED read from [%s] (Error %d: %s)",
			app_fifo_path.c_str(), errno, strerror(errno));
		chResp.result = RTLIB_BBQUE_CHANNEL_READ_FAILED;
	}

	// Notify the Sync-Change
	SyncP_PostChangeNotify(msg);
}

/*******************************************************************************
 * Runtime profiling
 ******************************************************************************/

void BbqueRPC_FIFO_Client::RpcBbqGetRuntimeProfile()
{
	rpc_msg_BBQ_GET_PROFILE_t msg;
	size_t bytes;
	// Read RPC request
	bytes = ::read(client_fifo_fd, (void *) &msg,
		RPC_PKT_SIZE(BBQ_GET_PROFILE));

	if (bytes <= 0) {
		logger->Error("RpcBbqGetRuntimeProfile: FAILED read from [%s] (Error %d: %s)",
			app_fifo_path.c_str(), errno, strerror(errno));
		chResp.result = RTLIB_BBQUE_CHANNEL_READ_FAILED;
	}

	// Get runtime profile
	GetRuntimeProfile(msg);
}

RTLIB_ExitCode_t BbqueRPC_FIFO_Client::_GetRuntimeProfileResp(rpc_msg_token_t token,
							      pRegisteredEXC_t prec,
							      uint32_t exc_time,
							      uint32_t mem_time)
{
	std::unique_lock<std::mutex> chCommand_ul(chCommand_mtx);
	rpc_fifo_BBQ_GET_PROFILE_RESP_t rf_BBQ_GET_PROFILE_RESP = {
		{
			FIFO_PKT_SIZE(BBQ_GET_PROFILE_RESP),
			FIFO_PYL_OFFSET(BBQ_GET_PROFILE_RESP),
			RPC_BBQ_RESP
		},
		{
			{
				RPC_BBQ_RESP,
				token,
				application_pid,
				prec->id
			},
			exc_time,
			mem_time
		}
	};
	// Sending RPC response
	logger->Debug("_GetRuntimeProfileResp: Setting runtime profile info for EXC [%d:%d]...",
		rf_BBQ_GET_PROFILE_RESP.pyl.hdr.app_pid,
		rf_BBQ_GET_PROFILE_RESP.pyl.hdr.exc_id);
	RPC_FIFO_SEND(BBQ_GET_PROFILE_RESP);
	return (RTLIB_ExitCode_t) chResp.result;
}

} // namespace rtlib

} // namespace bbque
