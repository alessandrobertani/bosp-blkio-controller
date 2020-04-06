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

#ifndef BBQUE_TEST_PLATFORM_PROXY_H
#define BBQUE_TEST_PLATFORM_PROXY_H

#include "bbque/platform_proxy.h"

#define BBQUE_TEST_PP_NAMESPACE     "bq.pp.test"
#define BBQUE_PP_TEST_PLATFORM_ID   "bq.test"
#define BBQUE_PP_TEST_HARDWARE_ID   "emulated"

namespace bbque {
namespace pp {

/**
 * @class TestPlatformProxy
 *
 * @brief Platform Proxy for the Simulated mode
 */
class TestPlatformProxy : public PlatformProxy
{
public:

	static TestPlatformProxy * GetInstance();

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

	TestPlatformProxy();

	ExitCode_t RegisterCPU(const PlatformDescription::CPU &cpu);

	ExitCode_t RegisterMEM(const PlatformDescription::Memory &mem);

	ExitCode_t RegisterIODev(const PlatformDescription::IO &io_dev);

	/**
	 * @brief The logger used by the worker thread
	 */
	std::unique_ptr<bu::Logger> logger;

	bool platformLoaded = false;
};

}
}
#endif // BBQUE_TEST_PLATFORM_PROXY_H
