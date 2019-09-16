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

#include "bbque/pp/local_platform_proxy.h"
#include "bbque/pp/test_platform_proxy.h"
#include "bbque/config.h"
#include "bbque/res/resource_path.h"
#include "bbque/utils/assert.h"

#ifdef CONFIG_TARGET_LINUX
#include "bbque/pp/linux_platform_proxy.h"
#elif defined CONFIG_TARGET_ANDROID
#include "bbque/pp/android_platform_proxy.h"
#elif defined CONFIG_TARGET_LINUX_MANGO
#include "bbque/pp/mango_platform_proxy.h"
#else
#warning LocalPlatformProxy cannot load any platform proxy
#endif

#ifdef CONFIG_BBQUE_OPENCL
#include "bbque/pp/opencl_platform_proxy.h"
#endif


namespace bbque
{
namespace pp
{

LocalPlatformProxy::LocalPlatformProxy()
{
	logger = bu::Logger::GetLogger(PLATFORM_PROXY_NAMESPACE ".local");
	assert(logger);

#ifdef CONFIG_BBQUE_TEST_PLATFORM_DATA
	this->host = std::unique_ptr<TestPlatformProxy>(TestPlatformProxy::GetInstance());
#elif defined CONFIG_TARGET_LINUX_MANGO
	this->host = std::unique_ptr<TestPlatformProxy>(TestPlatformProxy::GetInstance());
#elif defined CONFIG_TARGET_LINUX
	this->host = std::unique_ptr<LinuxPlatformProxy>(LinuxPlatformProxy::GetInstance());
#elif defined CONFIG_TARGET_ANDROID
	this->host = std::unique_ptr<AndroidPlatformProxy>(AndroidPlatformProxy::GetInstance());
	//this->host = std::unique_ptr<TestPlatformProxy>(TestPlatformProxy()::GetInstance());
#else
#error "No suitable PlatformProxy for host found."
#endif

#ifdef CONFIG_TARGET_LINUX_MANGO
	this->aux.push_back(std::unique_ptr<MangoPlatformProxy>(MangoPlatformProxy::GetInstance()));
#endif

#ifdef CONFIG_BBQUE_OPENCL
	this->aux.push_back(std::unique_ptr<OpenCLPlatformProxy>(OpenCLPlatformProxy::GetInstance()));
#endif

	bbque_assert(this->host);
}

const char* LocalPlatformProxy::GetPlatformID(int16_t system_id) const
{
#ifdef CONFIG_TARGET_LINUX_MANGO
	return this->aux[0]->GetPlatformID(system_id);
#else
	return this->host->GetPlatformID(system_id);
#endif
}

const char* LocalPlatformProxy::GetHardwareID(int16_t system_id) const
{
#ifdef CONFIG_TARGET_LINUX_MANGO
	return this->aux[0]->GetHardwareID(system_id);
#else
	return this->host->GetHardwareID(system_id);
#endif
}

LocalPlatformProxy::ExitCode_t LocalPlatformProxy::Setup(SchedPtr_t papp)
{
	ExitCode_t ec;

	// Obviously, at least a PE in the cpu must be provided to application
	// so we have to call the host PP (Linux, Android, etc.)
	ec = this->host->Setup(papp);
	if (ec != PLATFORM_OK) {
		return ec;
	}

	for (auto it = this->aux.begin() ; it < this->aux.end(); it++) {
		ec = (*it)->Setup(papp);
		if (ec != PLATFORM_OK) {
			return ec;
		}
	}

	return PLATFORM_OK;
}


LocalPlatformProxy::ExitCode_t LocalPlatformProxy::LoadPlatformData()
{
	ExitCode_t ec;

	ec = this->host->LoadPlatformData();
	if (ec != PLATFORM_OK) {
		return ec;
	}

	for (auto it = this->aux.begin() ; it < this->aux.end(); it++) {
		ec = (*it)->LoadPlatformData();
		if (ec != PLATFORM_OK) {
			return ec;
		}
	}

	return PLATFORM_OK;
}


LocalPlatformProxy::ExitCode_t LocalPlatformProxy::Refresh()
{
	ExitCode_t ec;

	ec = this->host->Refresh();
	if (ec != PLATFORM_OK) {
		return ec;
	}

	for (auto it = this->aux.begin() ; it < this->aux.end(); it++) {
		ec = (*it)->Refresh();
		if (ec != PLATFORM_OK) {
			return ec;
		}
	}

	return PLATFORM_OK;
}


LocalPlatformProxy::ExitCode_t LocalPlatformProxy::Release(SchedPtr_t papp)
{
	ExitCode_t ec;

	ec = this->host->Release(papp);
	if (ec != PLATFORM_OK) {
		return ec;
	}

	for (auto it = this->aux.begin() ; it < this->aux.end(); it++) {
		ec = (*it)->Release(papp);
		if (ec != PLATFORM_OK) {
			return ec;
		}
	}

	return PLATFORM_OK;
}


LocalPlatformProxy::ExitCode_t LocalPlatformProxy::ReclaimResources(SchedPtr_t papp)
{
	ExitCode_t ec;

	ec = this->host->ReclaimResources(papp);
	if (ec != PLATFORM_OK) {
		return ec;
	}

	for (auto it = this->aux.begin() ; it < this->aux.end(); it++) {
		ec = (*it)->ReclaimResources(papp);
		if (ec != PLATFORM_OK) {
			return ec;
		}
	}

	return PLATFORM_OK;
}


LocalPlatformProxy::ExitCode_t LocalPlatformProxy::MapResources(
        SchedPtr_t papp,
        ResourceAssignmentMapPtr_t pres,
        bool excl)
{
	ExitCode_t ec;

	ec = this->host->MapResources(papp, pres, excl);
	if (ec != PLATFORM_OK) {
		return ec;
	}

	for (auto it = this->aux.begin() ; it < this->aux.end(); it++) {
		ec = (*it)->MapResources(papp, pres, excl);
		if (ec != PLATFORM_OK) {
			return ec;
		}
	}

	return PLATFORM_OK;
}



void LocalPlatformProxy::Exit()
{
	this->host->Exit();
	for (auto it = this->aux.begin() ; it < this->aux.end(); it++)
		(*it)->Exit();
}


bool LocalPlatformProxy::IsHighPerformance(
        bbque::res::ResourcePathPtr_t const & path) const
{
	if (path->GetID(bbque::res::ResourceType::CPU) >= 0)
		return this->host->IsHighPerformance(path);
	return false;
}


} // namespace pp
} // namespace bbque
