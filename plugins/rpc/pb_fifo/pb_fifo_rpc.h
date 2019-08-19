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

#ifndef BBQUE_PLUGINS_FIFO_RPC_H_
#define BBQUE_PLUGINS_FIFO_RPC_H_

#include "bbque/rtlib/rpc/pb_fifo/rpc_pb_fifo_server.h"
#include "bbque/rtlib/rpc/pb_fifo/rpc_pb_message_factory.h"

#include "bbque/plugins/rpc_channel.h"
#include "bbque/plugins/plugin.h"
#include "bbque/utils/logging/logger.h"

#include <cstdint>

#define MODULE_NAMESPACE RPC_CHANNEL_NAMESPACE ".fif"

// These are the parameters received by the PluginManager on create calls
struct PF_ObjectParams;

namespace bu = bbque::utils;

namespace bbque { namespace plugins {

/**
 * @class PBFifoRPC
 * @brief A FIFO based implementation of the RPCChannelIF interface.
 * @details
 * This class provide a FIFO based communication channel between the Barbque
 * RTRM and the applications.
 */
class PBFifoRPC : public RPCChannelIF {

typedef struct fifo_data : ChannelData {
	/** The handler to the application FIFO */
	int app_fifo_fd;
	/** The application FIFO filename */
	char app_fifo_filename[BBQUE_FIFO_NAME_LENGTH];
} fifo_data_t;


public:

//----- static plugin interface

	/**
	 *
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 *
	 */
	static int32_t Destroy(void *);

	virtual ~PBFifoRPC();

//----- RPCChannelIF module interface

	virtual int Poll();

	virtual ssize_t RecvMessage(rpc_msg_ptr_t & msg);

	virtual plugin_data_t GetPluginData(rpc_msg_ptr_t & msg);

	virtual void ReleasePluginData(plugin_data_t & pd);

	virtual ssize_t SendMessage(plugin_data_t & pd, rpc_msg_ptr_t msg,
								size_t count);

	virtual void FreeMessage(rpc_msg_ptr_t & msg);

private:



	/**
	 * @brief System logger instance
	 */
	std::unique_ptr<bu::Logger> logger;

	/**
	 * @brief Thrue if the channel has been correctly initalized
	 */
	bool initialized;

	/**
	 * @brief The path of the directory for FIFOs creation
	 */
	std::string conf_fifo_dir;

	/**
	 * @brief The RPC server FIFO descriptor
	 */
	int rpc_fifo_fd;

	/**
	 * @brief   The plugins constructor
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 * @param
	 * @return
	 */
	PBFifoRPC(std::string const & fifo_dir);

	int Init();

};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_PLUGINS_TESTING_H_
