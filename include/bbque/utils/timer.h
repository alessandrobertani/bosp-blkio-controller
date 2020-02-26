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

#ifndef BBQUE_TIMER_H_
#define BBQUE_TIMER_H_

#include <mutex>
#include <time.h>
#include <unistd.h>

namespace bbque { namespace utils {

/**
 * @class Timer
 *
 * @brief Get time intervals with microseconds resolution.
 *
 * This class provides basic service to measure elapsed time with microseconds
 * accuracy.
 */
class Timer {

public:

	/**
	 * @brief Build a new Timer object
	 */
	Timer(bool running = false);

	/**
	 * @brief Monothonic timestamp in [s]
	 * The returned timestamp refers to some unspecified starting point.
	 */
	static double getTimestamp();

	/**
	 * @brief Monothonic timestamp in [ms]
	 * The returned timestamp refers to some unspecified starting point.
	 */
	static double getTimestampMs();

	/**
	 * @brief Monothonic timestamp in [us]
	 * The returned timestamp refers to some unspecified starting point.
	 */
	static double getTimestampUs();

	/**
	 * @brief Check if the timer is running
	 */
	inline bool Running() const {
		return !stopped;
	}

	/**
	 * @brief Start the timer
	 * startCount will be set at this point.
	 */
	void start();

	/**
	 * @brief Stop the timer
	 * endCount will be set at this point.
	 */
	void stop();

	/**
	 * @brief get elapsed time in [s]
	 * @return [s] of elapsed time
	 */
	double getElapsedTime();

	/**
	 * @brief get elapsed time in [ms]
	 * @return [ms] of elapsed time
	 */
	double getElapsedTimeMs();

	/**
	 * @brief get elapsed time in [us]
	 * @return [us] of elapsed time
	 */
	 double getElapsedTimeUs();

private:

	/**
	 * stop flag
	 */
	bool stopped;

	/**
	 * The mutex protecting timer concurrent access
	 */
	std::mutex timer_mtx;

	/**
	 * The timer start instant
	 */
	struct timespec start_ts;

	/**
	 * The timer stop instant
	 */
	struct timespec stop_ts;
};

} // namespace utils

} // namespace bbque

#endif // BBQUE_TIMER_H_
