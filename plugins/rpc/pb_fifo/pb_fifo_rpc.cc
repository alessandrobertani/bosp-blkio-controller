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

#include "pb_fifo_rpc.h"
#include "rpc_messages.pb.h"

#include "bbque/config.h"
#include <boost/filesystem.hpp>

#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#ifdef ANDROID
# include "bbque/android/ppoll.h"
#endif
#include <fcntl.h>
#include <csignal>
#include <stdexcept>

namespace bl = bbque::rtlib;
namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

PBFifoRPC::PBFifoRPC(std::string const & fifo_dir) :
	initialized(false),
	conf_fifo_dir(fifo_dir),
	rpc_fifo_fd(0)
{

	// Get a logger
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);

	// Ignore SIGPIPE, which will otherwise result into a BBQ termination.
	// Indeed, in case of write errors the timeouts allows BBQ to react to
	// the application not responding or disappearing.
	signal(SIGPIPE, SIG_IGN);

	logger->Debug("Built FIFO rpc object @%p", (void*)this);

}

PBFifoRPC::~PBFifoRPC()
{
	fs::path fifo_path(conf_fifo_dir);
	fifo_path /= "/" BBQUE_PUBLIC_FIFO;

	logger->Debug("FIFO RPC: cleaning up FIFO [%s]...",
	              fifo_path.string().c_str());

	::close(rpc_fifo_fd);
	// Remove the server side pipe
	::unlink(fifo_path.string().c_str());
}

//----- RPCChannelIF module interface

int PBFifoRPC::Init()
{
	int error;
	fs::path fifo_path(conf_fifo_dir);
	boost::system::error_code ec;

	if (initialized)
		return 0;

	logger->Debug("FIFO RPC: channel initialization...");

	fifo_path /= "/" BBQUE_PUBLIC_FIFO;
	logger->Debug("FIFO RPC: checking FIFO [%s]...",
	              fifo_path.string().c_str());

	// If the FIFO already exists: destroy it and rebuild a new one
	if (fs::exists(fifo_path, ec)) {
		logger->Debug("FIFO RPC: destroying old FIFO [%s]...",
		              fifo_path.string().c_str());
		error = ::unlink(fifo_path.string().c_str());
		if (error) {
			logger->Crit("FIFO RPC: cleanup old FIFO [%s] FAILED "
			             "(Error: %s)",
			             fifo_path.string().c_str(),
			             strerror(error));
			assert(error == 0);
			return -1;
		}
	}

	// Make dir (if not already present)
	logger->Debug("FIFO RPC: create dir [%s]...",
	              fifo_path.parent_path().c_str());
	fs::create_directories(fifo_path.parent_path(), ec);

	// Create the server side pipe (if not already existing)
	logger->Debug("FIFO RPC: create FIFO [%s]...",
	              fifo_path.string().c_str());
	error = ::mkfifo(fifo_path.string().c_str(), 0666);
	if (error) {
		logger->Error("FIFO RPC: RPC FIFO [%s] cration FAILED",
		              fifo_path.string().c_str());
		return -2;
	}

	// Ensuring we have a pipe
	if (fs::status(fifo_path, ec).type() != fs::fifo_file) {
		logger->Error("ERROR, RPC FIFO [%s] already in use",
		              fifo_path.string().c_str());
		return -3;
	}

	// Opening the server side pipe (R/W to keep it opened)
	logger->Debug("FIFO RPC: opening R/W...");
	rpc_fifo_fd = ::open(fifo_path.string().c_str(),
	                     O_RDWR);
	if (rpc_fifo_fd < 0) {
		logger->Error("FAILED opening RPC FIFO [%s]",
		              fifo_path.string().c_str());
		rpc_fifo_fd = 0;
		::unlink(fifo_path.string().c_str());
		return -4;
	}

	// Ensuring the FIFO is R/W to everyone
	if (fchmod(rpc_fifo_fd, S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH)) {
		logger->Error("FAILED setting permissions on RPC FIFO [%s] "
		              "(Error %d: %s)",
		              fifo_path.string().c_str(),
		              errno, strerror(errno));
		rpc_fifo_fd = 0;
		::unlink(fifo_path.string().c_str());
		return -5;
	}

	// Marking channel as already initialized
	initialized = true;

	logger->Info("FIFO RPC: channel initialization DONE");
	return 0;
}

