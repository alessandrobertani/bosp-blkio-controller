/*
 * Copyright (C) 2019  Politecnico di Milano
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

#ifndef ANDROID_PLATFORM_PROXY_H
#define ANDROID_PLATFORM_PROXY_H

#include "bbque/platform_proxy.h"
#include "bbque/pm/power_manager.h"
#include "bbque/pp/proc_listener.h"

#define ANDROID_PP_NAMESPACE "bq.pp.android"

namespace bbque {
namespace pp {

/**
 * @class AndroidPlatformProxy
 *
 * @brief Platform Proxy for the Simulated mode
 */
class AndroidPlatformProxy : public PlatformProxy
{
public:

	static AndroidPlatformProxy * GetInstance();

	/**
	 * @brief Platform specific resource setup interface.
	 */
	virtual ExitCode_t Setup(SchedPtr_t papp) override;

	/**
	 * @brief Platform specific resources enumeration
	 *
	 * The default implementation of this method loads the TPD, is such a
	 * function has been enabled
	 */
	virtual ExitCode_t LoadPlatformData() override;

	/**
	 * @brief Platform specific resources refresh
	 */
	virtual ExitCode_t Refresh() override;

	/**
	 * @brief Platform specific resources release interface.
	 */
	virtual ExitCode_t Release(SchedPtr_t papp) override;

	/**
	 * @brief Platform specific resource claiming interface.
	 */
	virtual ExitCode_t ReclaimResources(SchedPtr_t papp) override;

	/**
	 * @brief Platform specific resource binding interface.
	 */
	virtual ExitCode_t MapResources(SchedPtr_t papp,
					ResourceAssignmentMapPtr_t pres,
					bool excl = true) override;

	/**
	 * @brief Test platform specific termination.
	 */
	virtual void Exit();


	virtual bool IsHighPerformance(
				bbque::res::ResourcePathPtr_t const & path) const override;

private:

#ifdef CONFIG_BBQUE_LINUX_PROC_LISTENER
	ProcessListener & proc_listener;
#endif


#ifdef CONFIG_BBQUE_PM
	PowerManager & pm;
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

	AndroidPlatformProxy();

	ExitCode_t RegisterCPU(const PlatformDescription::CPU &cpu);

	ExitCode_t RegisterMEM(const PlatformDescription::Memory &mem);

	/**
	 * @brief The logger used by the worker thread
	 */
	std::unique_ptr<bu::Logger> logger;

	bool platformLoaded = false;
};

}
}
#endif // ANDROID_PLATFORM_PROXY_H
