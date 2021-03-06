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

#include "bbque/configuration_manager.h"
#include "bbque/resource_accounter.h"
#include "bbque/pm/power_manager_cpu.h"
#include "bbque/res/resource_path.h"
#include "bbque/utils/iofs.h"
#include "bbque/utils/string_utils.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <boost/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#define LOAD_SAMPLING_INTERVAL_SECONDS 1
#define LOAD_SAMPLING_NUMBER 1

#define PROCSTAT_FIRST  1
#define PROCSTAT_LAST   10

#define PROCSTAT_IDLE   4
#define PROCSTAT_IOWAIT 5

#define TEMP_SENSOR_FIRST_ID 1
#define TEMP_SENSOR_STEP_ID  1


#define GET_PROC_ELEMENT_ID(rp, pe_id) \
	pe_id = rp->GetID(br::ResourceType::PROC_ELEMENT);


namespace po = boost::program_options;
namespace bu = bbque::utils;

namespace bbque {

CPUPowerManager::CPUPowerManager() :
    prefix_sys_cpu(BBQUE_LINUX_SYS_CPU_PREFIX)
{
	ConfigurationManager & cfm(ConfigurationManager::GetInstance());

	// Core ID <--> Processing Element ID mapping
	InitCoreIdMapping();

	// Thermal monitoring initialization:
	// Get the per-socket thermal monitor directory
	std::string prefix_coretemp;
	po::variables_map opts_vm;
	po::options_description opts_desc("PowerManager socket options");
	opts_desc.add_options()(
		"PowerManager.temp.sockets",
		po::value<std::string>(&prefix_coretemp)->default_value(
									BBQUE_LINUX_SYS_CPU_THERMAL),
		"The directory exporting thermal status information");
	cfm.ParseConfigurationFile(opts_desc, opts_vm);

	std::list<std::string> tsensor_dirs;
	bu::SplitString(prefix_coretemp, tsensor_dirs, ":");
	logger->Info("CPUPowerManager: CPU sockets found = %d",
		tsensor_dirs.size());

#ifndef CONFIG_TARGET_ODROID_XU
	for (auto & ts_dir : tsensor_dirs) {
		InitTemperatureSensors(ts_dir + "/temp");
	}
#endif
	// Thermal sensors
	if (core_therms.empty()) {
		logger->Warn("CPUPowerManager: no thermal monitoring available. ");
		logger->Warn("\tCheck the configuration file [etc/bbque/bbque.conf]");
	}

	// Parse the available frequency governors
	InitFrequencyGovernors();

	// Initial settings: userspace governor
	auto ret = InitCPUFreq();
	if (ret != PowerManager::PMResult::OK) {
		logger->Error("CPUPowerManager: cpufreq initialization failed");
	}

	// Check Intel RAPL support availability
	std::ifstream ifs_r(BBQUE_LINUX_INTEL_RAPL_PREFIX"/enabled");
	if (ifs_r.is_open()) {
		char rapl_enabled_status;
		ifs_r.get(rapl_enabled_status);
		if (rapl_enabled_status == '1') {
			this->is_rapl_supported = true;
		}
		ifs_r.close();
	}
	logger->Info("CPUPowerManager: Intel RAPL available: %s",
		this->is_rapl_supported ? "YES" : "NO");
}

CPUPowerManager::~CPUPowerManager()
{
	for (auto pe_id_info : online_restore) {
		logger->Info("Restoring PE %d online status: %d",
			pe_id_info.first, pe_id_info.second);
		if (pe_id_info.second) {
			SetOn(pe_id_info.first);
		}
		else {
			SetOff(pe_id_info.first);
		}
	}

	for (auto pe_id_info : cpufreq_restore) {
		if (core_freqs[pe_id_info.first]->empty())
			continue;
		logger->Info("Restoring PE %d cpufreq bound: [%u - %u] kHz",
			pe_id_info.first,
			core_freqs[pe_id_info.first]->front(),
			core_freqs[pe_id_info.first]->back());
		SetClockFrequencyBoundaries(pe_id_info.first,
					core_freqs[pe_id_info.first]->front(),
					core_freqs[pe_id_info.first]->back());

		logger->Info("Restoring PE %d cpufreq governor: %s",
			pe_id_info.first, pe_id_info.second.c_str());
		SetClockFrequencyGovernor(pe_id_info.first, pe_id_info.second);
	}

	cpufreq_restore.clear();
	phy_core_ids.clear();
	core_therms.clear();
	core_freqs.clear();
	cpufreq_governors.clear();
	core_online.clear();
	online_restore.clear();
}

void CPUPowerManager::InitCoreIdMapping()
{
	bu::IoFs::ExitCode_t result;
	int cpu_id = 0;
	int pe_id  = 0;
	int core_id_bound[2]; // core_id_bound[0]=min_id, core_id_bound[1]=max_id
	// CPU <--> Core id mapping:
	// CPU is commonly used to reference the cores while in the BarbequeRTRM
	// 'core' is referenced as 'processing element', thus it needs a unique id
	// number. For instance in a SMT Intel 4-core:
	// -----------------------------------------------------------------------
	// CPU/HWt  Cores
	// 0        0
	// 1        0
	// 2        1
	// 3        1
	// ------------------------------------------------------------------------
	// Therefore we consider "processing element" what Linux calls CPU and "cpu"
	// what Linux calls "Core"
	//-------------------------------------------------------------------------

	std::string core_av_filepath(
				BBQUE_LINUX_SYS_CORE_PREFIX + std::string("/present"));

	// Taking the min and max pe_id available
	std::string core_id_range;
	result = bu::IoFs::ReadValueFrom(core_av_filepath, core_id_range);
	logger->Info("InitCoreIdMapping: core id range: %s",
		core_id_range.c_str());
	if (result != bu::IoFs::OK) {
		logger->Error("InitCoreIdMapping: failed while reading %s",
			core_av_filepath.c_str());
		return;
	}

	int i = 0;
	while (core_id_range.size() > 1) {
		std::string curr_id(br::ResourcePathUtils::SplitAndPop(core_id_range, "-"));
		logger->Info("InitCoreIdMapping: %d) core id = %s", i, curr_id.c_str());
		core_id_bound[i] = std::stoi(curr_id);
		i++;
	}

	pe_id = core_id_bound[0];
	while (pe_id <= core_id_bound[1]) {

		// Online status per core
		online_restore[pe_id] = IsOn(pe_id);
		core_online[pe_id] = online_restore[pe_id];

#ifdef CONFIG_TARGET_ANDROID
		// CoreID not supported
#else
		std::vector<int> siblings(2);
		if (core_online[pe_id]) {
			std::string core_sibl_filepath(
						BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
						"/topology/core_siblings_list");
			logger->Debug("InitCoreIdMapping: reading... %s",
				core_sibl_filepath.c_str());

			// Taking the min and max pe_id available
			std::string core_siblings_range;
			result = bu::IoFs::ReadValueFrom(
							core_sibl_filepath, core_siblings_range);
			logger->Debug("InitCoreIdMapping: core %d siblings: %s ",
				pe_id, core_siblings_range.c_str());
			if (result != bu::IoFs::OK) {
				logger->Error("InitCoreIdMapping: failed while reading %s",
					core_sibl_filepath.c_str());
				break;
			}

			i = 0;
			while (core_siblings_range.size() > 1) {
				std::string curr_core_id(
							br::ResourcePathUtils::SplitAndPop(
											core_siblings_range, "-"));
				siblings[i] = std::stoi(curr_core_id);
				logger->Debug("InitCoreIdMapping: pe_id=<%d>"
					" sibling bound[%d]=%d",
					pe_id, i, siblings[i]);
				i++;
			}

			std::string core_id_filepath(
						BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
						"/topology/core_id");
			logger->Debug("InitCoreIdMapping: reading... %s",
				core_id_filepath.c_str());

			// Processing element id <-> (physical) CPU id
			result = bu::IoFs::ReadIntValueFrom<int>(
				core_id_filepath, cpu_id);
			logger->Debug("InitCoreIdMapping: cpu id=%d", cpu_id);
			if (result != bu::IoFs::OK) {
				logger->Error("InitCoreIdMapping: failed while reading %s",
					core_id_filepath.c_str());
				break;
			}

			phy_core_ids[pe_id] = cpu_id;
			logger->Debug("InitCoreIdMapping: pe_id=%d -> "
				"physical cpu id=%d",
				pe_id, cpu_id);
		}
#endif
		// Available frequencies per core
		core_freqs[pe_id] = std::make_shared<std::vector < uint32_t >> ();
		_GetAvailableFrequencies(pe_id, core_freqs[pe_id]);
		if (!core_freqs[pe_id]->empty()) {
			logger->Info("InitCoreIdMapping: <sys.cpu%d.pe%d>: "
				"%d available frequencies",
				cpu_id, pe_id, core_freqs[pe_id]->size());
		}

		std::string scaling_curr_governor;
		GetClockFrequencyGovernor(pe_id, scaling_curr_governor);
		cpufreq_restore[pe_id] = scaling_curr_governor;

		++pe_id;
	}
}

void CPUPowerManager::InitTemperatureSensors(std::string const & prefix_coretemp)
{
	int cpu_id = 0;
	char str_value[8];
	int sensor_id = TEMP_SENSOR_FIRST_ID;
	bu::IoFs::ExitCode_t result = bu::IoFs::OK;

	for ( ; result == bu::IoFs::OK; sensor_id += TEMP_SENSOR_STEP_ID) {
		std::string therm_file(
				prefix_coretemp + std::to_string(sensor_id) +
				"_label");

		logger->Debug("Thermal sensors @[%s]", therm_file.c_str());
		result = bu::IoFs::ReadValueFrom(therm_file, str_value, 8);
		if (result != bu::IoFs::OK) {
			logger->Debug("Failed while reading '%s'", therm_file.c_str());
			break;
		}

		// Look for the label containing the core ID required
		std::string core_label(str_value);
		if (core_label.find("Core") != 0)
			continue;

		cpu_id = std::stoi(core_label.substr(5));
		core_therms[cpu_id] = std::make_shared<std::string>(
			prefix_coretemp + std::to_string(sensor_id) +
			"_input");
		logger->Info("Thermal sensors for CPU %d @[%s]",
			cpu_id, core_therms[cpu_id]->c_str());
	}
}

void CPUPowerManager::InitFrequencyGovernors()
{
	bu::IoFs::ExitCode_t result;
	std::string govs;
	std::string cpufreq_path(prefix_sys_cpu +
				"0/cpufreq/scaling_available_governors");
	result = bu::IoFs::ReadValueFrom(cpufreq_path, govs);
	if (result != bu::IoFs::OK) {
		logger->Error("InitFrequencyGovernors: error reading: %s",
			cpufreq_path.c_str());
		return;
	}

	logger->Info("InitFrequencyGovernors: ");
	while (govs.size() > 1)
		cpufreq_governors.push_back(
					br::ResourcePathUtils::SplitAndPop(govs, " "));
	for (std::string & g : cpufreq_governors)
		logger->Info("---> %s", g.c_str());
}

PowerManager::PMResult
CPUPowerManager::InitCPUFreq()
{
	PowerManager::PMResult result;

	for (auto pe_id_info : cpufreq_restore) {
		if (core_freqs[pe_id_info.first]->empty())
			continue;
		logger->Notice("InitCPUFreq: <pe%d> cpufreq range: [%u - %u] KHz",
			pe_id_info.first,
			core_freqs[pe_id_info.first]->front(),
			core_freqs[pe_id_info.first]->back());
		result = SetClockFrequencyBoundaries(pe_id_info.first,
						core_freqs[pe_id_info.first]->front(),
						core_freqs[pe_id_info.first]->back());
		if (result != PowerManager::PMResult::OK)
			return result;

		logger->Notice("InitCPUFreq: <pe%d> cpufreq governor: userspace",
			pe_id_info.first);
		result = SetClockFrequencyGovernor(pe_id_info.first, "userspace");

		if (result != PowerManager::PMResult::OK) {
			logger->Error("InitCPUFreq: <pe%d> cannot set "
				"'userspace' governor ", pe_id_info.first);
			return result;
		}
	}

	return PowerManager::PMResult::OK;
}

/**********************************************************************
 * Load                                                               *
 **********************************************************************/

CPUPowerManager::ExitStatus
CPUPowerManager::GetLoadInfo(CPUPowerManager::LoadInfo * info,
			     BBQUE_RID_TYPE cpu_core_id) const
{
	// Information about kernel activity is available in the /proc/stat
	// file. All the values are aggregated since the system first booted.
	// Thus, to compute the load, the variation of these values in a little
	//  and constant timespan has to be computed.
	std::ifstream procstat_info("/proc/stat");
	std::string   procstat_entry;

	// The information about CPU-N can be found in the line whose sintax
	// follows the pattern:
	// 	cpun x y z w ...
	// Check the Linux documentation to find information about those values
	boost::regex cpu_info_stats("cpu" + std::to_string(cpu_core_id) +
				" (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+)" +
				" (\\d+) (\\d+) (\\d+) (\\d+)");

	// Parsing the /proc/stat file to find the correct line
	bool found = false;
	while (std::getline(procstat_info, procstat_entry)) {
		boost::smatch match;
		// If that's the correct line
		if (!boost::regex_match(procstat_entry, match, cpu_info_stats))
			continue;
		found = true;

		// CPU core total time
		for (int i = PROCSTAT_FIRST; i <= PROCSTAT_LAST; ++i)
			info->total += boost::lexical_cast<int32_t>(match[i]);
		// CPU core idle time
		for (int i = PROCSTAT_IDLE; i <= PROCSTAT_IOWAIT; ++i)
			info->idle += boost::lexical_cast<int32_t>(match[i]);
	}

	if (!found) return CPUPowerManager::ExitStatus::ERR_GENERIC;
	return CPUPowerManager::ExitStatus::OK;
}

PowerManager::PMResult
CPUPowerManager::GetLoad(ResourcePathPtr_t const & rp,
			 uint32_t & perc)
{
	PMResult result;
	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	// Extract the single CPU core (PE) id from the resource path
	// (e.g., "cpu2.pe3", pe_id = 3)
	BBQUE_RID_TYPE pe_id = rp->GetID(br::ResourceType::PROC_ELEMENT);
	if (pe_id >= 0) {
		result = GetLoadCPU(pe_id, perc);
		if (result != PMResult::OK) return result;
	}
	else {
		// Multiple CPU cores (e.g., "cpu2.pe")
		uint32_t pe_load = 0;
		perc = 0;

		// Cumulate the load of each core
		br::ResourcePtrList_t const & r_list(ra.GetResources(rp));
		for (ResourcePtr_t rsrc : r_list) {
			result = GetLoadCPU(rsrc->ID(), pe_load);
			if (result != PMResult::OK) return result;
			perc += pe_load;
		}

		// Return the average
		perc /= r_list.size();
	}
	return PowerManager::PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::GetLoadCPU(BBQUE_RID_TYPE cpu_core_id,
			    uint32_t & load) const
{
	CPUPowerManager::ExitStatus result;
	CPUPowerManager::LoadInfo start_info, end_info;

	// Getting the load of a specified CPU. This is possible by reading the
	// /proc/stat file exposed by Linux. To compute the load, a minimum of two
	// samples are needed. In fact, the measure is obtained computing the
	// variation of the file content between two consecutive accesses.
	for (int i = 0; i < LOAD_SAMPLING_NUMBER; ++i) {
		// Performing the actual accesses, with an interval of
		// LOAD_SAMPLING_INTERVAL_SECONDS (circa)
		result = GetLoadInfo(&start_info, cpu_core_id);
		if (result != ExitStatus::OK) {
			logger->Error("No activity info on CPU core %d", cpu_core_id);
			return PMResult::ERR_INFO_NOT_SUPPORTED;
		}

		sleep(LOAD_SAMPLING_INTERVAL_SECONDS);
		result = GetLoadInfo(&end_info, cpu_core_id);
		if (result != ExitStatus::OK) {
			logger->Error("No activity info on CPU core %d", cpu_core_id);
			return PMResult::ERR_INFO_NOT_SUPPORTED;
		}

		// Usage is computed as 1 - idle_time[%]
		float usage =
			100 - (100 * (float)(end_info.idle - start_info.idle) /
			(float)(end_info.total - start_info.total));
		// If the usage is very low and LOAD_SAMPLING_INTERVAL_SECONDS
		// is very little, the usage could become negative, because
		// both the computing and the contents of /proc/stat are not
		// so accurate
		if (usage < 0) usage = 0;
		load = static_cast<uint32_t>(usage);
	}

	return PowerManager::PMResult::OK;
}

/**********************************************************************
 * Temperature                                                        *
 **********************************************************************/

PowerManager::PMResult
CPUPowerManager::GetTemperature(ResourcePathPtr_t const & rp,
				uint32_t & celsius)
{
	PMResult result;

	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);

	// Single CPU core (PE)
	if (pe_id >= 0) {
		logger->Debug("GetTemperature: <%s> references to a single core",
			rp->ToString().c_str());
		return GetTemperaturePerCore(pe_id, celsius);
	}

	// Mean over multiple CPU cores
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	ResourcePtrList_t procs_list(ra.GetResources(rp));

	uint32_t temp_cumulate = 0;
	uint32_t temp_per_core = 0;
	for (auto & proc_ptr : procs_list) {
		result = GetTemperaturePerCore(proc_ptr->ID(), temp_per_core);
		if (result == PMResult::OK) {
			temp_cumulate += temp_per_core;
		}
	}

	celsius = temp_cumulate / procs_list.size();

	return PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::GetTemperaturePerCore(int pe_id, uint32_t & celsius)
{
	bu::IoFs::ExitCode_t io_result;
	celsius = 0;

	// We may have the same sensor for more than one processing element, the
	// sensor is referenced at "core" level
	int phy_core_id = phy_core_ids[pe_id];
	if (core_therms[phy_core_id] == nullptr) {
		logger->Debug("GetTemperaturePerCore: sensor for <pe%d> not available", pe_id);
		return PMResult::ERR_INFO_NOT_SUPPORTED;
	}

	io_result = bu::IoFs::ReadIntValueFrom<uint32_t>(
		core_therms[phy_core_id]->c_str(), celsius);
	if (io_result != bu::IoFs::OK) {
		logger->Error("GetTemperaturePerCore: cannot read <pe%d> temperature", pe_id);
		return PMResult::ERR_SENSORS_ERROR;
	}

	if (celsius > 1000)
		celsius = celsius / 1000; // on Linux the temperature is reported in mC

	logger->Debug("GetTemperaturePerCore: <pe%d> = %d C", pe_id, celsius);
	return PMResult::OK;
}

int64_t CPUPowerManager::StartEnergyMonitor(br::ResourcePathPtr_t const & rp)
{
	if (!this->is_rapl_supported) {
		logger->Warn("StartEnergyMonitor: Intel RAPL not available");
		return -1;
	}

	auto init_value = GetEnergyFromIntelRAPL(rp);
	if (init_value == 0) {
		logger->Error("StartEnergyMonitor: error while reading energy value");
		return -2;
	}

	std::lock_guard<std::mutex>(this->mutex_energy);
	this->energy_start_values[rp] = init_value;
	logger->Info("StartEnergyMonitor: <%s> init_value=%ld",
		rp->ToString().c_str(), init_value);

	return init_value;
}

uint64_t CPUPowerManager::StopEnergyMonitor(br::ResourcePathPtr_t const & rp)
{
	std::lock_guard<std::mutex>(this->mutex_energy);

	auto curr_energy_value = GetEnergyFromIntelRAPL(rp);
	uint64_t energy_diff_value = curr_energy_value - this->energy_start_values[rp];
	logger->Info("StopEnergyMonitor: <%s> consumption=%llu [uJ]",
		rp->ToString().c_str(), energy_diff_value);

	return energy_diff_value;
}

uint64_t CPUPowerManager::GetEnergyFromIntelRAPL(br::ResourcePathPtr_t const & rp)
{
	auto package_id = rp->GetID(br::ResourceType::CPU);
	if (package_id < 0) {
		logger->Error("StartEnergyMonitor: not CPU id in the resource path");
		return 0;
	}

	unsigned int domain_id;
	if (rp->Type() == br::ResourceType::PROC_ELEMENT)
		// Core
		domain_id = 0;
	else if (rp->Type() == br::ResourceType::MEMORY)
		// DRAM
		domain_id = 2;
	else
		// Uncore
		domain_id = 1;
	logger->Debug("StartEnergyMonitor: <%s> -> package_id=%d domain_id=%d",
		rp->ToString().c_str(), package_id, domain_id);

	std::string rapl_path(BBQUE_LINUX_INTEL_RAPL_PREFIX"/intel-rapl:");
	rapl_path += std::to_string(package_id) + "/intel-rapl:";
	rapl_path += std::to_string(package_id) + ":";
	rapl_path += std::to_string(domain_id) + "/energy_uj";
	logger->Debug("StartEnergyMonitor: <%s> -> %s",
		rp->ToString().c_str(), rapl_path.c_str());

	std::ifstream ifs(rapl_path);
	if (!ifs.is_open()) {
		logger->Error("StartEnergyMonitor: cannot open <%s>",
			rapl_path.c_str());
		return 0;
	}

	uint64_t curr_energy_uj;
	ifs >> curr_energy_uj;
	ifs.close();

	return curr_energy_uj;
}

/**********************************************************************
 * Clock frequency management                                         *
 **********************************************************************/

PowerManager::PMResult
CPUPowerManager::GetClockFrequency(ResourcePathPtr_t const & rp,
				   uint32_t & khz)
{
	bu::IoFs::ExitCode_t result;
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);
	if (pe_id < 0) {
		logger->Warn("<%s> does not reference a valid processing element",
			rp->ToString().c_str());
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;
	}

	// Getting the frequency value
	result = bu::IoFs::ReadIntValueFrom<uint32_t>(
		BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
		"/cpufreq/scaling_cur_freq",
		khz);
	if (result != bu::IoFs::OK) {
		logger->Warn("Cannot read current frequency for %s",
			rp->ToString().c_str());
		return PMResult::ERR_SENSORS_ERROR;
	}

	return PowerManager::PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::SetClockFrequency(ResourcePathPtr_t const & rp, uint32_t khz)
{
	bu::IoFs::ExitCode_t result;
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);
	if (pe_id < 0) {
		logger->Warn("<%s> does not reference a valid processing element",
			rp->ToString().c_str());
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;
	}

	logger->Debug("SetClockFrequency: <%s> (cpu%d) set to %d KHz",
		rp->ToString().c_str(), pe_id, khz);

	result = bu::IoFs::WriteValueTo<uint32_t>(
		BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
		"/cpufreq/scaling_setspeed", khz);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PMResult::ERR_SENSORS_ERROR;

	return PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::SetClockFrequency(ResourcePathPtr_t const & rp,
				   uint32_t khz_min,
				   uint32_t khz_max)
{

	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);
	if (pe_id < 0) {
		logger->Warn("<%s> does not reference a valid processing element",
			rp->ToString().c_str());
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;
	}

	logger->Error("SetClockFrequency: <%s> (cpu%d) set to range [%d, %d] KHz",
		rp->ToString().c_str(), pe_id, khz_min, khz_max);


	return SetClockFrequencyBoundaries(pe_id, khz_min, khz_max);
}

PowerManager::PMResult
CPUPowerManager::SetClockFrequencyBoundaries(int pe_id,
					     uint32_t khz_min,
					     uint32_t khz_max)
{
	uint32_t cur_khz_max, cur_khz_min;

	bu::IoFs::ExitCode_t result;
	if (pe_id < 0) {
		logger->Warn("Frequency setting not available for PE %d",
			pe_id);
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;
	}

	if (khz_min > khz_max) {
		uint32_t khz_tmp = khz_max;
		khz_max = khz_min;
		khz_min = khz_tmp;
	}

	GetClockFrequencyInfo(pe_id, cur_khz_min, cur_khz_max);

	if (khz_min > cur_khz_max) {
		logger->Warn("Frequency setting [%d,%d]", khz_min, khz_max);
		result = bu::IoFs::WriteValueTo<uint32_t>(
			BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
			"/cpufreq/scaling_max_freq", khz_max);
		if (result != bu::IoFs::ExitCode_t::OK)
			return PMResult::ERR_SENSORS_ERROR;

		result = bu::IoFs::WriteValueTo<uint32_t>(
			BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
			"/cpufreq/scaling_min_freq", khz_min);
		if (result != bu::IoFs::ExitCode_t::OK)
			return PMResult::ERR_SENSORS_ERROR;

	}
	else {

		result = bu::IoFs::WriteValueTo<uint32_t>(
			BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
			"/cpufreq/scaling_min_freq", khz_min);
		if (result != bu::IoFs::ExitCode_t::OK)
			return PMResult::ERR_SENSORS_ERROR;

		result = bu::IoFs::WriteValueTo<uint32_t>(
			BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
			"/cpufreq/scaling_max_freq", khz_max);
		if (result != bu::IoFs::ExitCode_t::OK)
			return PMResult::ERR_SENSORS_ERROR;

	}

	return PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::GetClockFrequencyInfo(int pe_id,
				       uint32_t & khz_min,
				       uint32_t & khz_max)
{

	// Max and min frequency values
	auto edges = std::minmax_element(
					core_freqs[pe_id]->begin(), core_freqs[pe_id]->end());
	khz_min  = core_freqs[pe_id]->at(edges.first  - core_freqs[pe_id]->begin());
	khz_max  = core_freqs[pe_id]->at(edges.second - core_freqs[pe_id]->begin());

	return PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::GetClockFrequencyInfo(br::ResourcePathPtr_t const & rp,
				       uint32_t & khz_min,
				       uint32_t & khz_max,
				       uint32_t & khz_step)
{
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);

	if (pe_id < 0) {
		logger->Warn("<%s> does not reference a valid processing element",
			rp->ToString().c_str());
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;
	}

	// '0' to represent not fixed step value
	khz_step = 0;

	return GetClockFrequencyInfo(pe_id, khz_min, khz_max);
}

PowerManager::PMResult
CPUPowerManager::GetAvailableFrequencies(ResourcePathPtr_t const & rp,
					 std::vector<uint32_t> & freqs)
{
	// Extracting the selected CPU from the resource path. -1 if error
	int pe_id = rp->GetID(br::ResourceType::PROC_ELEMENT);

	if (pe_id < 0)
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;

	// Extracting available frequencies
	if (core_freqs[pe_id] == nullptr) {
		logger->Warn("GetAvailableFrequencies: <pe=%d> frequencies list missing",
			pe_id);
		return PowerManager::PMResult::ERR_INFO_NOT_SUPPORTED;
	}
	freqs = *(core_freqs[pe_id]);

	return PowerManager::PMResult::OK;
}

void
CPUPowerManager::_GetAvailableFrequencies(int pe_id,
					  std::shared_ptr<std::vector < uint32_t >> cpu_freqs)
{
	bu::IoFs::ExitCode_t result;
	std::string sysfs_path(BBQUE_LINUX_SYS_CPU_PREFIX + std::to_string(pe_id) +
			"/cpufreq/scaling_available_frequencies");

	// Extracting available frequencies string
	std::string cpu_available_freqs;
	result = bu::IoFs::ReadValueFrom(sysfs_path, cpu_available_freqs);
	logger->Debug("%s: { %s }", sysfs_path.c_str(), cpu_available_freqs.c_str());
	if (result != bu::IoFs::OK) {
		logger->Warn("GetAvailableFrequencies: <pe=%d> frequency list not available",
			pe_id);
		return;
	}

	logger->Debug("GetAvailableFrequencies: <pe=%d> { %s }",
		pe_id, cpu_available_freqs.c_str());

	// Fill the vector with the integer frequency values
	std::list<uint32_t> cpu_freqs_unsrt;
	while (cpu_available_freqs.size() > 1) {
		std::string freq(
				br::ResourcePathUtils::SplitAndPop(cpu_available_freqs, " "));
		try {
			uint32_t freq_value = std::stoi(freq);
			cpu_freqs_unsrt.push_back(freq_value);
		}
		catch (std::invalid_argument & ia) {
		}
	}

	// Sort the list of frequency in ascending order
	cpu_freqs_unsrt.sort();
	for (auto f : cpu_freqs_unsrt)
		cpu_freqs->push_back(f);
}

/**********************************************************************
 * Clock frequency governors                                          *
 **********************************************************************/

PowerManager::PMResult
CPUPowerManager::GetClockFrequencyGovernor(br::ResourcePathPtr_t const & rp,
					   std::string & governor)
{
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);
	if (pe_id < 0) {
		logger->Warn("<%s> does not reference a valid processing element",
			rp->ToString().c_str());
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;
	}

	return GetClockFrequencyGovernor(pe_id, governor);
}

PowerManager::PMResult
CPUPowerManager::GetClockFrequencyGovernor(int pe_id,
					   std::string & governor)
{
	bu::IoFs::ExitCode_t result;
	char gov[12];
	std::string cpufreq_path(prefix_sys_cpu + std::to_string(pe_id) +
				"/cpufreq/scaling_governor");

	result = bu::IoFs::ReadValueFrom(cpufreq_path, gov, 12);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;

	governor.assign(gov);
	return PowerManager::PMResult::OK;

}

PowerManager::PMResult
CPUPowerManager::SetClockFrequencyGovernor(br::ResourcePathPtr_t const & rp,
					   std::string const & governor)
{
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);
	if (pe_id < 0) {
		logger->Warn("<%s> does not reference a valid processing element",
			rp->ToString().c_str());
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;
	}