int PBFifoRPC::Poll()
{
	struct pollfd fifo_poll;
	sigset_t sigmask;
	int ret = 0;

	// Bind to the FIFO input stream
	fifo_poll.fd = rpc_fifo_fd;
	fifo_poll.events = POLLIN;

	// Return on any signal
	sigemptyset(&sigmask);

	// Wait for data availability or signal
	logger->Debug("FIFO RPC: waiting message...");
	ret = ::ppoll(&fifo_poll, 1, NULL, &sigmask);
	if (ret < 0) {
		logger->Debug("FIFO RPC: interrupted...");
		ret = -EINTR;
	}

	return ret;
}

ssize_t PBFifoRPC::RecvMessage(rpc_msg_ptr_t & msg)
{
	bl::rpc_fifo_header_t hdr;
	void *fifo_buff_ptr;
	ssize_t result;
	ssize_t bytes;

	// Read next message FIFO header
	bytes = ::read(rpc_fifo_fd, (void*)&hdr, FIFO_PKT_SIZE(header));
	if (bytes <= 0) {
		if (bytes == EINTR)
			logger->Debug("FIFO RPC: exiting FIFO read...");
		else
			logger->Error("FIFO RPC: fifo read error");
		return bytes;
	}

	// Allocate a new message buffer
	fifo_buff_ptr = ::malloc(hdr.fifo_msg_size);
	if (!fifo_buff_ptr) {
		char c;
		logger->Error("FIFO RPC: message buffer creation FAILED");
		// Remove the remaining message from the FIFO
		for ( ; bytes < hdr.fifo_msg_size; bytes++) {
			result = ::read(rpc_fifo_fd, (void*)&c, sizeof(char));
			if (BBQUE_LIKELY(result != -1))
				continue;

			// FIXME If a read fails at that point, the FIFO queue
			// could be dirty with pending bytes of the current
			// header message... a proper clean-up procedure
			// should be activated, e.g. lookup for the next
			// HEADER.
			logger->Error("FIFO RPC: read FAILED (Error %d: %s)",
			              errno, strerror(errno));
		}
		return -errno;
	}

	// Save header into new buffer
	::memcpy(fifo_buff_ptr, &hdr, FIFO_PKT_SIZE(header));

	bool error = false;

	PB_rpc_msg pb_msg;
	void *pyl_buffer;
	if (hdr.rpc_msg_type == bl::RPC_APP_PAIR) {
		// Read the fifo name string AND THE PAYLOAD
		result = ::read(rpc_fifo_fd,
		                &(((bl::rpc_fifo_APP_PAIR_t*)fifo_buff_ptr)->rpc_fifo),
		                hdr.fifo_msg_size - FIFO_PKT_SIZE(header));
		if (BBQUE_UNLIKELY(result == -1)) {
			error = true;
		} else {
			// Save the payload in the msg reference parameter
			pyl_buffer = &(((bl::rpc_fifo_APP_PAIR_t *)fifo_buff_ptr)->pyl);
			pb_msg.ParseFromArray(pyl_buffer, hdr.pyl_size);
			bl::rpc_msg_APP_PAIR_t *struct_msg = (bl::rpc_msg_APP_PAIR_t *)pyl_buffer;
			::memset(struct_msg, 0, hdr.pyl_size);
			bl::PBMessageFactory::struct_set_header((bl::rpc_msg_header_t *)struct_msg, pb_msg.hdr());
			struct_msg->mjr_version = pb_msg.mjr_version();
			struct_msg->mnr_version = pb_msg.mnr_version();
			strncpy(struct_msg->app_name, pb_msg.app_name().c_str(), RTLIB_APP_NAME_LENGTH);
		}
	} else {
		// Read the payload
		result = ::read(rpc_fifo_fd,
		                &(((bl::rpc_fifo_GENERIC_t*)fifo_buff_ptr)->pyl),
		                hdr.fifo_msg_size - FIFO_PKT_SIZE(header));
		if (BBQUE_UNLIKELY(result == -1)) {
			error = true;
		} else {
			// Save the payload in the msg reference parameter
			pyl_buffer = &(((bl::rpc_fifo_GENERIC_t *)fifo_buff_ptr)->pyl);
			pb_msg.ParseFromArray(pyl_buffer, hdr.pyl_size);
			::memset(pyl_buffer, 0, hdr.pyl_size);
			bl::PBMessageFactory::struct_set_header((bl::rpc_msg_header_t *)pyl_buffer, pb_msg.hdr());
			if (pb_msg.hdr().typ() == bl::RPC_APP_EXIT ||
			    pb_msg.hdr().typ() == bl::RPC_EXC_CLEAR ||
			    pb_msg.hdr().typ() == bl::RPC_EXC_START ||
			    pb_msg.hdr().typ() == bl::RPC_EXC_STOP ||
			    pb_msg.hdr().typ() == bl::RPC_EXC_SCHEDULE) {
				// DO NOTHING
			} else if (pb_msg.hdr().typ() == bl::RPC_EXC_REGISTER) {
				bl::rpc_msg_EXC_REGISTER_t *struct_msg = (bl::rpc_msg_EXC_REGISTER_t *)pyl_buffer;
				strncpy(struct_msg->exc_name, pb_msg.exc_name().c_str(), RTLIB_EXC_NAME_LENGTH);
				strncpy(struct_msg->recipe, pb_msg.recipe().c_str(), RTLIB_RECIPE_NAME_LENGTH);
				struct_msg->lang = (RTLIB_ProgrammingLanguage_t)pb_msg.lang();
			} else if (pb_msg.hdr().typ() == bl::RPC_EXC_UNREGISTER) {
				bl::rpc_msg_EXC_UNREGISTER_t *struct_msg = (bl::rpc_msg_EXC_UNREGISTER_t *)pyl_buffer;
				strncpy(struct_msg->exc_name, pb_msg.exc_name().c_str(), RTLIB_EXC_NAME_LENGTH);
			} else if (pb_msg.hdr().typ() == bl::RPC_EXC_SET) {
				bl::rpc_msg_EXC_SET_t *struct_msg = (bl::rpc_msg_EXC_SET_t *)pyl_buffer;
				struct_msg->count = pb_msg.constraints_size();
				RTLIB_Constraint_t *constraints = &(struct_msg->constraints);
				for (uint8_t i = 0; i < struct_msg->count; i++) {
					constraints[i].awm = pb_msg.constraints(i).awm();
					constraints[i].operation = (RTLIB_ConstraintOperation_t)pb_msg.constraints(i).operation();
					constraints[i].type = (RTLIB_ConstraintType_t)pb_msg.constraints(i).type();
				}
			} else if (pb_msg.hdr().typ() == bl::RPC_EXC_RTNOTIFY) {
				bl::rpc_msg_EXC_RTNOTIFY_t *struct_msg = (bl::rpc_msg_EXC_RTNOTIFY_t *)pyl_buffer;
				struct_msg->cps_goal_gap = pb_msg.cps_goal_gap();
				struct_msg->cpu_usage = pb_msg.cpu_usage();
				struct_msg->cycle_time_ms = pb_msg.cycle_time_ms();
				struct_msg->cycle_count = pb_msg.cycle_count();
			} else if (pb_msg.hdr().typ() == bl::RPC_BBQ_RESP) {
				if (pb_msg.hdr().resp_type() == UNDEF) {
					bl::rpc_msg_resp_t *struct_msg = (bl::rpc_msg_resp_t *)pyl_buffer;
					struct_msg->result = pb_msg.result();
				} else if (pb_msg.hdr().resp_type() == PB_BBQ_SYNCP_PRECHANGE_RESP) {
					bl::rpc_msg_BBQ_SYNCP_PRECHANGE_RESP_t *struct_msg = (bl::rpc_msg_BBQ_SYNCP_PRECHANGE_RESP_t *)pyl_buffer;
					struct_msg->syncLatency = pb_msg.sync_latency();
					struct_msg->result = pb_msg.result();
				} else if (pb_msg.hdr().resp_type() == PB_BBQ_GET_PROFILE_RESP) {
					bl::rpc_msg_BBQ_GET_PROFILE_RESP_t *struct_msg = (bl::rpc_msg_BBQ_GET_PROFILE_RESP_t *)pyl_buffer;
					struct_msg->exec_time = pb_msg.exec_time();
					struct_msg->mem_time = pb_msg.mem_time();
				}
			} else {
				logger->Error("Unrecognized msg type %d", msg->typ);
			}
		}
	}
	msg = (rpc_msg_ptr_t)pyl_buffer;
	logger->Debug("FIFO RPC: Rx FIFO_HDR [sze: %hd, off: %hd, typ: %hd] "
	              "RPC_HDR [typ: %d, pid: %d, eid: %hd]",
	              ((bl::rpc_fifo_APP_PAIR_t*)fifo_buff_ptr)->hdr.fifo_msg_size,
	              ((bl::rpc_fifo_APP_PAIR_t*)fifo_buff_ptr)->hdr.rpc_msg_offset,
	              ((bl::rpc_fifo_APP_PAIR_t*)fifo_buff_ptr)->hdr.rpc_msg_type,
	              pb_msg.hdr().typ(),
	              pb_msg.hdr().app_pid(),
	              pb_msg.hdr().exc_id());

	if (error) {
		logger->Error("FIFO RPC: read RPC message FAILED (Error %d: %s)",
		              errno, strerror(errno));

		free(fifo_buff_ptr);
		msg = NULL;

		return -errno;
	}

	// Recovery the payload size to be returned
	bytes = hdr.fifo_msg_size - hdr.rpc_msg_offset;

	return bytes;
}

