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

#include "bbque/config.h"
#include "bbque/pm/power_manager.h"
#include "bbque/pp/local_platform_proxy.h"
#include "bbque/res/resources.h"
#include "bbque/utils/assert.h"

#ifdef CONFIG_TARGET_LINUX
  #include "bbque/pp/linux_platform_proxy.h"
#elif defined CONFIG_TARGET_ANDROID
  #include "bbque/pp/android_platform_proxy.h"
#elif defined CONFIG_TARGET_EMULATED_HOST
  #include "bbque/pp/test_platform_proxy.h"
#else
  #error No host platform proxy: check target platform dependencies
#endif

#if defined CONFIG_TARGET_LINUX_MANGO
  #include "bbque/pp/mango_platform_proxy.h"
#endif

#if defined CONFIG_TARGET_LINUX_RECIPE
  #include "bbque/pp/recipe_platform_proxy.h"
#endif

#ifdef CONFIG_TARGET_OPENCL
  #include "bbque/pp/opencl_platform_proxy.h"
#endif

#ifdef CONFIG_TARGET_NVIDIA
  #include "bbque/pp/nvml_platform_proxy.h"
#endif

namespace bbque
{
namespace pp
{

LocalPlatformProxy::LocalPlatformProxy()
{
	logger = bu::Logger::GetLogger(PLATFORM_PROXY_NAMESPACE ".local");
	assert(logger);

#ifdef CONFIG_TARGET_EMULATED_HOST
	this->host = std::unique_ptr<TestPlatformProxy>(TestPlatformProxy::GetInstance());
#elif CONFIG_TARGET_LINUX
	this->host = std::unique_ptr<LinuxPlatformProxy>(LinuxPlatformProxy::GetInstance());
#elif CONFIG_TARGET_ANDROID
	this->host = std::unique_ptr<AndroidPlatformProxy>(AndroidPlatformProxy::GetInstance());
#else
#error "No suitable PlatformProxy for host found."
#endif

	bbque_assert(this->host);
	std::string host_proxy(this->host->GetPlatformID());
	logger->Info("LocalPlatformProxy: host = { %s }", host_proxy.c_str());

#ifdef CONFIG_TARGET_LINUX_MANGO
	this->accl.push_back(std::unique_ptr<MangoPlatformProxy>(MangoPlatformProxy::GetInstance()));
#endif

#ifdef CONFIG_TARGET_LINUX_RECIPE
	this->accl.push_back(std::unique_ptr<RecipePlatformProxy>(RecipePlatformProxy::GetInstance()));
#elif CONFIG_TARGET_OPENCL
	this->accl.push_back(std::unique_ptr<OpenCLPlatformProxy>(OpenCLPlatformProxy::GetInstance()));
#endif

#ifdef CONFIG_TARGET_NVIDIA
	this->accl.push_back(std::unique_ptr<NVMLPlatformProxy>(NVMLPlatformProxy::GetInstance()));
#endif

	std::string accl_proxies;
	for (auto & p: this->accl) {
		accl_proxies += p->GetPlatformID() + std::string(", ");
	}
	logger->Info("LocalPlatformProxy: accl = { %s }", accl_proxies.c_str());
}

const char* LocalPlatformProxy::GetPlatformID(int16_t system_id) const
{
#ifdef CONFIG_TARGET_LINUX_MANGO
	return this->accl[0]->GetPlatformID(system_id);
#else
	return this->host->GetPlatformID(system_id);
#endif
}

const char* LocalPlatformProxy::GetHardwareID(int16_t system_id) const
{
#ifdef CONFIG_TARGET_LINUX_MANGO
	return this->accl[0]->GetHardwareID(system_id);
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

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
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

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
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

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
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

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
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
	uint8_t err_count = 0;

	ec = this->host->ReclaimResources(papp);
	if (ec != PLATFORM_OK) {
		err_count++;
	}

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
		ec = (*it)->ReclaimResources(papp);
		if (ec != PLATFORM_OK) {
			err_count++;
		}
	}

	if (err_count == this->accl.size() + 1) {
		logger->Error("ReclaimResources: failed");
		return PLATFORM_MAPPING_FAILED;
	}

	return PLATFORM_OK;
}


LocalPlatformProxy::ExitCode_t LocalPlatformProxy::MapResources(
        SchedPtr_t papp,
        ResourceAssignmentMapPtr_t pres,
        bool excl)
{
	ExitCode_t ec;
	uint8_t err_count = 0;

	ec = this->host->MapResources(papp, pres, excl);
	if (ec != PLATFORM_OK) {
		err_count++;
	}

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
		ec = (*it)->MapResources(papp, pres, excl);
		if (ec != PLATFORM_OK) {
			err_count++;
		}
	}

	if (err_count == this->accl.size() + 1) {
		logger->Error("MapResources: failed");
		return PLATFORM_MAPPING_FAILED;
	}

	return PLATFORM_OK;
}

