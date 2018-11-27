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

#ifndef BBQUE_POWER_MANAGER_CPU_H_
#define BBQUE_POWER_MANAGER_CPU_H_

#include <map>
#include <vector>

#include "bbque/pm/power_manager.h"
#include "bbque/res/resources.h"

#define BBQUE_LINUX_SYS_CPU_PREFIX   "/sys/devices/system/cpu/cpu"

#ifndef CONFIG_BBQUE_PM_NOACPI
  #define BBQUE_LINUX_SYS_CPU_THERMAL  "/sys/devices/platform/coretemp.0/temp"
#else
  #define BBQUE_LINUX_SYS_CPU_THERMAL  "/sys/bus/platform/drivers/coretemp/coretemp.0/hwmon/hwmon"
#endif

using namespace bbque::res;

namespace bbque {

/**
 * @class CPUPowerManager
 *
 * Provide generic power management API related to CPUs, by extending @ref
 * PowerManager class.
 */
class CPUPowerManager: public PowerManager {

public:

	enum class ExitStatus {
		/** Successful call */
		OK = 0,
		/** A not specified error code */
		ERR_GENERIC
	};

	/**
	 * @see class PowerManager
	 */
	CPUPowerManager();

	virtual ~CPUPowerManager();

	/**
	 * @see class PowerManager
	 */
	PMResult GetLoad(ResourcePathPtr_t const & rp, uint32_t & perc);

	/**
	 * @see class PowerManager
	 */
	PMResult GetTemperature(ResourcePathPtr_t const & rp, uint32_t &celsius);


	/* ===========  Frequency management =========== */

	/**
	 * @see class PowerManager
	 */
	PMResult GetClockFrequency(ResourcePathPtr_t const & rp, uint32_t &khz);

	/**
	 * @see class PowerManager
	 */
	PMResult SetClockFrequency(ResourcePathPtr_t const & rp, uint32_t khz);
	PMResult SetClockFrequencyBoundaries(
			int pe_id, uint32_t khz_min, uint32_t khz_max);

	/**
	 * @see class PowerManager
	 */
	PMResult SetClockFrequency(
			ResourcePathPtr_t const & rp,
			uint32_t khz_min,
			uint32_t khz_max);



	/**
	 * @see class PowerManager
	 */
	PMResult GetClockFrequencyInfo(
			br::ResourcePathPtr_t const & rp,
			uint32_t &khz_min,
			uint32_t &khz_max,
			uint32_t &khz_step);

	PMResult GetClockFrequencyInfo(
		int pe_id, uint32_t & khz_min, uint32_t & khz_max);
	
	/**
	 * @see class PowerManager
	 */
	PMResult GetAvailableFrequencies(
			ResourcePathPtr_t const & rp, std::vector<uint32_t> &freqs);
	PMResult GetAvailableFrequencies(
			int pe_id, std::vector<uint32_t> &freqs);

	/**
	 * @see class PowerManager
	 */
	std::vector<std::string> const & GetAvailableFrequencyGovernors(
			br::ResourcePathPtr_t const & rp) {
		(void) rp;
		return cpufreq_governors;
	}

	PMResult GetClockFrequencyGovernor(
			br::ResourcePathPtr_t const & rp,
			std::string & governor);

	PMResult SetClockFrequencyGovernor(
			br::ResourcePathPtr_t const & rp,
			std::string const & governor);

	PMResult SetClockFrequencyGovernor(
			int pe_id,
			std::string const & governor);

	/**  On/off status */

	PMResult SetOn(br::ResourcePathPtr_t const & rp);

	PMResult SetOff(br::ResourcePathPtr_t const & rp);

	bool IsOn(br::ResourcePathPtr_t const & rp) const;


	/* ===========   Power consumption  =========== */

	PMResult GetPowerUsage(
			br::ResourcePathPtr_t const & rp, uint32_t & mwatt) {
		(void) rp;
		mwatt = 0;
		return PMResult::ERR_API_NOT_SUPPORTED;
	}

	/* ===========   Performance/power states  =========== */

	/**
	 * @brief Change the current CPU performance state
	 *
	 * This function has been implemented in order to provide an abstract
	 * interface to the scheduling policy, such that it is possibile to get
	 * the current frequency as the related position of the value in the
	 * 'core_freqs' vector.
	 *
	 * @note it requires the cpufreq governor to be set to 'userspace' or
	 * similar
	 */
	PMResult GetPerformanceState(br::ResourcePathPtr_t const & rp, uint32_t &value);

	/**
	 * @brief Get the number CPU performance states available
	 *
	 * This function returns the number of supported frequency values.
	 *
	 * @note it requires the cpufreq governor to be set to 'userspace' or
	 * similar
	 */
	PMResult GetPerformanceStatesCount(br::ResourcePathPtr_t const & rp, uint32_t &count);

	/**
	 * @brief Change the current CPU performance state
	 *
	 * This function has been implemented in order to provide an abstract
	 * interface to the scheduling policy, such that it is possibile to set
	 * a frequency withouth knowing the exact value. The idea is to provide
	 * an integer index as argument which corresponds to the position of the
	 * frequency value in the related 'core_freqs' vector.
	 *
	 * @note it requires the cpufreq governor to be set to 'userspace' or
	 * similar
	 */
	PMResult SetPerformanceState(br::ResourcePathPtr_t const & rp, uint32_t value);

protected:

	/*** Mapping processing elements / CPU cores */
	std::map<int,int> core_ids;

	/*** Mapping system CPU cores to thermal sensors path */
	std::map<int, std::shared_ptr<std::string>> core_therms;

	/*** Available clock frequencies for each processing element (core) */
	std::map<int, std::shared_ptr<std::vector<uint32_t>> > core_freqs;

	/*** Original clock frequencies, to be restored when bbque stops */
	std::map<int, std::string> cpufreq_restore;

	/*** SysFS CPU prefix path ***/
	std::string prefix_sys_cpu;

	/**
	 * @struct LoadInfo
	 * @brief Save the information of a single /proc/stat sampling
	 * (processor activity in 'jitters')
	 */
	struct LoadInfo {
		int32_t total = 0;
		int32_t idle  = 0;
	};


	void InitCoreIdMapping();

	void InitTemperatureSensors(std::string const & prefix_coretemp);

	void InitFrequencyGovernors();


	PMResult GetTemperaturePerCore(int pe_id, uint32_t & celsius);

	void _GetAvailableFrequencies(int cpu_id, std::shared_ptr<std::vector<uint32_t>> v);

	/**
	 *  Sample CPU activity samples from /proc/stat
	 */
	PMResult GetLoadCPU(BBQUE_RID_TYPE cpu_core_id, uint32_t & load) const;

	/**
	 *  Sample CPU activity samples from /proc/stat
	 */
	ExitStatus GetLoadInfo(LoadInfo *info, BBQUE_RID_TYPE cpu_core_id) const;

	/**
	 *  Set Cpufreq scaling governor for PE pe_id
	 */
	PMResult GetClockFrequencyGovernor(int pe_id, std::string & governor);
};

}

#endif // BBQUE_POWER_MANAGER_CPU_H_
