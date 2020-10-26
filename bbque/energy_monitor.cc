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

#include "bbque/energy_monitor.h"
#include "bbque/resource_accounter_status.h"


namespace bbque {

EnergyMonitor & EnergyMonitor::GetInstance()
{
	static EnergyMonitor instance;
	return instance;
}

EnergyMonitor::EnergyMonitor() :
#ifdef CONFIG_BBQUE_PM_BATTERY
    bm(BatteryManager::GetInstance()),
#endif
    pm(PowerManager::GetInstance())
{
	logger = bu::Logger::GetLogger(ENERGY_MONITOR_NAMESPACE);
	assert(logger);
	logger->Info("EnergyMonitor initialization...");
	this->terminated = false;
}

EnergyMonitor::~EnergyMonitor()
{
	this->terminated = true;
	StopSamplingResourceConsumption();
}

void EnergyMonitor::RegisterResource(br::ResourcePathPtr_t resource_path)
{
	std::lock_guard<std::mutex> lck(this->m);
	if (resource_path != nullptr) {
		this->values.emplace(resource_path, 0);
		logger->Debug("RegisterResource: <%s> for energy monitoring",
			resource_path->ToString().c_str());
	}
}

void EnergyMonitor::StartSamplingResourceConsumption()
{
	logger->Debug("StartResourceConsumptionSampling...");
	WaitForSamplingTermination();
	if (this->terminated)
		return;

	std::lock_guard<std::mutex> lck(this->m);
	this->is_sampling = true;
	for (auto & e_entry : values) {
		this->pm.StartEnergyMonitor(e_entry.first);
	}
}

void EnergyMonitor::StopSamplingResourceConsumption()
{
	logger->Debug("StopResourceConsumptionSampling...");
	std::lock_guard<std::mutex> lck(this->m);

	if (!this->is_sampling) {
		logger->Debug("StopResourceConsumptionSampling: no sampling in progress");
		return;
	}

	for (auto & e_entry : values) {
		br::ResourcePathPtr_t const & resource_path(e_entry.first);
		e_entry.second = this->pm.StopEnergyMonitor(resource_path);
		logger->Info("StopResourceConsumptionSampling: <%s> value=%ldnJ",
			resource_path->ToString().c_str(),
			e_entry.second);
	}

	this->is_sampling = false;
	cv.notify_all();
}

EnergySampleType EnergyMonitor::GetValue(br::ResourcePathPtr_t resource_path) const
{
	std::lock_guard<std::mutex> lck(this->m);
	auto it = values.find(resource_path);
	if (it != values.end()) {
		return it->second;
	}
	return 0;
}

void EnergyMonitor::WaitForSamplingTermination()
{
	std::unique_lock<std::mutex> lck(this->m);
	while (this->is_sampling) {
		logger->Debug("WaitForSamplingTermination: sampling in progress");
		cv.wait(lck);
	}
	logger->Debug("WaitForSamplingTermination: sampling terminated");
}


} // namespace bbque