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
#include "bbque/trig/trigger_factory.h"

#define MODULE_NAMESPACE    "bq.eym"
#define MODULE_CONFIG       "EnergyMonitor"

#define LOAD_CONFIG_OPTION(name, type, var, default) \
	opts_desc.add_options() \
	(MODULE_CONFIG "." name, po::value<type>(&var)->default_value(default), "");

namespace po = boost::program_options;

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
    pm(PowerManager::GetInstance()),
    cm(CommandManager::GetInstance()),
    cfm(ConfigurationManager::GetInstance())
{
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	logger->Info("EnergyMonitor initialization...");
	this->terminated = false;

#ifdef CONFIG_BBQUE_PM_BATTERY

	// Configuration options for the battery management
	std::string batt_trig;
	uint32_t batt_thrs_level = 0;
	uint32_t batt_thrs_rate = 0;
	float  batt_thrs_rate_margin = 0.05;

	try {
		po::options_description opts_desc("Energy Monitor options");
		LOAD_CONFIG_OPTION("batt.trigger", std::string, batt_trig, "under_threshold");
		LOAD_CONFIG_OPTION("batt.threshold_level", uint32_t, batt_thrs_level, 15);
		LOAD_CONFIG_OPTION("batt.threshold_rate",  uint32_t, batt_thrs_rate,  0);
		LOAD_CONFIG_OPTION("batt.margin_rate", float, batt_thrs_rate_margin, 0.05);
		LOAD_CONFIG_OPTION("batt.sampling_period", uint32_t, this->batt_sampling_period, 20000);
		po::variables_map opts_vm;
		cfm.ParseConfigurationFile(opts_desc, opts_vm);
	}
	catch (boost::program_options::invalid_option_value ex) {
		logger->Error("Errors in configuration file [%s]", ex.what());
	}

	// Triggers registration
	bbque::trig::TriggerFactory & tgf(bbque::trig::TriggerFactory::GetInstance());
	logger->Debug("Battery current scheduling policy trigger setting");
	// Battery status scheduling policy trigger setting
	triggers[PowerManager::InfoType::CURRENT] = tgf.GetTrigger(batt_trig);
	triggers[PowerManager::InfoType::CURRENT]->threshold_high = batt_thrs_rate;
	triggers[PowerManager::InfoType::CURRENT]->margin = batt_thrs_rate_margin;
	logger->Debug("Battery energy scheduling policy trigger setting");
	triggers[PowerManager::InfoType::ENERGY] = tgf.GetTrigger(batt_trig);
	triggers[PowerManager::InfoType::ENERGY]->threshold_high = batt_thrs_level;

	logger->Info("=====================================================================");
	logger->Info("| THRESHOLDS             | VALUE       | MARGIN  |      TRIGGER     |");
	logger->Info("+------------------------+-------------+---------+------------------+");
	logger->Info("| Battery discharge rate | %6d %%/h  | %6.0f%% | %16s |",
		triggers[PowerManager::InfoType::CURRENT]->threshold_high,
		triggers[PowerManager::InfoType::CURRENT]->margin * 100, batt_trig.c_str());
	logger->Info("| Battery charge level   | %6d %c/100|  %6s | %16s |",
		triggers[PowerManager::InfoType::ENERGY]->threshold_high, '%', "-", batt_trig.c_str());
	logger->Info("=====================================================================");

	// Commands
#define CMD_WM_SYSLIFETIME "syslifetime"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_WM_SYSLIFETIME,
			static_cast<CommandHandler*>(this),
			"Set the system target lifetime");

	pbatt = bm.GetBattery();
	if (pbatt == nullptr)
		logger->Warn("Battery available: NO");
	else
		logger->Info("Battery available: %s", pbatt->StrId().c_str());

	// Monitoring task for the battery(ies)
	Worker::Setup(BBQUE_MODULE_NAME("eym.batt"), MODULE_NAMESPACE);
	Worker::Start();

#endif // CONFIG_BBQUE_PM_BATTERY

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

int EnergyMonitor::CommandsCb(int argc, char *argv[])
{
	uint8_t cmd_offset = ::strlen(MODULE_NAMESPACE) + 1;
	char * command_id  = argv[0] + cmd_offset;
	logger->Info("CommandsCb: processing command [%s]", command_id);

#ifdef CONFIG_BBQUE_PM_BATTERY
	// System life-time target
	if (!strncmp(CMD_WM_SYSLIFETIME , command_id, strlen(CMD_WM_SYSLIFETIME))) {
		if (argc < 2) {
			logger->Error("CommandsCb: command [%s] missing argument"
				"[set/clear/info/help]", command_id);
			return 1;
		}
		if (argc > 2)
			return SystemLifetimeCmdHandler(argv[1], argv[2]);
		else
			return SystemLifetimeCmdHandler(argv[1], "");
	}
#endif
	logger->Error("CommandsCb: unknown command [%s]", command_id);
	return -1;
}

/*******************************************************************
 *                 ENERGY BUDGET MANAGEMENT                        *
 *******************************************************************/

