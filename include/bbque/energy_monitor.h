/*
 * Copyright (C) 2020  Politecnico di Milano
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

#ifndef BBQUE_ENERGY_MONITOR_H
#define BBQUE_ENERGY_MONITOR_H

#include <map>
#include <memory>
#include <mutex>

#include "bbque/config.h"
#include "bbque/configuration_manager.h"
#include "bbque/pm/battery_manager.h"
#include "bbque/pm/power_manager.h"
#include "bbque/trig/trigger.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/worker.h"

#define ENERGY_MONITOR_NAMESPACE    "bq.eym"

namespace bu = bbque::utils;
namespace br = bbque::res;

using EnergySampleType = uint64_t;

namespace bbque {

class EnergyMonitor
{
public:

	static EnergyMonitor & GetInstance();

	virtual ~EnergyMonitor();

	EnergyMonitor & operator=(EnergyMonitor &) = delete;

	/**
	 * @brief Register a resource for monitoring its energy consumption
	 * @param resource_path The path object of the resource
	 */
	void RegisterResource(br::ResourcePathPtr_t resource_path);

	/**
	 * @brief Start the monitoring of the energy consumption
	 */
	void StartSamplingResourceConsumption();

	/**
	 * @brief Start the monitoring of the energy consumption and get the
	 * values
	 */
	void StopSamplingResourceConsumption();

	/**
	 * @brief Get the energy consumption value of a specific resource
	 * @param resource_path The path object of the resource
	 * @return the energy consumption
	 */
	EnergySampleType GetValue(br::ResourcePathPtr_t resource_path) const;

private:

#ifdef CONFIG_BBQUE_PM_BATTERY
	BatteryManager & bm;
#endif

	PowerManager & pm;

	std::unique_ptr<bu::Logger> logger;

	mutable std::mutex m;

	mutable std::condition_variable cv;

	bool is_sampling;

	std::atomic<bool> terminated;

	std::map<br::ResourcePathPtr_t, EnergySampleType> values;

	EnergyMonitor();

	/**
	 * @brief Inhibit the possibility of starting an energy consumption sampling
	 * while already in progress
	 */
	void WaitForSamplingTermination();

};


}


#endif /* BBQUE_ENERGY_MONITOR_H */

