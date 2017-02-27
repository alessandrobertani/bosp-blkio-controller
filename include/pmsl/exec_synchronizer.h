/*
 * Copyright (C) 2017  Politecnico di Milano
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

#ifndef BBQUE_EXC_SYNCHRONIZER_H_
#define BBQUE_EXC_SYNCHRONIZER_H_

#include <bitset>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>

#include "bbque/bbque_exc.h"
#include "bbque/tg/task_graph.h"

#define BBQUE_TG_SERIAL_FILE "/tmp/tg_"


namespace bbque {

class ExecutionSynchronizer: public bbque::rtlib::BbqueEXC {

public:

	enum class ExitCode {
		SUCCESS,
		ERR_TASK_ID,
		ERR_TASK_GRAPH_NOT_VALID,
		ERR_TASKS_IN_EXECUTION
	};

	struct EventSync {
		std::mutex mx;
		std::condition_variable cv;
		uint32_t id;
		bool occurred = false;

		EventSync(uint32_t _id): id(_id){}
	};

	ExecutionSynchronizer(
		std::string const & name,
		std::string const & recipe,
		RTLIB_Services_t * rtlib);

	ExecutionSynchronizer(
		std::string const & name,
		std::string const & recipe,
		RTLIB_Services_t * rtlib,
		std::shared_ptr<TaskGraph> tg);

	virtual ~ExecutionSynchronizer() {
		events.clear();
	}


	ExitCode SetTaskGraph(std::shared_ptr<TaskGraph> tg) noexcept;

	inline std::shared_ptr<TaskGraph> GetTaskGraph() noexcept {
		return task_graph;
	}

	ExitCode StartTask(uint32_t task_id) noexcept;

	ExitCode StartTasks(std::list<uint32_t> tasks_id) noexcept;

	ExitCode StartTasksAll() noexcept;

	ExitCode StopTask(uint32_t task_id) noexcept;

	ExitCode StopTasks(std::list<uint32_t> tasks_id) noexcept;

	ExitCode StopTasksAll() noexcept;


	void NotifyEvent(uint32_t event_id) noexcept;


	void WaitForResourceAllocation() noexcept;

	inline bool IsResourceAllocationReady() noexcept {
		std::unique_lock<std::mutex> rtrm_ul(rtrm.mx);
		return rtrm.scheduled;
	}

protected:

	std::string app_name;

	std::string serial_file_path;

	std::shared_ptr<TaskGraph> task_graph;

	std::map<uint32_t, std::shared_ptr<EventSync>> events;


	/**
	 * \struct tasks
	 * \brief Status information about tasks
	 */
	struct {
		std::mutex mx;
		std::condition_variable cv;
		std::bitset<BBQUE_TASKS_MAX_NUM> start_status;
		std::queue<uint32_t> start_queue;
		std::shared_ptr<EventSync> run_sync;
	} tasks;

	/**
	 * \struct rtrm
	 * \brief Status information the actions of the resource manager
	 */
	struct {
		std::mutex mx;
		std::condition_variable cv;
		bool scheduled;
	} rtrm;


	bool CheckTaskGraph() noexcept;

	void SendTaskGraphToRM();

	void RecvTaskGraphFromRM();

	void NotifyResourceAllocation() noexcept;

	void StartTaskControl(uint32_t task_id) noexcept;


	// ----- BbqueEXC derived functions ----- //


	RTLIB_ExitCode_t onSetup() override;

	RTLIB_ExitCode_t onConfigure(int8_t awm_id) override;

	RTLIB_ExitCode_t onRun() override;

	RTLIB_ExitCode_t onMonitor() override;

	RTLIB_ExitCode_t onRelease() override;


	// --------------------------------------------------------------- //

	inline void enqueue_task(TaskPtr_t t) {
		if (tasks.is_stopped.test(t->Id())) {
			tasks.start_queue.push(t->Id());
			tasks.is_stopped.reset(t->Id());
			tasks.is_running[t->Id()] = true;
		}
	}

	inline void dequeue_task(TaskPtr_t t) {
		if (!tasks.is_stopped.test(t->Id())) {
			tasks.is_stopped.set(t->Id());
			tasks.is_running[t->Id()] = false;
		}
		NotifyEvent(t->Event());
	}

};


}


#endif // BBQUE_EXC_SYNCHRONIZER_H_
