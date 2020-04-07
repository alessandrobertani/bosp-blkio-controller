/*
 * Copyright (C) 2017  Politecnico di Milano
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

#ifndef BBQUE_LINUX_PLATFORM_PROXY_H
#define BBQUE_LINUX_PLATFORM_PROXY_H

#include "bbque/config.h"
#include "bbque/platform_proxy.h"
#include "bbque/utils/logging/logger.h"

#define LINUX_PP_NAMESPACE "bq.pp.linux"

// The _types.h file must be included after the
// constants define
#include "bbque/pp/linux_platform_proxy_types.h"
#include "bbque/pp/proc_listener.h"

#include <bitset>

#define BBQUE_PP_LINUX_PLATFORM_ID "bq.linux"

namespace bbque {
namespace pp {

class LinuxPlatformProxy : public PlatformProxy
{
public:

	static LinuxPlatformProxy * GetInstance();

	virtual ~LinuxPlatformProxy();

	/**
	 * @brief Linux specific resource setup interface.
	 */
	ExitCode_t Setup(SchedPtr_t papp) noexcept override final;

	/**
	 * @brief Linux specific resources enumeration
	 *
	 * The default implementation of this method loads the TPD, is such a
	 * function has been enabled
	 */
	ExitCode_t LoadPlatformData() noexcept override final;

	/**
	 * @brief Linux specific resources refresh
	 */
	ExitCode_t Refresh() noexcept override final;

	/**
	 * @brief Linux specific resources release interface.
	 */
	ExitCode_t Release(SchedPtr_t papp) override final;

	/**
	 * @brief Linux specific resource claiming interface.
	 */
	ExitCode_t ReclaimResources(SchedPtr_t papp) noexcept override final;

	/**
	 * @brief Linux specific resource binding interface.
	 */
	ExitCode_t MapResources(
				SchedPtr_t papp, ResourceAssignmentMapPtr_t pres, bool excl) noexcept override final;

	/**
	 * @brief Linux platform specific termination.
	 */
	void Exit() override;


	bool IsHighPerformance(bbque::res::ResourcePathPtr_t const & path) const override;

#ifdef CONFIG_BBQUE_RELIABILITY

	ReliabilityActionsIF::ExitCode_t Dump(app::SchedPtr_t psched) override;

	ReliabilityActionsIF::ExitCode_t Restore(uint32_t pid, std::string exe_name) override;

	ReliabilityActionsIF::ExitCode_t Freeze(app::SchedPtr_t psched) override;

	ReliabilityActionsIF::ExitCode_t Thaw(app::SchedPtr_t papp) override;

#endif

private:
	//-------------------- CONSTS
	/**
	 * @brief Default MAX number of CPUs per socket
	 */
	const int MaxCpusCount = BBQUE_MAX_R_ID_NUM + 1;

	/**
	 * @brief Default MAX number of MEMs node per host
	 */
	const int MaxMemsCount = BBQUE_MAX_R_ID_NUM + 1;

	//-------------------- ATTRIBUTES
	/**
	 * @brief the control group controller
	 *
	 * This is a reference to the controller used on a generic Linux host.
	 * So far we use the "cpuset" controller.
	 */
	const char *controller;

	bool refreshMode;

	int cfs_margin_pct = 0; /**< CFS bandwidth enforcement safety margin (default: 0%) */
	int cfs_threshold_pct = 100; /**< CFS bandwidth enforcement threshold (default: 100%)   */

	std::unique_ptr<bu::Logger> logger;

	/**
	 * @brief The "silos" CGroup
	 *
	 * The "silos" is a control group where are placed processe which have
	 * been scheduled. This CGroup is indended to be a resource constrained
	 * group which grants a bare minimun of resources for the controlling
	 * application to run the RTLib
	 */
	CGroupDataPtr_t psilos;


#ifdef CONFIG_BBQUE_RELIABILITY
	/**
	 * @brief Initialize the support for freezing, checkpoint, restore actions
	 */
	void InitReliabilitySupport();
#endif


#ifdef CONFIG_TARGET_ARM_BIG_LITTLE
	/**
	 * @brief ARM big.LITTLE support: type of each CPU core
	 *
	 * If true, indicates that the related CPU cores is an
	 * high-performance one.
	 */
	std::array<bool, BBQUE_TARGET_CPU_CORES_NUM> high_perf_cores = {
		{false}
	};

	void InitCoresType();
#endif

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH

	NetworkInfo_t network_info;
	void InitNetworkManagement();

	ExitCode_t MakeQDisk(int if_index);
	ExitCode_t MakeCLS(int if_index);
	ExitCode_t SetCGNetworkBandwidth(SchedPtr_t papp, CGroupDataPtr_t pcgd,
					ResourceAssignmentMapPtr_t pres,
					RLinuxBindingsPtr_t prlb);
	ExitCode_t MakeNetClass(AppPid_t handle, unsigned rate, int if_index);

	static ExitCode_t HTBParseOpt(struct nlmsghdr *n);
	static ExitCode_t HTBParseClassOpt(unsigned rate, struct nlmsghdr *n);
	static ExitCode_t CGParseOpt(long handle, struct nlmsghdr *n);
#endif

	uint64_t GetNetIFBandwidth(const std::string &ifname) const;

	std::string memory_ids_all;

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	ProcessListener & proc_listener;
#endif

	//-------------------- METHODS

	LinuxPlatformProxy();


	void InitPowerInfo(const char * resourcePath, BBQUE_RID_TYPE core_id);

	/**
	 * @brief Load values from the configuration file
	 */
	void LoadConfiguration() noexcept;


	/**
	 * @brief Resources Mapping and Assigment to Applications
	 */
	ExitCode_t GetResourceMapping(
				SchedPtr_t papp, ResourceAssignmentMapPtr_t assign_map,
				RLinuxBindingsPtr_t prlb,
				BBQUE_RID_TYPE node_id,
				br::RViewToken_t rvt) noexcept;


	ExitCode_t ScanPlatformDescription() noexcept;
	ExitCode_t RegisterCPU(const PlatformDescription::CPU &cpu, bool is_local = true) noexcept;
	ExitCode_t RegisterMEM(const PlatformDescription::Memory &mem, bool is_local = true) noexcept;
	ExitCode_t RegisterNET(const PlatformDescription::NetworkIF &net, bool is_local = true) noexcept;
	ExitCode_t RegisterIODev(const PlatformDescription::IO &io_dev, bool is_local = true) noexcept;

	// --- CGroup-releated methods

	/**
	 * @brief Load the libcgroup and initialize the internal representation
	 */
	ExitCode_t InitCGroups() noexcept;

	/**
	 * @brief Load the silos for minimal resource allocation
	 */
	ExitCode_t BuildSilosCG(CGroupDataPtr_t &pcgd) noexcept;

	ExitCode_t BuildCGroup(CGroupDataPtr_t &pcgd) noexcept;
	ExitCode_t GetCGroupData(ba::SchedPtr_t papp, CGroupDataPtr_t &pcgd) noexcept;
	ExitCode_t SetupCGroup(CGroupDataPtr_t &pcgd, RLinuxBindingsPtr_t prlb,
			bool excl = false, bool move = true) noexcept;
	ExitCode_t BuildAppCG(SchedPtr_t papp, CGroupDataPtr_t &pcgd) noexcept;
};

} // namespace pp
} // namespace bbque

#endif // LINUX_PLATFORM_PROXY_H