	return SetClockFrequencyGovernor(pe_id, governor);
}

PowerManager::PMResult
CPUPowerManager::SetClockFrequencyGovernor(int pe_id,
					   std::string const & governor)
{

	bu::IoFs::ExitCode_t result;

	std::string cpufreq_path(prefix_sys_cpu + std::to_string(pe_id) +
				"/cpufreq/scaling_governor");
	result = bu::IoFs::WriteValueTo<std::string>(cpufreq_path, governor);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;

	logger->Debug("SetGovernor: '%s' > %s",
		governor.c_str(), cpufreq_path.c_str());
	return PowerManager::PMResult::OK;
}

PowerManager::PMResult CPUPowerManager::SetOn(br::ResourcePathPtr_t const & rp)
{
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);

	return SetOn(pe_id);
}

PowerManager::PMResult CPUPowerManager::SetOn(int pe_id)
{
	bu::IoFs::ExitCode_t result;
	std::string online_path(prefix_sys_cpu + std::to_string(pe_id) +
				"/online");

	result = bu::IoFs::WriteValueTo<int>(online_path, 1);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;

	core_online[pe_id] = true;

	logger->Debug("SetOn: '1' > %s", online_path.c_str());

	return PowerManager::PMResult::OK;
}

PowerManager::PMResult CPUPowerManager::SetOff(br::ResourcePathPtr_t const & rp)
{
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);

	return SetOff(pe_id);
}

