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

#include <chrono>
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

namespace bu = bbque::utils;
namespace br = bbque::res;

using EnergySampleType = uint64_t;

namespace bbque {

class EnergyMonitor : public CommandHandler, bu::Worker
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

	/**
	 * @brief Command handlers dispatcher
	 */
	int CommandsCb(int argc, char *argv[]) override;

	/**
	 * @brief System power budget, given the target lifetime set
	 * @return The power value in mW; 0: No target set; -1: Always on
	 * mode required
	 */
	int32_t GetSystemPowerBudget();

#ifdef CONFIG_BBQUE_PM_BATTERY

	/**
	 * @brief System lifetime left (in seconds)
	 *
	 * @return Chrono duration object (seconds) with the count of the
	 * remaining seconds
	 */
	inline std::chrono::seconds GetSystemLifetimeLeft() const
	{
		auto now = std::chrono::system_clock::now();
		return std::chrono::duration_cast<std::chrono::seconds>(sys_lifetime.target_time - now);
	}
#endif

private:

#ifdef CONFIG_BBQUE_PM_BATTERY
	BatteryManager & bm;
#endif

	PowerManager & pm;

	CommandManager & cm;

	ConfigurationManager & cfm;

	std::unique_ptr<bu::Logger> logger;

	mutable std::mutex m;

	mutable std::condition_variable cv;

	bool is_sampling;

	std::atomic<bool> terminated;

	std::map<br::ResourcePathPtr_t, EnergySampleType> values;

#ifdef CONFIG_BBQUE_PM_BATTERY

	/**
	 * @brief Battery object instance
	 */
	BatteryPtr_t pbatt;

	uint32_t batt_sampling_period;

	/**
	 * @struct SystemLifetimeInfo_t
	 * @brief System power budget information
	 */
	struct SystemLifetimeInfo_t
	{
		/** Mutex to protect concurrent accesses */
		std::mutex mtx;
		/** Time point of the required system lifetime */
		std::chrono::system_clock::time_point target_time;
		/** System power budget to guarantee the required lifetime */
		int32_t power_budget_mw = 0;
		/** If true the request is to keep the system always on */
		bool always_on;
	} sys_lifetime;

	std::map<PowerManager::InfoType, std::shared_ptr<bbque::trig::Trigger>> triggers;

#endif // CONFIG_BBQUE_PM_BATTERY

	EnergyMonitor();

	/**
	 * @brief Inhibit the possibility of starting an energy consumption sampling
	 * while already in progress
	 */
	void WaitForSamplingTermination();

	/**
	 * @brief Periodic task
	 */
	void Task() override;


#ifdef CONFIG_BBQUE_PM_BATTERY
	/**
	 * @brief Sample the battery status
	 */
	void SampleBatteryStatus();

	/**
	 * @brief Compute the system power budget
	 * @return The power value in milliwatts.
	 */
	inline uint32_t ComputeSysPowerBudget() const
	{
		// How many seconds remains before lifetime target is reached?
		std::chrono::seconds secs_from_now = GetSystemLifetimeLeft();
		// System energy budget in mJ
		uint32_t energy_budget = pbatt->GetChargeMAh() * 3600 *
			pbatt->GetVoltage() / 1e3;
		return energy_budget / secs_from_now.count();
	}

	/**
	 * @brief Trigger execution for the battery status.
	 * The optimization is required in case of battery level under
	 * a given threshold
	 */
	void ExecuteTriggerForBattery();

	/**
	 * @brief System target lifetime setting
	 * @param action The control actions:
	 *		set   (to set the amount of hours)
	 *		info  (to get the current information)
	 *		clear (to clear the target)
	 *		help  (command help)
	 * @param hours For the action 'set' only
	 * @return 0 for success, a negative number in case of error
	 */
	int SystemLifetimeCmdHandler(
				const std::string action,
				const std::string arg);

	/**
	 * @brief System target lifetime information report
	 */
	void PrintSystemLifetimeInfo() const;

#endif // CONFIG_BBQUE_PM_BATTERY
};


}


#endif /* BBQUE_ENERGY_MONITOR_H */