RPCChannelIF::plugin_data_t PBFifoRPC::GetPluginData(
        rpc_msg_ptr_t & msg)
{
	fifo_data_t * pd;
	fs::path fifo_path(conf_fifo_dir);
	boost::system::error_code ec;
	bl::rpc_fifo_APP_PAIR_t * hdr;
	int fd;


	// We should have the FIFO dir already on place
	assert(initialized);

	// We should also have a valid RPC message
	assert(msg->typ == bl::RPC_APP_PAIR);

	// Get a reference to FIFO header
	hdr = container_of(msg, bl::rpc_fifo_APP_PAIR_t, pyl);
	logger->Debug("FIFO RPC: plugin data initialization...");

	// Build fifo path
	fifo_path /= "/";
	fifo_path /= hdr->rpc_fifo;

	try {
		// The application should build the channel, this could be used as
		// an additional handshaking protocol and API versioning verification
		logger->Debug("FIFO RPC: checking for application FIFO [%s]...",
		              fifo_path.string().c_str());
		if (!fs::exists(fifo_path, ec)) {
			throw std::runtime_error("FIFO RPC: apps FIFO NOT FOUND");
		}

		// Ensuring we have a pipe
		if (fs::status(fifo_path, ec).type() != fs::fifo_file) {
			throw std::runtime_error("FIFO RPC: apps FIFO not valid");
		}

		// Opening the application side pipe WRITE only
		logger->Debug("FIFO RPC: opening (WR only)...");
		fd = ::open(fifo_path.string().c_str(), O_WRONLY);
		if (fd < 0) {
			logger->Error("FAILED opening application RPC FIFO [%s] (Error %d: %s)",
			              fifo_path.string().c_str(), errno, strerror(errno));
			fd = 0;
			// Debugging: abort on too many files open
			assert(errno != EMFILE);
			throw std::runtime_error("FAILED opening application RPC FIFO");
		}

		// Build a new set of plugins data
		pd = (fifo_data_t*)::malloc(sizeof(fifo_data_t));
		if (!pd) {
			::close(fd);
			throw std::runtime_error("FIFO RPC: get plugin data (malloc) FAILED");
		}

	} // try
	catch(std::runtime_error &ex) {
		logger->Error("Error trying to get plugin data RPC FIFO [%s]",
		              fifo_path.string().c_str());
		logger->Error(ex.what());
		return plugin_data_t();
	}


	::strncpy(pd->app_fifo_filename, hdr->rpc_fifo, BBQUE_FIFO_NAME_LENGTH);
	pd->app_fifo_fd = fd;

	logger->Info("FIFO RPC: [%5d:%s] channel initialization DONE",
	             pd->app_fifo_fd, hdr->rpc_fifo);

	return plugin_data_t(pd);

}

