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

#ifndef BBQUE_DEFERRABLE_H_
#define BBQUE_DEFERRABLE_H_

#include "bbque/utils/logging/logger.h"
#include "bbque/utils/utility.h"
#include "bbque/utils/worker.h"
#include "bbque/utils/timer.h"

#include <functional>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>


using std::chrono::time_point;
using std::chrono::milliseconds;
using std::chrono::system_clock;

using bbque::utils::Timer;
using bbque::utils::Worker;

namespace bbque { namespace utils {

/**
 * @class Deferrable
 *
 * @brief Deferrable execution of specified functionalities
 *
 * This class provides the basic support to schedule a deferrable execution of
 * a specified task, with the possibility to update the scheduled execution
 * time and also to repeat the execution at specified intervals.
 * On each time, the most recent future execution request is executed, by
 * discarding all the (non periodic) older ones.
 */
class Deferrable : public Worker {

public:

	typedef time_point<system_clock> DeferredTime_t;
	typedef std::function<void(void)> DeferredFunction_t;

#define SCHEDULE_NONE milliseconds(0)

	/**
	 * @brief Build a new "on-demand" or "repetitive" deferrable object
	 *
	 * @param name the name of this executor, used mainly for logging
	 * purposes
	 * @param period when not null, the minimum execution period [ms] for
	 * each execution
	 */
	Deferrable(const char *name, DeferredFunction_t func = NULL,
			milliseconds period = SCHEDULE_NONE);

	/**
	 * @brief Release all deferrable resources
	 */
	virtual ~Deferrable();

	const char *Name() const {
		return name.c_str();
	}

	/**
	 * @brief The operations to execute
	 *
	 * This should be implemeted by derived classes with the set of
	 * operations to be executed at the next nearest scheduled time.
	 */
	virtual void Execute() {
		if (func) func();
	}

#define SCHEDULE_NOW  milliseconds(0)

	/**
	 * @brief Schedule a new execution
	 *
	 * A new execution of this deferred is scheduled into the specified
	 * time, or immediately if time is 0.
	 */
	void Schedule(milliseconds time = SCHEDULE_NOW);

	/**
	 * @brief Update the "repetitive" scheduling period
	 */
	void SetPeriodic(milliseconds period);

	/**
	 * @brief Set the deferrable to be just on-demand
	 */
	void SetOnDemand();

protected:

	/**
	 * @brief The defarrable name
	 */
	const std::string name;

private:

#define BBQUE_DEFERRABLE_THDNAME_MAXLEN 32
	/**
	 * @brief The deferrable (thread) name
	 */
	char thdName[BBQUE_DEFERRABLE_THDNAME_MAXLEN];

	/**
	 * @brief
	 */
	DeferredFunction_t func;

	/**
	 * @brief The minimum "repetition" period [ms] for the execution
	 *
	 * When this period is 0 this is just an on-demand deferrable which
	 * execute one time for the nearest of the pending schedule.
	 * Otherwise, when not zero, an execution is triggered at least once
	 * every min_time
	 */
	milliseconds max_time;

	/**
	 * @brief The next execution time
	 */
	DeferredTime_t next_time;

	/**
	 * @brief A timeout for the nearest execution time
	 */
	milliseconds next_timeout;

	/**
	 * @brief A timer used for validation
	 */
	DB(Timer tmr);

	/**
	 * @brief The deferrable execution thread
	 */
	void Task();

};

} // namespace utils

} // namespace bbque

#endif // BBQUE_DEFERRABLE_H_