LocalPlatformProxy::ExitCode_t LocalPlatformProxy::ActuatePowerManagement()
{
	// Platform-specific power settings (system-level configurations)
	ExitCode_t ec = this->host->ActuatePowerManagement();
	if (ec != PLATFORM_OK) {
		return ec;
	}

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
		ec = (*it)->ActuatePowerManagement();
	}
	return PLATFORM_OK;
}

LocalPlatformProxy::ExitCode_t LocalPlatformProxy::ActuatePowerManagement(
        bbque::res::ResourcePtr_t resource)
{

#ifdef CONFIG_BBQUE_PM
	PowerManager & pm(PowerManager::GetInstance());
	logger->Debug("ActuatePowerManagement: looking for pending management actions");

	// Apply the power management actions to local resources
	auto & ps = resource->GetPowerSettings();

	if (ps.PendingActions() | br::Resource::PowerSettings::TURN_ONOFF) {
		logger->Debug("ActuatePowerManagement: <%> set on/off: %d",
		              resource->Path()->ToString().c_str(),
		              ps.IsOnline());
		if (ps.IsOnline())
			pm.SetOn(resource->Path());
		else
			pm.SetOff(resource->Path());
	}

	if (ps.PendingActions() | br::Resource::PowerSettings::CHANGE_GOVERNOR) {
		logger->Debug("ActuatePowerManagement: <%> setting governor '%s'",
		              resource->Path()->ToString().c_str(),
		              ps.FrequencyGovernor().c_str());
		pm.SetClockFrequencyGovernor(
		        resource->Path(), ps.FrequencyGovernor());
	}

	if (ps.PendingActions() | br::Resource::PowerSettings::SET_FREQUENCY) {
		logger->Debug("ActuatePowerManagement: <%> setting frequency: %d KHz",
		              resource->Path()->ToString().c_str(),
		              ps.ClockFrequency());
		pm.SetClockFrequency(resource->Path(), ps.ClockFrequency());
	}

	if (ps.PendingActions() | br::Resource::PowerSettings::SET_PERF_STATE) {
		logger->Debug("ActuatePowerManagement: <%> setting performance state: %d",
		              resource->Path()->ToString().c_str(),
		              ps.PerformanceState());
		pm.SetClockFrequency(resource->Path(), ps.PerformanceState());
	}

	ps.ClearPendingActions();
	logger->Debug("ActuatePowerManagement: <%s> pending actions left: %d",
	              resource->Path()->ToString().c_str(),
	              resource->GetPowerSettings().PendingActions());
#else
	UNUSED(resource);

#endif // CONFIG_BBQUE_PM

	return PLATFORM_OK;
}


void LocalPlatformProxy::Exit()
{
	logger->Info("Exit: closing host platform proxy [%s]...",
	             host->GetPlatformID());
	this->host->Exit();
	for (auto & accl_pp : accl) {
		logger->Info("Exit: closing accliliary platform proxy [%s]...",
		             accl_pp->GetPlatformID());
		accl_pp->Exit();
	}
}


bool LocalPlatformProxy::IsHighPerformance(
        bbque::res::ResourcePathPtr_t const & path) const
{
	if (path->GetID(bbque::res::ResourceType::CPU) >= 0)
		return this->host->IsHighPerformance(path);
	return false;
}


ReliabilityActionsIF::ExitCode_t LocalPlatformProxy::Dump(app::SchedPtr_t psched)
{
	ReliabilityActionsIF::ExitCode_t ec;

	ec = this->host->Dump(psched);
	if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
		return ec;
	}

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
		ec = (*it)->Dump(psched);
		if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
			return ec;
		}
	}
	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t LocalPlatformProxy::Restore(
        uint32_t pid, std::string exec_name)
{
	ReliabilityActionsIF::ExitCode_t ec;

	ec = this->host->Restore(pid, exec_name);
	if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
		return ec;
	}

	// TODO: Need to retrieve the set of tasks of the application, first...

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
		ec = (*it)->Restore(pid);
		if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
			return ec;
		}
	}
	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t LocalPlatformProxy::Freeze(app::SchedPtr_t psched)
{
	ReliabilityActionsIF::ExitCode_t ec;

	ec = this->host->Freeze(psched);
	if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
		return ec;
	}

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
		ec = (*it)->Freeze(psched);
		if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
			return ec;
		}
	}
	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t LocalPlatformProxy::Thaw(app::SchedPtr_t psched)
{
	ReliabilityActionsIF::ExitCode_t ec;

	ec = this->host->Thaw(psched);
	if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
		return ec;
	}

	for (auto it = this->accl.begin() ; it < this->accl.end(); it++) {
		ec = (*it)->Thaw(psched);
		if (ec != ReliabilityActionsIF::ExitCode_t::OK) {
			return ec;
		}
	}
	return ReliabilityActionsIF::ExitCode_t::OK;
}


} // namespace pp
} // namespace bbque