void PBFifoRPC::ReleasePluginData(plugin_data_t & pd)
{
	fifo_data_t * ppd = (fifo_data_t*)pd.get();

	assert(initialized == true);
	assert(ppd && ppd->app_fifo_fd);

	// Close the FIFO and cleanup plugin data
	::close(ppd->app_fifo_fd);

	logger->Info("FIFO RPC: [%5d:%s] channel release DONE",
	             ppd->app_fifo_fd, ppd->app_fifo_filename);

}

ssize_t PBFifoRPC::SendMessage(plugin_data_t & pd, rpc_msg_ptr_t msg,
                               size_t count)
{
	fifo_data_t * ppd = (fifo_data_t*)pd.get();
	bl::rpc_fifo_GENERIC_t *fifo_msg;
	ssize_t error;

	assert(rpc_fifo_fd);
	assert(ppd && ppd->app_fifo_fd);

	// FIXME copying the RPC message into the FIFO one is not efficient at all,
	// but this it the less intrusive patch to use a single write on the PIPE.
	// A better solution, e.g. pre-allocating a channel message, should be
	// provided by a future patch

	// Build a new message of the required type
	// NOTE all BBQ generated command have the sam FIFO layout
	fifo_msg = (bl::rpc_fifo_GENERIC_t*)::malloc(sizeof(bl::rpc_fifo_GENERIC_t));

	static uint16_t nr_sys = 0;
	if (nr_sys > 0) {
		// msg contains a rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM
		PB_rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM pb_sys;
		bl::rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t *sys = (bl::rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t *)msg;
		pb_sys.set_sys_id(sys->sys_id);
		pb_sys.set_nr_cpus(sys->nr_cpus);
		pb_sys.set_nr_procs(sys->nr_procs);
		pb_sys.set_r_proc(sys->r_proc);
		pb_sys.set_r_mem(sys->r_mem);
#ifdef CONFIG_TARGET_OPENCL
		pb_sys.set_r_gpu(sys->r_gpu);
		pb_sys.set_r_acc(sys->r_acc);
		pb_sys.set_dev(sys->dev);
#endif // CONFIG_TARGET_OPENCL
		pb_sys.SerializeToArray(fifo_msg->pyl, pb_sys.ByteSize());
		fifo_msg->hdr.pyl_size = pb_sys.ByteSize();
		nr_sys--;
	} else {
		PB_rpc_msg pb_msg;
		bl::PBMessageFactory::pb_set_header(pb_msg, msg->typ, msg->token, msg->app_pid, msg->exc_id);
		if (msg->typ == bl::RPC_BBQ_SYNCP_PRECHANGE) {
			bl::rpc_msg_BBQ_SYNCP_PRECHANGE_t *themsg = (bl::rpc_msg_BBQ_SYNCP_PRECHANGE_t *)msg;
			pb_msg.set_event(themsg->event);
			pb_msg.set_awm(themsg->awm);
#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
			pb_msg.set_cpu_ids(themsg->cpu_ids);
			pb_msg.set_cpu_ids_isolation(themsg->cpu_ids_isolation);
			pb_msg.set_mem_ids(themsg->mem_ids);
#endif // CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
			nr_sys = themsg->nr_sys;
			pb_msg.set_nr_sys(nr_sys);
		} else if (msg->typ == bl::RPC_BBQ_SYNCP_SYNCCHANGE ||
		           msg->typ == bl::RPC_BBQ_SYNCP_DOCHANGE ||
		           msg->typ == bl::RPC_BBQ_SYNCP_POSTCHANGE) {
			// DO NOTHING
		} else if (msg->typ == bl::RPC_BBQ_STOP_EXECUTION) {
			bl::rpc_msg_BBQ_STOP_t *themsg = (bl::rpc_msg_BBQ_STOP_t *)msg;
			pb_msg.mutable_timeout()->set_seconds(themsg->timeout.tv_sec);
			pb_msg.mutable_timeout()->set_nanos(themsg->timeout.tv_nsec);
		} else if (msg->typ == bl::RPC_BBQ_GET_PROFILE) {
			pb_msg.set_is_ocl(((bl::rpc_msg_BBQ_GET_PROFILE_t *)msg)->is_ocl);
		} else if (msg->typ == bl::RPC_APP_RESP || msg->typ == bl::RPC_EXC_RESP) {
			pb_msg.set_result(((bl::rpc_msg_resp_t *)msg)->result);
		} else {
			logger->Error("Unrecognized msg type %d", msg->typ);
		}
		pb_msg.SerializeToArray(fifo_msg->pyl, pb_msg.ByteSize());
		fifo_msg->hdr.pyl_size = pb_msg.ByteSize();
	}

	// Copy the RPC message into the FIFO msg
	//::memcpy(&(fifo_msg->pyl), msg, count);

	logger->Debug("FIFO RPC: TX [typ: %d, sze: %d] "
	              "using app channel [%d:%s]...",
	              msg->typ, count,
	              ppd->app_fifo_fd,
	              ppd->app_fifo_filename);

	// Send the RPC FIFO message
	fifo_msg->hdr.fifo_msg_size = sizeof(bl::rpc_fifo_GENERIC_t);
	fifo_msg->hdr.rpc_msg_offset = offsetof(bl::rpc_fifo_GENERIC_t, pyl);
	fifo_msg->hdr.rpc_msg_type = msg->typ;

	error = ::write(ppd->app_fifo_fd, fifo_msg, fifo_msg->hdr.fifo_msg_size);
	if (error == -1) {
		logger->Error("FIFO RPC: send massage (header) FAILED (Error %d: %s)",
		              errno, strerror(errno));
		return -errno;
	}

	return fifo_msg->hdr.fifo_msg_size;
}

