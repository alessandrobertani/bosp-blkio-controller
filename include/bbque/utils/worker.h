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

#ifndef BBQUE_WORKER_H_
#define BBQUE_WORKER_H_

#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "bbque/utils/logging/logger.h"

namespace bu = bbque::utils;

namespace bbque
{
namespace utils
{

/**
 * @class Worker
 * @brief A BarbequeRTRM worker thread
 * @details
 * This class provides a bare minimum support to properly initialize and track
 * BarbequeRTRM support threads. A worker is a thread which is started
 * initially and run till the resource manager is stopped. This class provides
 * the necessary support to name worker threads and properly termiante them
 * when BBQ is terminated.
 */
class Worker
{

public:

	/**
	 * @brief The worker constructor
	 */
	Worker();

	/**
	 * @brief The worker destructor
	 */
	virtual ~Worker();

	/**
	 * @brief Setup the worker before starting
	 *
	 * This method allows to setup a name for the worker and to load the
	 * specified logger configuration.
	 *
	 * This method shoudl be colled by the derived class constructor to
	 * properly initialized the worker.  Otherwise default values will be
	 * used to configure it.
	 *
	 * @param name the worker name, mostly used for logging
	 * @param logname the name of the logger, used to load the
	 * configuration
	 *
	 * @return 0 on success, -1 otherwise
	 */
	int Setup(std::string const & name, std::string const & logname);

	/**
	 * @brief Start the worker processing
	 *
	 * The code defined by the @see Task method will be executed after a
	 * call of this method.
	 */
	void Start();


	/**
	 * @brief Notify the worker
	 *
	 * A call to this method notify the @see worker_status_cv conditional
	 * variable thus, for example, returning from a blocking @see Wait
	 * call
	 */
	void Notify();

	/**
	 * @brief Stop the worker processing
	 *
	 * Call this method to end the processing of the worker. The @see done
	 * attribute is set to done and the worker thread is notified by:<br>
	 * 1. notifying the @see worker_status_cv conditional variable
	 * 2. signaling a SIGUSR1 to the worker thread
	 */
	void Terminate();

protected:

	/**
	 * @brief The logger used by the worker thread
	 */
	std::unique_ptr<bu::Logger> logger;

	/**
	 * @brief The name of the worker thread
	 */
	std::string name;

	/**
	 * @brief Set true to terminate the worker thread
	 */
	bool done;

	/**
	 * @brief The worker thread
	 */
	std::thread worker_thd;

	/**
	 * @brief The thread ID of the worker thread
	 */
	pid_t worker_tid;

	/**
	 * @brief Mutex controlling the worker execution
	 */
	std::mutex worker_status_mtx;

	/**
	 * @brief Conditional variable used to signal the worker thread
	 */
	std::condition_variable worker_status_cv;

	/**
	 * @brief The Worker main code
	 */
	virtual void Task() = 0;

	/*
	 * @brief Wait for a notification
	 *
	 * A call to this method blocks until the @see worker_status_cv
	 * conditional variable is notified. The notificaiton could be done by
	 * using the @see Notify() method.
	 *
	 * @return true if the worker should continue, false is it must
	 * terminate
	 */
	bool Wait();

protected:

	/**
	 * @brief Preparation to termination procedure
	 **/
	virtual void _PreTerminate();

	/**
	 * @brief Cleanup after termination procedure
	 **/
	virtual void _PostTerminate();

private:

	/**
	 * @brief The code common to all tasks
	 */
	void _Task();

};

} // namespace utils

} // namespace bbque

#endif // BBQUE_WORKER_H_