PowerManager::PMResult CPUPowerManager::SetOff(int pe_id)
{
	bu::IoFs::ExitCode_t result;
	std::string online_path(prefix_sys_cpu + std::to_string(pe_id) +
				"/online");

	result = bu::IoFs::WriteValueTo<int>(online_path, 0);
	if (result != bu::IoFs::ExitCode_t::OK)
		return PowerManager::PMResult::ERR_RSRC_INVALID_PATH;

	core_online[pe_id] = false;

	logger->Debug("SetOff: '0' > %s", online_path.c_str());

	return PowerManager::PMResult::OK;
}

bool CPUPowerManager::IsOn(br::ResourcePathPtr_t const & rp) const
{
	int pe_id;
	GET_PROC_ELEMENT_ID(rp, pe_id);

	return IsOn(pe_id);
}

bool CPUPowerManager::IsOn(int pe_id) const
{
	bu::IoFs::ExitCode_t result;
	int online;

	std::string online_path(prefix_sys_cpu + std::to_string(pe_id) +
				"/online");

	result = bu::IoFs::ReadIntValueFrom<int>(online_path, online);
	if (result != bu::IoFs::ExitCode_t::OK)
		return false;

	logger->Debug("IsOn: <%s> = %d", online_path.c_str(), online);
	return (online == 1);
}

