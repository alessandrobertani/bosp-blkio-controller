#include "bbque/platform_proxy.h"

namespace bbque
{

plugins::PlatformLoaderIF * PlatformProxy::pli = nullptr;

bool PlatformProxy::IsHighPerformance(bbque::res::ResourcePathPtr_t const & path) const
{
	(void) path;
	return false;
}

PlatformProxy::ExitCode_t PlatformProxy::ActuatePowerManagement()
{
	return ExitCode_t::PLATFORM_OK;
}

PlatformProxy::ExitCode_t PlatformProxy::ActuatePowerManagement(
        bbque::res::ResourcePtr_t resource)
{
	(void) resource;
	return ExitCode_t::PLATFORM_OK;
}


#ifndef CONFIG_BBQUE_PIL_LEGACY

const pp::PlatformDescription & PlatformProxy::GetPlatformDescription()
{
	std::unique_ptr<bu::Logger> logger =
	        bu::Logger::GetLogger(PLATFORM_PROXY_NAMESPACE);

	// Check if the plugin is never loaded. In that case load it and parse the
	// platform configuration
	if (pli == NULL) {
		logger->Debug("I'm creating a new instance of PlatformLoader plugin.");
		pli = ModulesFactory::GetModule<plugins::PlatformLoaderIF>(
		              std::string("bq.pl.") + BBQUE_PLOADER_DEFAULT);
		assert(pli);
		if (plugins::PlatformLoaderIF::PL_SUCCESS != pli->loadPlatformInfo()) {
			logger->Fatal("Unable to load platform information.");
			throw std::runtime_error("PlatformLoaderPlugin pli->loadPlatformInfo() failed.");
		} else {
			logger->Info("Platform information loaded successfully.");
		}
	}

	// Return the just or previous loaded configuration
	return pli->getPlatformInfo();
}

#endif

} //namespace bbque
