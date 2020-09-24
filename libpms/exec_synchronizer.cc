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

#include <fstream>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "pmsl/exec_synchronizer.h"


namespace bbque {

ExecutionSynchronizer::ExecutionSynchronizer(
		std::string const & name,
		std::string const & recipe,
		RTLIB_Services_t * rtlib):
	BbqueEXC(name, recipe, rtlib, RTLIB_LANG_TASKGRAPH),
	app_name(name) {

	app_name.resize(RTLIB_APP_NAME_LENGTH-1);
}


ExecutionSynchronizer::ExecutionSynchronizer(
		std::string const & name,
		std::string const & recipe,
		RTLIB_Services_t * rtlib,
		std::shared_ptr<TaskGraph> tg):
	BbqueEXC(name, recipe, rtlib),
	app_name(name),
	task_graph(tg) {

	app_name.resize(RTLIB_APP_NAME_LENGTH-1);
}


ExecutionSynchronizer::ExitCode ExecutionSynchronizer::SetTaskGraph(std::shared_ptr<TaskGraph> tg) {
	// Cannot change the TG while still in execution
	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	if (tasks.is_stopped.any()) {
		logger->Error("SetTaskGraph: tasks execution still in progress...");
		return ExitCode::ERR_TASKS_IN_EXECUTION;
	}

	// Task graph validation and setting
	if (!CheckTaskGraph(tg)) {
		logger->Error("SetTaskGraph: task graph not valid");
		return ExitCode::ERR_TASK_GRAPH_NOT_VALID;
	}
	task_graph = tg;
	task_graph->SetApplicationId(GetUid());

	// Set serialization file and named semaphore paths
	auto ret = SetTaskGraphPaths();
	if (ret != ExitCode::SUCCESS) {
		logger->Error("SetTaskGraph: path setting failed");
		return ret;
	}

	// Task execution status initialization
	for (auto & t_entry: task_graph->Tasks()) {
		auto & task(t_entry.second);
		tasks.is_stopped.set(task->Id());
		std::shared_ptr<RuntimeInfo> rt_info = std::make_shared<RuntimeInfo>(false);
		tasks.runtime.emplace(task->Id(), rt_info);
	}

	// Task synchronization events initialization
	for (auto & ev_entry: task_graph->Events()) {
		auto & event(ev_entry.second);
		std::shared_ptr<EventSync> ev_sync = std::make_shared<EventSync>(event->Id());
                events.emplace(event->Id(), ev_sync);
	}

	logger->Info("SetTaskGraph: task-graph successfully set");

	return ExitCode::SUCCESS;
}


ExecutionSynchronizer::ExitCode ExecutionSynchronizer::SetTaskGraphPaths() {
	try {
		std::string app_suffix(std::string(GetUniqueID_String()).substr(0, 6) + app_name);
		tg_file_path = BBQUE_TG_FILE_PREFIX + app_suffix;
		std::string tg_str(app_suffix);
		std::replace(tg_str.begin(), tg_str.end(), ':', '.');
		tg_sem_path  = "/" + tg_str;
		logger->Info("Task-graph [uid=%d] file:<%s>  sem:<%s> ", GetUniqueID(),
			tg_file_path.c_str(), tg_sem_path.c_str());

		tg_sem = sem_open(tg_sem_path.c_str(), O_CREAT, 0644, 1);
		if (tg_sem == nullptr) {
			logger->Error("Task-graph [uid=%d]: cannot create semaphore <%s> errno=%d",
				GetUniqueID(), tg_sem_path.c_str(), errno);
		}
		logger->Info("Semaphore open");
	}
	catch (std::exception & ex) {
		logger->Error("Error while creating serialization file name");
		return ExitCode::ERR_TASK_GRAPH_FILES;
	}
	return ExitCode::SUCCESS;
}

bool ExecutionSynchronizer::CheckTaskGraph(std::shared_ptr<TaskGraph> tg) noexcept {
	if (tg == nullptr) {
		logger->Error("Task graph missing");
		return false;
	}

	if (!tg->IsValid()) {
		logger->Error("Task graph not valid");
		return false;
	}
	return true;
}


void ExecutionSynchronizer::SendTaskGraphToRM() {
	sem_wait(tg_sem);
	try {
		std::ofstream ofs(tg_file_path);
		boost::archive::text_oarchive oa(ofs);
		oa << *(this->task_graph);
	}
	catch(std::exception & ex) {
		logger->Error("Task-graph serialization error: %s",
			ex.what());
	}
	sem_post(tg_sem);
	logger->Info("Task-graph sent for resource allocation");
}

void ExecutionSynchronizer::RecvTaskGraphFromRM() {
	sem_wait(tg_sem);
	try {
		std::ifstream ifs(tg_file_path);
		boost::archive::text_iarchive ia(ifs);
		ia >> *(this->task_graph);
	}
	catch(std::exception & ex) {
		logger->Error("Task-graph de-serialization error: %s",
			ex.what());
	}
	sem_post(tg_sem);
	logger->Info("Task-graph restored after resource allocation");
}


ExecutionSynchronizer::ExitCode ExecutionSynchronizer::StartTask(uint32_t task_id) noexcept {
	if (!CheckTaskGraph(task_graph))
		return ExitCode::ERR_TASK_GRAPH_NOT_VALID;

	auto task = task_graph->GetTask(task_id);
	if (task == nullptr) {
		logger->Error("StartTask: unknown task id: %d", task_id);
		return ExitCode::ERR_TASK_ID;
	}

	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	enqueue_task(task);
	tasks.cv.notify_all();
	logger->Info("StartTask: [Task %2d] launched", task_id);

	return ExitCode::SUCCESS;
}

ExecutionSynchronizer::ExitCode ExecutionSynchronizer::StartTasks(
		std::list<uint32_t> tasks_ids) noexcept {
	if (!CheckTaskGraph(task_graph))
		return ExitCode::ERR_TASK_GRAPH_NOT_VALID;

	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	for (auto task_id: tasks_ids) {
		auto task = task_graph->GetTask(task_id);
		if (task == nullptr) {
			logger->Error("StartTasks: unknown task id: %d", task_id);
			return ExitCode::ERR_TASK_ID;
		}
		enqueue_task(task);
	}
	tasks.cv.notify_all();
	logger->Info("StartTasks: Requested tasks started");
	return ExitCode::SUCCESS;
}

ExecutionSynchronizer::ExitCode ExecutionSynchronizer::StartTasksAll() noexcept {
	if (!CheckTaskGraph(task_graph))
		return ExitCode::ERR_TASK_GRAPH_NOT_VALID;

	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	for (auto t_entry: task_graph->Tasks()) {
		auto & task = t_entry.second;
		enqueue_task(task);
	}
	tasks.cv.notify_all();
	logger->Info("StartTasksAll: Tasks started");
	return ExitCode::SUCCESS;
}


ExecutionSynchronizer::ExitCode ExecutionSynchronizer::StopTask(uint32_t task_id) noexcept {
	if (!CheckTaskGraph(task_graph))
		return ExitCode::ERR_TASK_GRAPH_NOT_VALID;

	auto task = task_graph->GetTask(task_id);
	if (task == nullptr) {
		logger->Error("StopTask: unknown task id: %d", task_id);
			return ExitCode::ERR_TASK_ID;
	}

	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	dequeue_task(task);
	NotifyTaskEvents(task);
	tasks.cv.notify_all();

	logger->Info("StopTask: [Task %2d] stopped", task_id);
	return ExitCode::SUCCESS;
}

ExecutionSynchronizer::ExitCode ExecutionSynchronizer::StopTasks(
		std::list<uint32_t> tasks_ids) noexcept {
	if (!CheckTaskGraph(task_graph))
		return ExitCode::ERR_TASK_GRAPH_NOT_VALID;

	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	for (auto task_id: tasks_ids) {
		auto task = task_graph->GetTask(task_id);
		if (task == nullptr) {
			logger->Error("StopTasks: unknown task id: %d", task_id);
			return ExitCode::ERR_TASK_ID;
		}
		dequeue_task(task);
		NotifyTaskEvents(task);
	}
	tasks.cv.notify_all();
	logger->Info("StopTasks: Required tasks stopped");
	return ExitCode::SUCCESS;
}

ExecutionSynchronizer::ExitCode ExecutionSynchronizer::StopTasksAll() noexcept {
	if (!CheckTaskGraph(task_graph))
		return ExitCode::ERR_TASK_GRAPH_NOT_VALID;

	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	for (auto & t_entry: task_graph->Tasks()) {
		auto & task(t_entry.second);
		dequeue_task(task);
		NotifyTaskEvents(task);
	}
	tasks.cv.notify_all();
	logger->Info("StopTasksAll: Tasks stopped");
	return ExitCode::SUCCESS;
}


void ExecutionSynchronizer::NotifyEvent(uint32_t event_id) noexcept {
	auto evit = events.find(event_id);
	if (evit == events.end()) {
		logger->Error("[Event %2d] not registered", event_id);
		return;
	}
	auto & event(evit->second);

	std::unique_lock<std::mutex> ev_lock(event->mx);
	event->occurred = true;
	event->cv.notify_all();
	logger->Debug("[Event %2d] notified", event_id);
}


void ExecutionSynchronizer::WaitForResourceAllocation() noexcept {
	std::unique_lock<std::mutex> rtrm_lock(rtrm.mx);
	while (!rtrm.scheduled)
		rtrm.cv.wait(rtrm_lock);
}


// ---------------- Protected/private ---------------------------------------//


void ExecutionSynchronizer::NotifyResourceAllocation() noexcept {
	std::unique_lock<std::mutex> rtrm_lock(rtrm.mx);
	rtrm.scheduled = true;
	rtrm.cv.notify_all();
}

void ExecutionSynchronizer::TaskProfiler(uint32_t task_id) noexcept {

	auto task = task_graph->GetTask(task_id);
	if (task == nullptr) {
		logger->Crit("[Task %2d] missing task descriptor", task_id);
		return;
	}

	auto out_buffers = task->OutputBuffers();
	if (out_buffers.empty()) {
		logger->Error("[Task %2d] missing output buffers", task_id);
		return;
	}

	// Pick the first output buffer of the task
	auto buffer = task_graph->GetBuffer(out_buffers.front());
	if (buffer == nullptr) {
		logger->Error("[Task %2d] missing output buffer descriptor", task_id);
		return;
	}

	auto evit = events.find(buffer->Event());
	if (evit == events.end()) {
		logger->Error("[Task %2d] missing event(%d) associated to buffer %d",
			task_id, buffer->Event(), buffer->Id());
		return;
	}

	auto & event(evit->second);
	auto & ctime = tasks.runtime[task_id]->ctime;
	ctime.timer.start();
	double t_curr, t_start, t_finish;

	// Synchronize the profiling timing according to the events (write)
	// affecting the output buffer of the task
	logger->Info("[Task %2d] profiling started", task_id);
	while (tasks.runtime[task_id]->is_running) {
		std::unique_lock<std::mutex> ev_lock(event->mx);
		t_start  = ctime.timer.getElapsedTimeUs();
		event->cv.wait(ev_lock);
		t_finish = ctime.timer.getElapsedTimeUs();
		t_curr = t_finish - t_start;
		ctime.acc(t_curr);
		logger->Debug("[Task %d] timing current = %.2f us", task_id, t_curr);
	}
	ctime.timer.stop();
	logger->Info("[Task %2d] profiling stopped", task_id);
}

// ---------------- BbqueEXC overloading ------------------------------------//

RTLIB_ExitCode_t ExecutionSynchronizer::onSetup() {
	if (!CheckTaskGraph(task_graph))
		return RTLIB_ERROR;

	// Synchronization event for onRun() return
	auto outb = task_graph->OutputBuffer();
	if (outb == nullptr) {
		logger->Error("onSetup: Task-graph output buffer missing");
		return RTLIB_ERROR;
	}

	auto ev = task_graph->GetEvent(outb->Event());
	if (ev == nullptr) {
		logger->Error("onSetup: Task-graph synchronization event missing"
			"(associated to the output buffer)");
		return RTLIB_ERROR;
	}

	on_run_sync = events[ev->Id()];
	logger->Info("onSetup: Task-graph synchronization event_id = %d", on_run_sync->id);

	// Send the task graph
	SendTaskGraphToRM();
	logger->Info("onSetup: [Application %s] starting...", app_name.c_str());

	return RTLIB_OK;
}


RTLIB_ExitCode_t ExecutionSynchronizer::onConfigure(int8_t awm_id) {
	(void) awm_id;

	// Task graph here should has been filled by the RTRM policy
	RecvTaskGraphFromRM();
	logger->Info("onConfigure: Resource allocation performed");
	NotifyResourceAllocation();

	// TODO: reconfiguration management
	if (Cycles() > 1) {
		logger->Warn("onConfigure: Reconfiguration not supported yet");
		return RTLIB_OK;
	}

	// Wait for tasks to start
	std::unique_lock<std::mutex> tasks_lock(tasks.mx);
	logger->Info("onConfigure: Waiting for tasks to start...");
	while (tasks.is_stopped.any()) {
		logger->Debug("Tasks stopped: %s", tasks.is_stopped.to_string().c_str());
		tasks.cv.wait(tasks_lock);
	}

	logger->Info("onConfigure: Tasks queue length: %d", tasks.start_queue.size());
	while (!tasks.start_queue.empty()) {
		auto task_id = tasks.start_queue.front();
		tasks.runtime[task_id]->monitor_thr = std::move(
			std::thread(&ExecutionSynchronizer::TaskProfiler, this, task_id));
		tasks.start_queue.pop();
		logger->Info("onConfigure: [Task %2d] started on processor %d", task_id,
				task_graph->GetTask(task_id)->GetAssignedProcessor());
	}

	logger->Info("onConfigure: All tasks have been launched");

	return RTLIB_OK;
}

RTLIB_ExitCode_t ExecutionSynchronizer::onRun() {
	logger->Info("onRun: [c=%02d]...", Cycles());
	{
		std::unique_lock<std::mutex> tasks_lock(tasks.mx);
		if (tasks.is_stopped.count() == task_graph->TaskCount()) {
			logger->Info("onRun: Termination...");
			return RTLIB_EXC_WORKLOAD_NONE;
		}
	}

	// Wait for synchronization event: task-graph output
	std::unique_lock<std::mutex> run_lock(on_run_sync->mx);
	while (!on_run_sync->occurred) {
		on_run_sync->cv.wait(run_lock);
		logger->Info("onRun: [c=%02d] sync_event: %d", Cycles(), on_run_sync->id);
	}
	on_run_sync->occurred = false;

	return RTLIB_OK;
}


RTLIB_ExitCode_t ExecutionSynchronizer::onMonitor() {

	for (auto & rt_entry: tasks.runtime) {
		// Task completion time
		auto & ctime(rt_entry.second->ctime);
		logger->Info("onMonitor: [Task %2d] execution time  = %.2f us (mean)",
			rt_entry.first, mean(ctime.acc));

		// Task throughput: multiple by 100 for the decimal part (divide when read)
		auto & throughput(rt_entry.second->throughput);
		uint16_t task_throughput =
			static_cast<uint16_t>((float(1e6) / float(mean(ctime.acc))) * 100);
		logger->Info("onMonitor: [Task %2d] throughput      = %.2f CPS",
			rt_entry.first, task_throughput);

		throughput.acc(task_throughput);
		task_graph->GetTask(rt_entry.first)->SetProfiling(
				task_throughput, mean(ctime.acc));
	}

	auto tg_throughput = GetCPS() * 100;
	auto tg_time = GetExecutionTimeMs();
	logger->Info("onMonitor: Task-graph throughput     = %.2f CPS", tg_throughput);
	logger->Info("onMonitor: Task-graph execution time = %d ms", tg_time);
	task_graph->SetProfiling(tg_throughput, tg_time);
	SendTaskGraphToRM();

	return RTLIB_OK;
}


RTLIB_ExitCode_t ExecutionSynchronizer::onRelease() {

	for (auto & ev_entry: events) {
		logger->Info("onRelease: notifying event %d to unlock", ev_entry.first);
		NotifyEvent(ev_entry.first);
	}

	for (auto & rt_entry: tasks.runtime) {
		auto & monitor(rt_entry.second->monitor_thr);
		monitor.join();
		logger->Info("onRelease: Monitoring thread joined");
	}

	PrintProfilingData();

	return RTLIB_OK;
}


#define LIBPMS_PROF_TABLE_DIV \
	"==============+==========+=======================================+========================================"

#define LIBPMS_PROF_TABLE_DIV2 \
	"--------------+----------+---------------------------------------+---------------------------------------|"

#define LIBPMS_PROF_TABLE_HEADER \
	"| Task        |    N     |           Completion time (ms)        |           Throughput (CPS)            |"

#define LIBPMS_APP_TABLE_HEADER \
	"| Application            |           Completion time (ms)        |         Avg. Throughput (CPS)         |"

#define LIBPMS_PROF_TABLE_HEADER2 \
	"|             |          |     min      max      avg     var     |     min      max      avg     var     |"

void ExecutionSynchronizer::PrintProfilingData() const {
#if 0
	// Per-task profiling printing left to the programming model library

	logger->Notice(LIBPMS_PROF_TABLE_DIV);
	logger->Notice(LIBPMS_PROF_TABLE_HEADER);
	logger->Notice(LIBPMS_PROF_TABLE_DIV2);
	logger->Notice(LIBPMS_PROF_TABLE_HEADER2);
	logger->Notice(LIBPMS_PROF_TABLE_DIV);

	// Per task
	for (auto & rt_entry: tasks.runtime) {
		auto & task_id = rt_entry.first;
		auto & ctime   = rt_entry.second->ctime;
		auto & throughput = rt_entry.second->throughput;
		logger->Notice("| %4d | %7d | %8.2f %8.2f %8.2f %8.2f   | %8.2f %8.2f %8.2f %8.2f   |",
			task_id, count(ctime.acc),
			min(ctime.acc)/1e3, max(ctime.acc)/1e3, mean(ctime.acc)/1e3, variance(ctime.acc)/1e6,
			min(throughput.acc)/100.0, max(throughput.acc)/100.0,
			mean(throughput.acc)/100.0, variance(throughput.acc)/1e4
		);
	}
#endif

	// Overall
	uint32_t total_ctime;
	uint16_t final_throughput;
	task_graph->GetProfiling(final_throughput, total_ctime);
	if (total_ctime == 0)
		total_ctime = GetExecutionTimeMs();

	std::string trunc_app_name(app_name);
	if (trunc_app_name.size() > 21)
		trunc_app_name = app_name.substr(0, 21);

	logger->Notice(LIBPMS_PROF_TABLE_DIV);
	logger->Notice(LIBPMS_APP_TABLE_HEADER);
	logger->Notice(LIBPMS_PROF_TABLE_DIV2);
	logger->Notice("| %-21s  |  %34d   | %35.2f   |",
		trunc_app_name.c_str(), total_ctime, final_throughput);
	logger->Notice(LIBPMS_PROF_TABLE_DIV);
}


// ---------------------------------------------------------------------------//


} // namespace bbque