/**********************************************************************
 * Performance states                                                 *
 **********************************************************************/

PowerManager::PMResult
CPUPowerManager::GetPerformanceState(br::ResourcePathPtr_t const & rp,
				     uint32_t & value)
{
	PowerManager::PMResult result;

	uint32_t curr_freq;
	result = GetClockFrequency(rp, curr_freq);
	if (result != PMResult::OK)
		return result;

	std::vector<uint32_t> freqs;
	result = GetAvailableFrequencies(rp, freqs);
	if (result != PMResult::OK)
		return result;

	int curr_state = 0;
	for (auto f : freqs) {
		if (f == curr_freq) {
			value = curr_state;
			break;
		}
		else
			curr_state++;
	}

	logger->Debug("<%s> current performance state: %d",
		rp->ToString().c_str(), value);
	return PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::GetPerformanceStatesCount(br::ResourcePathPtr_t const & rp,
					   uint32_t & count)
{
	PowerManager::PMResult result;
	std::vector<uint32_t> freqs;

	result = GetAvailableFrequencies(rp, freqs);
	if (result != PMResult::OK)
		return result;

	count = (uint32_t) freqs.size();
	return PMResult::OK;
}

PowerManager::PMResult
CPUPowerManager::SetPerformanceState(br::ResourcePathPtr_t const & rp,
				     uint32_t value)
{
	PowerManager::PMResult result;
	std::vector<uint32_t> freqs;

	result = GetAvailableFrequencies(rp, freqs);
	if (result != PMResult::OK)
		return result;

	if (value >= freqs.size()) {
		logger->Error("<%s> unsupported performance state value: %d",
			rp->ToString().c_str(), value);
		return PMResult::ERR_API_INVALID_VALUE;
	}

	result = SetClockFrequency(rp, freqs[value]);
	if (result != PMResult::OK)
		return result;

	logger->Info("<%s> performance state set: %d:%d",
		rp->ToString().c_str(), value, freqs[value]);

	return PMResult::OK;
}

} // namespace bbque
