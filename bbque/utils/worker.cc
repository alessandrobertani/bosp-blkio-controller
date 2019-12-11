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

#include "bbque/utils/worker.h"

#include "bbque/configuration_manager.h"
#include "bbque/resource_manager.h"
#include "bbque/utils/utility.h"

#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <csignal>

using bbque::ResourceManager;
namespace bu = bbque::utils;

#define WORKER_NAMESPACE "bq.wk"
#define MODULE_NAMESPACE WORKER_NAMESPACE

namespace bbque
{
namespace utils
{

Worker::Worker() :
	name("undef"),
	done(false),
	worker_tid(0)
{
}

Worker::~Worker()
{
}

int Worker::Setup(std::string const & name, std::string const & logname)
{

	//---------- Setup the Worker name
	this->name = name;

	//---------- Get a logger module
	logger = Logger::GetLogger(logname.c_str());
	assert(logger);

	logger->Debug("Worker[%s]: Setup completed", name.c_str());
	return 0;

}

void Worker::_Task()
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);

	// Set the module name
	logger->Debug("Worker[%s]: Initialization...", name.c_str());
	if (prctl(PR_SET_NAME, (long unsigned int) name.c_str(), 0, 0, 0) != 0) {
		logger->Error("Worker[%s]: Set name FAILED! (Error: %s)\n",
		              name.c_str(), strerror(errno));
	}

	// get the thread ID for further management
	worker_tid = gettid();
	worker_status_cv.notify_one();

	// Registering to the ResourceManager
	ResourceManager::Register(name, (Worker*)this);
	logger->Info("Worker[%s]: Registered", name.c_str());
	worker_status_ul.unlock();

	// Run the user defined task
	Task();
}

void Worker::Start()
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);

	// Use default setup if not defined by derived class
	if (logger == NULL)
		Setup(WORKER_NAMESPACE ".undef", WORKER_NAMESPACE);

	logger->Debug("Worker[%s]: Starting...", name.c_str());

	// Spawn the Enqueuing thread
	worker_thd = std::thread(&Worker::_Task, this);

	// Detaching workers, indeed we don't need to sync on termination.
	// All workers are expected to terminate mainly at BBQ termination
	// time, moreover, generally we don't expect to sync with a worker
	// before going on with tome other activity.
	// Eventually, this kind of synchronization could be obtained using
	// conditional variables.
	worker_thd.detach();
	worker_status_cv.wait(worker_status_ul);
}


void Worker::Notify()
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	logger->Debug("Worker[%s]: Notifying...", name.c_str());
	worker_status_cv.notify_all();
}

void Worker::Terminate()
{
	_PreTerminate();

	logger->Debug("Worker[%s]: Terminating...", name.c_str());

	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	if (done == true) {
		logger->Warn("Worker[%s]: Already terminated", name.c_str());
		return;
	}
	done = true;

	// Notify to wake up the worker thread
	worker_status_cv.notify_all();

	// Send signal to worker
	assert(worker_tid != 0);
	::kill(worker_tid, SIGUSR1);

	_PostTerminate();

	// Unregistering Worker from ResourceManager
	ResourceManager::Unregister(name);
	logger->Info("Worker[%s]: Unregistered", name.c_str());
}

bool Worker::IsRunning()
{
	return !done;
}

bool Worker::Wait()
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	logger->Debug("Worker[%s]: Waiting...", name.c_str());
	worker_status_cv.wait(worker_status_ul);
	return !done;
}

void Worker::_PreTerminate()
{
	logger->Debug("Worker[%s]: Pre-termination", name.c_str());
}

void Worker::_PostTerminate()
{
	logger->Debug("Worker[%s]: Post-termination", name.c_str());
}

} /*utils */

} /* bbque */
