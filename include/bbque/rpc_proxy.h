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

#ifndef BBQUE_RPC_PROXY_H_
#define BBQUE_RPC_PROXY_H_

#include "bbque/plugins/rpc_channel.h"
#include "bbque/utils/worker.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/metrics_collector.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

using bbque::plugins::RPCChannelIF;
using bbque::utils::MetricsCollector;
namespace bu = bbque::utils;

namespace bbque {

/**
 * @brief Queuing support to the low-level communication interface.
 *
 * This defines the a proxy class use to provide message queuing support by
 * wrapping the RPCChannelIF. This class unloads the channel modules
 * from the message queuing management, thus allowing for a simpler
 * implementaton. Meanwhile, this is class provides all the code to manage
 * message queuing and dequenung policies.
 */
class RPCProxy : public RPCChannelIF, public bu::Worker {

public:

	/**
	 * 
	 */
	static RPCProxy *GetInstance(std::string const & id);

	/**
	 * @brief Destructor
	 */
	virtual ~RPCProxy();

	/**
	 * @brief Initialize the communication channel.
	 */
	virtual int Init();


	/**
	 * @brief Block waiting for new data or signal
	 *
	 * A call to this method is blocking until new data are available for
	 * reading or the channel has been closed.
	 * @see RPCChannelIF::Poll
	 *
	 * @return Not negative numner of new data is available, negative
	 * error otherwise.
	 */
	virtual int Poll();

	/**
	 * @brief Get a pointer to the next message buffer.
	 *
	 * This methods blocks the caller until a new message is available and
	 * then return a pointer to the beginning of the message buffer and the
	 * size of the returned buffer.
	 *
	 * @note If more than one message are waiting to be processed, the message
	 * returned depends on the dequeuing rules defined by the implementation
	 * of this class.
	 */
	virtual ssize_t RecvMessage(rpc_msg_ptr_t & msg);

	/**
	 * @brief Get a pointer to plugins data.
	 *
	 * Based on the specified message buffer, the channel module could
	 * allocate and initialize a set of plugin specific data. These data are
	 * opaque to the Barbeque RTRM, but they will be passed to the plugin each
	 * time a message should be sent.
	 *
	 * This method is called only after the reception of a RPC_APP_PAIR
	 * message, which is passed to the communication channel module as a
	 * reference to map the new communication channel. A communication channel
	 * module could use these information (or its own specific information
	 * pre-pended to the specified message buffer) to setup a communication
	 * channel with the application and save all the required handler to the
	 * plugins data to be returned. These resource and correponding handler
	 * will be passed to the module each time a new message should be sent to
	 * an application.
	 *
	 * @see look at the BbqueRPC_FIFO_Server class for a usage example.
	 *
	 * @note this call is forwarded directly to the low-level channel module.
	 */
	virtual plugin_data_t GetPluginData(rpc_msg_ptr_t & msg);

	/**
	 * @brief Release plugins data.
	 *
	 * Ask the channel to release all the resources associated with the
	 * specified plugin data. This Barbeque RTRM opaque type is usually used
	 * to save the communication channel specific connection information. In
	 * this case, this method authorize the communicaiton channel module to
	 * close the corresponding connection and release all the resource.
	 *
	 * @see look at the BbqueRPC_FIFO_Server class for a usage example.
	 *
	 * @note this call is forwarded directly to the low-level channel module.
	 */
	virtual void ReleasePluginData(plugin_data_t & pd);

	/**
	 * @brief Send a message buffer to the specified application.
	 *
	 * This method blocks the caller until the specified message buffer could
	 * be accepted for delivery to the specified application.
	 *
	 * @note this call is forwarded directly to the low-level channel module.
	 */
	virtual ssize_t SendMessage(plugin_data_t & pd, rpc_msg_ptr_t msg,
								size_t count);

	/**
	 * @brief Release the specified RPC message.
	 *
	 * This methods blocks the caller until a new message is available and
	 * then return a pointer to the beginning of the message buffer and the
	 * size of the returned buffer.
	 */
	virtual void FreeMessage(rpc_msg_ptr_t & msg);

private:

	/**
	 * 
	 */
	static RPCProxy *instance;

	MetricsCollector & mc;

	static bool channelLoaded;

	/**
	 * 
	 */
	typedef std::pair<rpc_msg_ptr_t, size_t> channel_msg_t;

	/**
	 * 
	 */
	class RPCMsgCompare {
	public:
		bool operator() (const channel_msg_t & lhs,
				const channel_msg_t & rhs) const;
	};

	/**
	 * 
	 */
	std::unique_ptr<RPCChannelIF> rpc_channel;

	/**
	 * 
	 */
	std::priority_queue<channel_msg_t,
		std::vector<channel_msg_t>,
		RPCMsgCompare> msg_queue;

	/**
	 * 
	 */
	std::mutex msg_queue_mtx;

	/**
	 * 
	 */
	std::condition_variable queue_ready_cv;

	typedef enum RpcPrxMetrics {
		//----- Event counting metrics
		RP_BYTES_TX = 0,
		RP_BYTES_RX,
		RP_MSGS_TX,
		RP_MSGS_RX,
		//----- Couting statistics
		RP_RX_QUEUE,

		RP_METRICS_COUNT
	} RpcPrxMetrics_t;

	static MetricsCollector::MetricsCollection_t metrics[RP_METRICS_COUNT];
	/**
	 * 
	 */
	RPCProxy(std::string const & id);

	/**
	 * @brief Enqueue a new received message.
	 *
	 * Provides an enqueuing thread which continuously fetch messages from the
	 * low-level channel module and enqueue them to the proper queue.
	 */
	void Task();

	void SignalPoll();

};

} // namespace bbque

#endif // BBQUE_RPC_PROXY_H_