void PBFifoRPC::FreeMessage(rpc_msg_ptr_t & msg)
{
	void* fifo_msg;

	// Recover the beginning of the FIFO message
	switch (msg->typ) {
	case bl::RPC_APP_PAIR:
		fifo_msg = (void*)container_of(msg, bl::rpc_fifo_APP_PAIR_t, pyl);
		break;
	default:
		fifo_msg = (void*)container_of(msg, bl::rpc_fifo_GENERIC_t, pyl);
		break;
	}

	// Releaseing the FIFO message buffer
	::free(fifo_msg);
}

//----- static plugin interface

void * PBFifoRPC::Create(PF_ObjectParams *params)
{
	static std::string conf_fifo_dir;

	// Declare the supported options
	po::options_description fifo_rpc_opts_desc("FIFO RPC Options");
	fifo_rpc_opts_desc.add_options()
	(MODULE_NAMESPACE".dir", po::value<std::string>
	 (&conf_fifo_dir)->default_value(BBQUE_PATH_VAR),
	 "path of the FIFO dir")
	;
	static po::variables_map fifo_rpc_opts_value;

	// Get configuration params
	PF_Service_ConfDataIn data_in;
	data_in.opts_desc = &fifo_rpc_opts_desc;
	PF_Service_ConfDataOut data_out;
	data_out.opts_value = &fifo_rpc_opts_value;
	PF_ServiceData sd;
	sd.id = MODULE_NAMESPACE;
	sd.request = &data_in;
	sd.response = &data_out;

	int32_t response = params->
	                   platform_services->InvokeService(PF_SERVICE_CONF_DATA, sd);
	if (response != PF_SERVICE_DONE)
		return NULL;

	if (daemonized)
		syslog(LOG_INFO, "Using RPC FIFOs dir [%s]",
		       conf_fifo_dir.c_str());
	else
		fprintf(stderr, FI("FIFO RPC: using dir [%s]\n"),
		        conf_fifo_dir.c_str());

	return new PBFifoRPC(conf_fifo_dir);

}

int32_t PBFifoRPC::Destroy(void *plugin)
{
	if (!plugin)
		return -1;
	delete (PBFifoRPC *)plugin;
	return 0;
}

} // namesapce plugins

} // namespace bque
