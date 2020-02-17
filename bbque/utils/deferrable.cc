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

#include "bbque/utils/deferrable.h"

#define DEFERRABLE_NAMESPACE "bq.df"
#define MODULE_NAMESPACE DEFERRABLE_NAMESPACE

namespace bbque
{
namespace utils
{

Deferrable::Deferrable(const char *name,
                       DeferredFunction_t func,
                       milliseconds period) : Worker(),
	name(name),
	func(func),
	max_time(period),
	next_time(system_clock::now())
{

	//---------- Setup Worker
	snprintf(thdName, BBQUE_DEFERRABLE_THDNAME_MAXLEN, DEFERRABLE_NAMESPACE ".%s", Name());
	Worker::Setup(thdName, thdName);

	if (max_time == SCHEDULE_NONE) {
		logger->Debug("Starting new \"on-demand\" deferrable [%s]...",
		              Name());
	} else {
		logger->Debug("Starting new \"repetitive\" deferrable [%s], period %d[ms]...",
		              Name(), max_time);
	}

	// Start "periodic" deferrables
	Worker::Start();

}

Deferrable::~Deferrable()
{
}

void Deferrable::Schedule(milliseconds time)
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	DeferredTime_t request_time = system_clock::now();
	DeferredTime_t schedule_time = request_time + time;

	// Handle immediate scheduling
	if (time == SCHEDULE_NOW) {
		logger->Debug("DF[%s] immediate scheduling required", Name());
		next_timeout = SCHEDULE_NOW;
		worker_status_cv.notify_one();
		return;
	}

	// Return if _now_ (i.e. schedule_time) we already have a pending
	// schedule which is nearest then the required schedule time
	if (BBQUE_LIKELY(next_time > request_time)) {
		logger->Debug("DF[%s] checking for future schedule...", Name());
		if (schedule_time >= next_time) {
			DB(logger->Debug("DF[%s: %9.3f] nearest then %d[ms] schedule pending",
			                 Name(), tmr.getElapsedTimeMs(), time));
			return;
		}
	}

	// Update for next nearest schedule time
	DB(logger->Debug("DF[%s: %9.3f] update nearest schedule to %d[ms]",
	                 Name(), tmr.getElapsedTimeMs(), time));
	next_time = schedule_time;
	next_timeout = time;
	worker_status_cv.notify_one();

}

void Deferrable::SetPeriodic(milliseconds period)
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);

	if (period == SCHEDULE_NONE) {
		logger->Info("DF[%s] unexpected SetPeriodic() 0[ms], "
		             "must use the SetOnDemand() instead.",
		             Name());
		assert(period != SCHEDULE_NONE);
		return;
	}

	logger->Info("DF[%s] set \"repetitive\" mode, period %d[ms]",
	             Name(), period);

	max_time = period;
	worker_status_cv.notify_one();
}

void Deferrable::SetOnDemand()
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	logger->Info("DF[%s] set \"on-demand\" mode", Name());
	max_time = SCHEDULE_NONE;
	worker_status_cv.notify_one();
}

#define RESET_TIMEOUT \
	do { \
		next_timeout = SCHEDULE_NONE; \
		if (max_time != SCHEDULE_NONE) \
			next_timeout = max_time; \
		next_time = system_clock::now() + next_timeout; \
	} while(0)

void Deferrable::Task()
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	std::cv_status wakeup_reason;

	logger->Info("DF[%s] Deferrable thread STARTED", Name());

	// Schedule next execution if we are "repetitive"
	RESET_TIMEOUT;

	DB(tmr.start());
	while (!done) {

		// Wait for next execution or re-scheduling
		if (next_timeout == SCHEDULE_NONE) {
			DB(logger->Debug("DF[%s: %9.3f] on-demand waiting...",
			                 Name(), tmr.getElapsedTimeMs()));

			DB(tmr.start());
			worker_status_cv.wait(worker_status_ul);
			DB(logger->Debug("DF[%s: %9.3f] wakeup ON-DEMAND",
			                 Name(), tmr.getElapsedTimeMs()));

		} else {
			DB(logger->Debug("DF[%s: %9.3f] waiting for %d[ms]...",
			                 Name(), tmr.getElapsedTimeMs(),
			                 next_timeout));

			DB(tmr.start());
			wakeup_reason = worker_status_cv.wait_for(worker_status_ul, next_timeout);
			if (wakeup_reason == std::cv_status::timeout) {
				DB(logger->Debug("DF[%s: %9.3f] wakeup TIMEOUT",
				                 Name(), tmr.getElapsedTimeMs()));
				next_timeout = SCHEDULE_NOW;
			}

		}

		if (BBQUE_UNLIKELY(done)) {
			DB(fprintf(stderr, FD("DF[%s] exiting executor..."), Name()));
			continue;
		}

		// Timeout rescheduling due to nearest schedule
		if (next_timeout != SCHEDULE_NOW) {
			DB(logger->Debug("DF[%s: %9.3f] rescheduling timeout in %d[ms]",
			                 Name(), tmr.getElapsedTimeMs(),
			                 next_timeout));
			continue;
		}

		// Execute the deferred task
		logger->Info("DF[%s] execution START", Name());
		Execute();
		logger->Info("DF[%s] execution DONE", Name());

		// Setup the next timeout
		RESET_TIMEOUT;

	}

	DB(fprintf(stderr, FI("DF[%s] Deferrable thread ENDED"), Name()));

}

} // namespace utils

} // namespace bbque
