#ifndef TEST_PLATFORM_PROXY_H
#define TEST_PLATFORM_PROXY_H

#include "bbque/platform_proxy.h"

#define TEST_PP_NAMESPACE "bq.pp.test"

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
	 * @brief Return the Platform specific string identifier
	 */
	virtual const char* GetPlatformID(int16_t system_id=-1) const override;

	/**
	 * @brief Return the Hardware identifier string
	 */
	virtual const char* GetHardwareID(int16_t system_id=-1) const override;
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
	virtual ExitCode_t MapResources(
		SchedPtr_t papp, ResourceAssignmentMapPtr_t pres, bool excl = true) override;

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

	bool platformLoaded=false;
};

}
}
#endif // TEST_PLATFORM_PROXY_H
