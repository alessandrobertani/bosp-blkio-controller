/*
 * Copyright (C) 2014  Politecnico di Milano
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

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

#include "bbque/power_monitor.h"

#include "bbque/resource_accounter.h"
#include "bbque/res/resource_path.h"
#include "bbque/utils/utility.h"
#include "bbque/trig/trigger_factory.h"


#define MODULE_CONFIG "PowerMonitor"
#define MODULE_NAMESPACE POWER_MONITOR_NAMESPACE

#define WM_LOGFILE_FMT "%s/BBQ_PowerMonitor_%.dat"
#define WM_LOGFILE_HEADER \
	"# Columns legend:\n"\
	"#\n"\
	"# 1: Load (%)\n"\
	"# 2: Temperature (°C)\n"\
	"# 3: Core frequency (MHz)\n"\
	"# 4: Fanspeed (%)\n"\
	"# 5: Voltage (mV)\n"\
	"# 6: Performance level\n"\
	"# 7: Power state\n"\
	"#\n"


namespace po = boost::program_options;
using namespace bbque::trig;

namespace bbque {


#define LOAD_CONFIG_OPTION(name, type, var, default) \
	opts_desc.add_options() \
	(MODULE_CONFIG "." name, po::value<type>(&var)->default_value(default), "");

PowerMonitor & PowerMonitor::GetInstance()
{
	static PowerMonitor instance;
	return instance;
}

PowerMonitor::PowerMonitor() :
    Worker(),
    pm(PowerManager::GetInstance()),
    cm(CommandManager::GetInstance()),
#ifdef CONFIG_BBQUE_DM
    dm(DataManager::GetInstance()),
#endif
    cfm(ConfigurationManager::GetInstance()),
    optimize_dfr("wm.opt", std::bind(&PowerMonitor::SendOptimizationRequest, this))
{
	// Initialization
	logger = bu::Logger::GetLogger(POWER_MONITOR_NAMESPACE);
	assert(logger);
	logger->Info("PowerMonitor initialization...");
	Init();

	// Configuration options
	uint32_t temp_crit  = 0, temp_crit_arm  = 0;
	uint32_t power_cons = 0, power_cons_arm = 0;
	float temp_margin = 0.05, power_margin = 0.05;
	std::string temp_trig, power_trig;

	try {
		po::options_description opts_desc("Power Monitor options");
		LOAD_CONFIG_OPTION("period_ms", uint32_t,  wm_info.period_ms, WM_DEFAULT_PERIOD_MS);
		LOAD_CONFIG_OPTION("log.dir", std::string, wm_info.log_dir, "/tmp/");
		LOAD_CONFIG_OPTION("log.enabled", bool,    wm_info.log_enabled, false);
		LOAD_CONFIG_OPTION("temp.trigger", std::string, temp_trig, "");
		LOAD_CONFIG_OPTION("temp.threshold_high", uint32_t, temp_crit, 0);
		LOAD_CONFIG_OPTION("temp.threshold_low", uint32_t, temp_crit_arm, 0);
		LOAD_CONFIG_OPTION("temp.margin", float, temp_margin, 0.05);
		LOAD_CONFIG_OPTION("power.trigger", std::string, power_trig, "");
		LOAD_CONFIG_OPTION("power.threshold_high", uint32_t, power_cons, 150000);
		LOAD_CONFIG_OPTION("power.threshold_low", uint32_t, power_cons_arm, 0);
		LOAD_CONFIG_OPTION("power.margin", float, power_margin, 0.05);
		LOAD_CONFIG_OPTION("nr_threads", uint16_t, nr_threads, 1);
		po::variables_map opts_vm;
		cfm.ParseConfigurationFile(opts_desc, opts_vm);
	}
	catch (boost::program_options::invalid_option_value ex) {
		logger->Error("Errors in configuration file [%s]", ex.what());
	}

	// Create data logging directory
	if (wm_info.log_enabled) {
		try {
			if (!boost::filesystem::exists(wm_info.log_dir))
				boost::filesystem::create_directories(wm_info.log_dir);
			boost::filesystem::perms prms(boost::filesystem::owner_all);
			prms |= boost::filesystem::others_read;
			prms |= boost::filesystem::group_read;
			boost::filesystem::permissions(wm_info.log_dir, prms);
			logger->Info("PowerMonitor: data logging enabled [dir=%s]",
				wm_info.log_dir.c_str());
		}
		catch (std::exception & ex) {
			logger->Error("PowerMonitor: %s: %s",
				ex.what(), wm_info.log_dir.c_str());
		}
	}

#define CMD_WM_DATALOG "datalog"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_WM_DATALOG,
			static_cast<CommandHandler*>(this),
			"Start/stop power monitor data logging");

	bbque::trig::TriggerFactory & tgf(TriggerFactory::GetInstance());
	// Temperature scheduling policy trigger setting
	logger->Debug("Temperature scheduling policy trigger setting");
	triggers[PowerManager::InfoType::TEMPERATURE] = tgf.GetTrigger(temp_trig);
	triggers[PowerManager::InfoType::TEMPERATURE]->threshold_high = temp_crit * 1000;
	triggers[PowerManager::InfoType::TEMPERATURE]->threshold_low = temp_crit_arm * 1000;
	triggers[PowerManager::InfoType::TEMPERATURE]->margin = temp_margin;
#ifdef CONFIG_BBQUE_DM
	triggers[PowerManager::InfoType::TEMPERATURE]->SetActionFunction(
									[&, this]() {
										dm.NotifyUpdate(stat::EVT_RESOURCE);
									});
#endif // CONFIG_BBQUE_DM
	// Power consumption scheduling policy trigger setting
	logger->Debug("Power consumption scheduling policy trigger setting");
	triggers[PowerManager::InfoType::POWER] = tgf.GetTrigger(power_trig);
	triggers[PowerManager::InfoType::POWER]->threshold_high = power_cons;
	triggers[PowerManager::InfoType::POWER]->threshold_low = power_cons_arm;
	triggers[PowerManager::InfoType::POWER]->margin = power_margin;

	logger->Info("=====================================================================");
	logger->Info("| THRESHOLDS             | VALUE       | MARGIN  |      TRIGGER     |");
	logger->Info("+------------------------+-------------+---------+------------------+");
	logger->Info("| Temperature            | %6d C    | %6.0f%%  | %16s |",
		triggers[PowerManager::InfoType::TEMPERATURE]->threshold_high / 1000,
		triggers[PowerManager::InfoType::TEMPERATURE]->margin * 100, temp_trig.c_str());
	logger->Info("| Power consumption      | %6d mW   | %6.0f%%  | %16s |",
		triggers[PowerManager::InfoType::POWER]->threshold_high,
		triggers[PowerManager::InfoType::POWER]->margin * 100, power_trig.c_str());
	logger->Info("=====================================================================");

	// Status of the optimization policy execution request
	opt_request_sent = false;

	//---------- Setup Worker
	Worker::Setup(BBQUE_MODULE_NAME("wm"), POWER_MONITOR_NAMESPACE);
	Worker::Start();
}

void PowerMonitor::Init()
{
	PowerMonitorGet[size_t(PowerManager::InfoType::LOAD)]        =
		&PowerManager::GetLoad;
	PowerMonitorGet[size_t(PowerManager::InfoType::TEMPERATURE)] =
		&PowerManager::GetTemperature;
	PowerMonitorGet[size_t(PowerManager::InfoType::FREQUENCY)]   =
		&PowerManager::GetClockFrequency;
	PowerMonitorGet[size_t(PowerManager::InfoType::POWER)]       =
		&PowerManager::GetPowerUsage;
	PowerMonitorGet[size_t(PowerManager::InfoType::PERF_STATE)]  =
		&PowerManager::GetPerformanceState;
	PowerMonitorGet[size_t(PowerManager::InfoType::POWER_STATE)] =
		&PowerManager::GetPowerState;
}

PowerMonitor::~PowerMonitor()
{
	Stop();
	Worker::Terminate();
}

void PowerMonitor::Task()
{
	logger->Debug("Monitor: waiting for platform to be ready...");
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	ra.WaitForPlatformReady();
	std::vector<std::thread> samplers(nr_threads);

	uint16_t nr_resources_to_monitor = wm_info.resources.size();
	uint16_t nr_resources_per_thread = nr_resources_to_monitor;
	uint16_t nr_resources_left = 0;
	if (nr_resources_to_monitor > nr_threads) {
		nr_resources_per_thread = nr_resources_to_monitor / nr_threads;
		nr_resources_left = nr_resources_to_monitor % nr_threads;
	}
	else
		nr_threads = 1;
	logger->Debug("Monitor: nr_threads=%d nr_resources_to_monitor=%d",
		nr_threads, nr_resources_to_monitor);

	uint16_t nt = 0, last_resource_id = 0;
	for (; nt < nr_threads; ++nt) {
		logger->Debug("Monitor: starting thread %d...", nt);
		last_resource_id += nr_resources_per_thread;
		samplers.push_back(std::thread(
					&PowerMonitor::SampleResourcesStatus, this,
					nt * nr_resources_per_thread,
					last_resource_id));
	}

	// The number of resources is not divisible by the number of threads...
	// --> spawn one more thread
	if (nr_resources_left > 0) {
		logger->Debug("Monitor: starting thread %d [extra]...", nr_threads);
		samplers.push_back(std::thread(
					&PowerMonitor::SampleResourcesStatus, this,
					nr_threads * nr_resources_per_thread,
					nr_resources_to_monitor));
	}

	while (!done)
		Wait();
	std::for_each(samplers.begin(), samplers.end(), std::mem_fn(&std::thread::join));
}

int PowerMonitor::CommandsCb(int argc, char *argv[])
{
	uint8_t cmd_offset = ::strlen(MODULE_NAMESPACE) + 1;
	char * command_id  = argv[0] + cmd_offset;
	logger->Info("CommandsCb: processing command [%s]", command_id);

	// Data logging control
	if (!strncmp(CMD_WM_DATALOG, command_id, strlen(CMD_WM_DATALOG))) {
		if (argc != 2) {
			logger->Error("CommandsCb: command [%s] missing action [start/stop/clear]",
				command_id);
			return 1;
		}
		return DataLogCmdHandler(argv[1]);
	}

	logger->Error("CommandsCb: unknown command [%s]", command_id);
	return -1;
}

PowerMonitor::ExitCode_t PowerMonitor::Register(br::ResourcePathPtr_t rp,
						PowerManager::SamplesArray_t const & samples_window)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	// Register all the resources referenced by the path specified
	auto r_list(ra.GetResources(rp));
	if (r_list.empty()) {
		logger->Warn("Register: no resources to monitor <%s>", rp->ToString().c_str());
		return ExitCode_t::ERR_RSRC_MISSING;
	}

	// Register each resource to monitor, specifying the number of samples to
	// consider in the (exponential) mean computation and the output log file
	// descriptor
	for (auto & rsrc : r_list) {
		rsrc->EnablePowerProfiling(samples_window);
		logger->Info("Register: adding <%s> to power monitoring...",
			rsrc->Path()->ToString().c_str());
		wm_info.resources.push_back( { rsrc->Path(), rsrc});
		wm_info.log_fp.emplace(rsrc->Path(), new std::ofstream());
	}

	return ExitCode_t::OK;
}

PowerMonitor::ExitCode_t PowerMonitor::Register(const std::string & rp_str,
						PowerManager::SamplesArray_t const & samples_window)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	return Register(ra.GetPath(rp_str), samples_window);
}

void PowerMonitor::Start(uint32_t period_ms)
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	logger->Info("Start: logging %d registered resources to monitor:",
		wm_info.resources.size());

	if ((period_ms != 0) && (period_ms != wm_info.period_ms))
		wm_info.period_ms = period_ms;

	for (auto & rh : wm_info.resources) {
		logger->Info("Start: \t<%s>", rh.path->ToString().c_str());
	}

	if (wm_info.started) {
		logger->Warn("Start: power logging already started (T = %d ms)...",
			wm_info.period_ms);
		return;
	}

	logger->Info("Start: starting power logging (T = %d ms)...", wm_info.period_ms);
	events.set(WM_EVENT_UPDATE);
	worker_status_cv.notify_all();
}

void PowerMonitor::Stop()
{
	std::unique_lock<std::mutex> worker_status_ul(worker_status_mtx);
	if (!wm_info.started) {
		logger->Warn("Stop: power logging already stopped");
		return;
	}

	logger->Info("Stop: stopping power logging...");
	events.reset(WM_EVENT_UPDATE);
	worker_status_cv.notify_all();
}

void PowerMonitor::ManageRequest(PowerManager::InfoType info_type,
				 double curr_value)
{
	// Return if optimization request already sent
	if (opt_request_sent) return;

	// Check the required trigger is available
	auto & trigger = triggers[info_type];
	if (trigger == nullptr)
		return;

	// Check and execute the trigger (i.e., the trigger function or the
	// schedule the optimization request)
	//	logger->Debug("ManageRequest: (before) trigger armed: %d",trigger->IsArmed());
	bool request_to_send = trigger->Check(curr_value);
	//	logger->Debug("ManageRequest: (after)  trigger armed: %d",trigger->IsArmed());
	if (request_to_send) {
		logger->Info("ManageRequest: trigger <InfoType: %d> current = %d, threshold = %u [m=%0.f]",
			info_type, curr_value, trigger->threshold_high, trigger->margin);
		auto trigger_func = trigger->GetActionFunction();
		if (trigger_func) {
			trigger_func();
			opt_request_sent = false;
		}
		else {
			opt_request_sent = true;
			optimize_dfr.Schedule(milliseconds(WM_OPT_REQ_TIME_FACTOR * wm_info.period_ms));
		}
	}
}

void PowerMonitor::SendOptimizationRequest()
{
	ResourceManager & rm(ResourceManager::GetInstance());
	rm.NotifyEvent(ResourceManager::BBQ_PLAT);
	logger->Info("Trigger: optimization request sent [generic: %d, battery: %d]",
		opt_request_sent.load(), opt_request_for_battery);
	opt_request_sent = false;
}

void  PowerMonitor::SampleResourcesStatus(uint16_t first_resource_index,
					  uint16_t last_resource_index)
{
	PowerManager::SamplesArray_t samples;
	PowerManager::InfoType info_type;
	uint16_t thd_id = 0;

	if (last_resource_index != first_resource_index)
		thd_id = first_resource_index / (last_resource_index - first_resource_index);
	else
		thd_id = first_resource_index;
	logger->Debug("SampleResourcesStatus: [thread %d] monitoring resources in range [%d, %d)",
		thd_id, first_resource_index, last_resource_index);

	while (!done) {
		if (events.none()) {
			logger->Debug("SampleResourcesStatus: [thread %d] no events to process",
				first_resource_index);
			Wait();
		}
		if (!events.test(WM_EVENT_UPDATE))
			continue;

		// Power status monitoring over all the registered resources
		uint16_t i = first_resource_index;
		for (; i < last_resource_index; ++i) {
			auto const & r_path(wm_info.resources[i].path);
			auto & rsrc(wm_info.resources[i].resource_ptr);

			std::string log_i("<" + rsrc->Path()->ToString() + "> (I): ");
			std::string log_m("<" + rsrc->Path()->ToString() + "> (M): ");
			std::string i_values, m_values;
			uint info_idx   = 0;
			uint info_count = 0;
			logger->Debug("SampleResourcesStatus: [thread %d] monitoring <%s>",
				thd_id, r_path->ToString().c_str());

			for (; info_idx < PowerManager::InfoTypeIndex.size() &&
			info_count < rsrc->GetPowerInfoEnabledCount();
			++info_idx, ++info_count) {
				// Check if the power profile information has been required
				info_type = PowerManager::InfoTypeIndex[info_idx];
				if (rsrc->GetPowerInfoSamplesWindowSize(info_type) <= 0) {
					logger->Warn("SampleResourcesStatus: [thread %d] power profile "
						"not enabled for %s",
						thd_id,
						rsrc->Path()->ToString().c_str());
					continue;
				}

				// Call power manager get function and update the resource
				// descriptor power profile information
				if (PowerMonitorGet[info_idx] == nullptr) {
					logger->Warn("SampleResourcesStatus: [thread %d] power monitoring "
						"or <%s> not available",
						thd_id, PowerManager::InfoTypeStr[info_idx]);
					continue;
				}
				PowerMonitorGet[info_idx](pm, r_path, samples[info_idx]);
				rsrc->UpdatePowerInfo(info_type, samples[info_idx]);

				// Log messages
				BuildLogString(rsrc, info_idx, i_values, m_values);

				// Policy execution trigger (ENERGY is for the battery monitor thread)
				if (info_type != PowerManager::InfoType::ENERGY)
					ExecuteTrigger(rsrc, info_type);
			}

			logger->Debug("SampleResourcesStatus: [thread %d] sampling %s ",
				thd_id, (log_i + i_values).c_str());
			logger->Debug("SampleResourcesStatus: [thread %d] sampling %s ",
				thd_id, (log_m + m_values).c_str());
			if (wm_info.log_enabled) {
				DataLogWrite(r_path, i_values);
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(wm_info.period_ms));
	}
	logger->Notice("SampleResourcesStatus: [thread %d] terminating", thd_id);
}

void PowerMonitor::BuildLogString(br::ResourcePtr_t rsrc,
				  uint info_idx,
				  std::string & inst_values,
				  std::string & mean_values)
{
	auto info_type = PowerManager::InfoTypeIndex[info_idx];

	std::stringstream ss_i;
	ss_i
		<< std::setprecision(0) << std::fixed
		<< std::setw(str_w[info_idx]) << std::left
		<< rsrc->GetPowerInfo(info_type, br::Resource::INSTANT);
	inst_values  += ss_i.str() + " ";

	std::stringstream ss_m;
	ss_m
		<< std::setprecision(str_p[info_idx]) << std::fixed
		<< std::setw(str_w[info_idx]) << std::left
		<< rsrc->GetPowerInfo(info_type, br::Resource::MEAN);
	mean_values += ss_m.str() + " ";
}

/*******************************************************************
 *                        DATA LOGGING                             *
 *******************************************************************/