void EnergyMonitor::Task()
{
	logger->Debug("Task: battery status monitoring...");
	while (pbatt && !this->terminated) {
		logger->Debug("Task: battery power=%d mW charge=%d",
			pbatt->GetPower(),
			pbatt->GetChargePerc());
		ExecuteTriggerForBattery();
		std::this_thread::sleep_for(std::chrono::milliseconds(this->batt_sampling_period));
	}
}

#ifndef CONFIG_BBQUE_PM_BATTERY

int32_t EnergyMonitor::GetSystemPowerBudget()
{
	return 0;
}
#else // CONFIG_BBQUE_PM_BATTERY

int32_t EnergyMonitor::GetSystemPowerBudget()
{

	std::unique_lock<std::mutex> ul(sys_lifetime.mtx);
	/*
		if (!pbatt->IsDischarging() && pbatt->GetChargePerc() == 100) {
			logger->Debug("System battery full charged and power plugged");
			return 0;
		}
	 */

	if (sys_lifetime.always_on) {
		logger->Debug("GetSysPowerBudget: system lifetime target = 'always_on'");
		return -1;
	}

	if (sys_lifetime.power_budget_mw == 0) {
		logger->Debug("GetSysPowerBudget: no system lifetime target");
		return 0;
	}

	// Compute power budget
	sys_lifetime.power_budget_mw = ComputeSysPowerBudget();
	return sys_lifetime.power_budget_mw;

}

int EnergyMonitor::SystemLifetimeCmdHandler(const std::string action, const std::string hours)
{
	std::unique_lock<std::mutex> ul(sys_lifetime.mtx);
	std::chrono::system_clock::time_point now;
	logger->Info("SystemLifetimeCmdHandler: action=[%s], hours=[%s]",
		action.c_str(), hours.c_str());
	// Help
	if (action.compare("help") == 0) {
		logger->Notice("SystemLifetimeCmdHandler: %s set <HOURS> (set hours)", CMD_WM_SYSLIFETIME);
		logger->Notice("SystemLifetimeCmdHandler: %s info  (target lifetime)", CMD_WM_SYSLIFETIME);
		logger->Notice("SystemLifetimeCmdHandler: %s clear (clear setting)",   CMD_WM_SYSLIFETIME);
		logger->Notice("SystemLifetimeCmdHandler: %s help  (this help)",  CMD_WM_SYSLIFETIME);
		return 0;
	}
	// Clear the target lifetime setting
	if (action.compare("clear") == 0) {
		logger->Notice("SystemLifetimeCmdHandler: clearing system target lifetime...");
		sys_lifetime.power_budget_mw = 0;
		sys_lifetime.always_on   = false;
		return 0;
	}
	// Return information about last target lifetime set
	if (action.compare("info") == 0) {
		logger->Notice("SystemLifetimeCmdHandler: system target lifetime information...");
		sys_lifetime.power_budget_mw = ComputeSysPowerBudget();
		PrintSystemLifetimeInfo();
		return 0;
	}
	// Set the target lifetime
	if (action.compare("set") == 0) {
		logger->Notice("SystemLifetimeCmdHandler: setting system target lifetime...");
		// Argument check
		if (!IsNumber(hours)) {
			logger->Error("SystemLifetimeCmdHandler: invalid argument");
			return -1;
		}
		else if (hours.compare("always_on") == 0) {
			logger->Info("SystemLifetimeCmdHandler: set to 'always on'");
			sys_lifetime.power_budget_mw = -1;
			sys_lifetime.always_on     = true;
			return 0;
		}
		// Compute system clock target lifetime
		now = std::chrono::system_clock::now();
		std::chrono::hours h(std::stoi(hours));
		sys_lifetime.target_time     = now + h;
		sys_lifetime.always_on       = false;
		sys_lifetime.power_budget_mw = ComputeSysPowerBudget();
		PrintSystemLifetimeInfo();
	}
	return 0;
}

void EnergyMonitor::ExecuteTriggerForBattery()
{
	// Do not require other policy execution (due battery level) until the charge
	// is not above the threshold value again
	if (pbatt->IsDischarging()) {
		/*
			// Battery level
			ManageRequest(
				PowerManager::InfoType::ENERGY, static_cast<float>(pbatt->GetChargePerc()));
			// Discharging rate check
			ManageRequest(PowerManager::InfoType::CURRENT, pbatt->GetDischargingRate());
		 */
		logger->Debug("ExecuteTriggerForBattery: %d", pbatt->GetChargePerc());
	}
}

void EnergyMonitor::PrintSystemLifetimeInfo() const
{
	std::chrono::seconds secs_from_now;
	// Print output
	std::time_t time_out = std::chrono::system_clock::to_time_t(sys_lifetime.target_time);
	logger->Notice("System target lifetime    : %s", ctime(&time_out));
	secs_from_now = GetSystemLifetimeLeft();
	logger->Notice("System target lifetime [s]: %d", secs_from_now.count());
	logger->Notice("System power budget   [mW]: %d", sys_lifetime.power_budget_mw);
}

#endif // CONFIG_BBQUE_PM_BATTERY


} // namespace bbque