void PowerMonitor::DataLogWrite(br::ResourcePathPtr_t rp,
				std::string const & data_line,
				std::ios_base::openmode om)
{
	//std::string file_path(BBQUE_WM_DATALOG_PATH "/");
	std::string file_path(wm_info.log_dir + "/");
	file_path.append(rp->ToString());
	file_path.append(".dat");
	logger->Debug("DataLogWrite: writing to file [%s]: %s",
		file_path.c_str(), data_line.c_str());

	// Open file
	wm_info.log_fp[rp]->open(file_path, om);
	if (!wm_info.log_fp[rp]->is_open()) {
		logger->Warn("DataLogWrite: log file not open");
		return;
	}

	// Write data line
	*wm_info.log_fp[rp] << data_line << std::endl;
	if (wm_info.log_fp[rp]->fail()) {
		logger->Error("DataLogWrite: log file write failed [F:%d, B:%d]",
			wm_info.log_fp[rp]->fail(),
			wm_info.log_fp[rp]->bad());
		*wm_info.log_fp[rp] << "Error";
		*wm_info.log_fp[rp] << std::endl;
		return;
	}
	// Close file
	wm_info.log_fp[rp]->close();
}

void PowerMonitor::DataLogClear()
{
	for (auto log_ofs : wm_info.log_fp) {
		DataLogWrite(log_ofs.first, WM_LOGFILE_HEADER, std::ios_base::out);
	}
}

int PowerMonitor::DataLogCmdHandler(const char * arg)
{
	std::string action(arg);
	logger->Info("DataLogCmdHandler: action = [%s]", action.c_str());
	// Start
	if ((action.compare("start") == 0)
	&& (!wm_info.log_enabled)) {
		logger->Info("DataLogCmdHandler: starting data logging...");
		wm_info.log_enabled = true;
		return 0;
	}
	// Stop
	if ((action.compare("stop") == 0)
	&& (wm_info.log_enabled)) {
		logger->Info("DataLogCmdHandler: stopping data logging...");
		wm_info.log_enabled = false;
		return 0;
	}
	// Clear
	if (action.compare("clear") == 0) {
		bool de = wm_info.log_enabled;
		wm_info.log_enabled = false;
		DataLogClear();
		wm_info.log_enabled = de;
		logger->Info("DataLogCmdHandler: clearing data logs...");
		return 0;
	}

	logger->Warn("DataLogCmdHandler: unknown action [%s]",
		action.c_str());
	return -1;
}


} // namespace bbque
