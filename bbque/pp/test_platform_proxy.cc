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

#include "bbque/pp/test_platform_proxy.h"

namespace bbque {
namespace pp {

TestPlatformProxy * TestPlatformProxy::GetInstance()
{
	static TestPlatformProxy * instance;
	if (instance == nullptr)
		instance = new TestPlatformProxy();
	return instance;
}

TestPlatformProxy::TestPlatformProxy()
{
	this->logger = bu::Logger::GetLogger(BBQUE_TEST_PP_NAMESPACE);
	this->platform_id = BBQUE_PP_TEST_PLATFORM_ID;
	this->hardware_id = BBQUE_PP_TEST_HARDWARE_ID;
}

TestPlatformProxy::ExitCode_t TestPlatformProxy::Setup(SchedPtr_t papp)
{
	logger->Info("Setup: %s", papp->StrId());
	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t TestPlatformProxy::LoadPlatformData()
{
	logger->Info("LoadPlatformData...");
	if (platformLoaded)
		return PLATFORM_OK;

	logger->Warn("Loading TEST platform data");

	const PlatformDescription *pd;
	try {
		pd = &this->GetPlatformDescription();
	}
	catch (const std::runtime_error& e) {
		UNUSED(e);
		logger->Fatal("Unable to get the PlatformDescription object");
		return PLATFORM_LOADING_FAILED;
	}

	for (const auto & sys_entry : pd->GetSystemsAll()) {
		auto & sys = sys_entry.second;
		logger->Debug("[%s@%s] Scanning the CPUs...",
			sys.GetHostname().c_str(), sys.GetNetAddress().c_str());
		for (const auto cpu : sys.GetCPUsAll()) {
			ExitCode_t result = this->RegisterCPU(cpu);
			if (BBQUE_UNLIKELY(PLATFORM_OK != result)) {
				logger->Fatal("Register CPU %d failed", cpu.GetId());
				return result;
			}
		}
		logger->Debug("[%s@%s] Scanning the memories...",
			sys.GetHostname().c_str(), sys.GetNetAddress().c_str());
		for (const auto mem : sys.GetMemoriesAll()) {
			ExitCode_t result = this->RegisterMEM(*mem);
			if (BBQUE_UNLIKELY(PLATFORM_OK != result)) {
				logger->Fatal("Register MEM %d failed", mem->GetId());
				return result;
			}

			if (sys.IsLocal()) {
				logger->Debug("[%s@%s] is local",
					sys.GetHostname().c_str(), sys.GetNetAddress().c_str());
			}
		}

		logger->Debug("ScanPlatformDescription: [%s@%s] IO storages...",
			sys.GetHostname().c_str(),
			sys.GetNetAddress().c_str());
		for (const auto storage : sys.GetStoragesAll()) {
			ExitCode_t result = this->RegisterIODev(*storage);
			if (BBQUE_UNLIKELY(PLATFORM_OK != result)) {
				logger->Fatal("ScanPlatformDescription: STORAGE %d "
					"registration failed",
					storage->GetId());
				return result;
			}
		}
	}

	platformLoaded = true;

	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t
TestPlatformProxy::RegisterCPU(const PlatformDescription::CPU &cpu)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	for (const auto pe : cpu.GetProcessingElementsAll()) {
		auto pe_type = pe.GetPartitionType();
		if (PlatformDescription::MDEV == pe_type ||
		PlatformDescription::SHARED == pe_type) {

			const std::string resource_path = pe.GetPath();
			const int share = pe.GetShare();

			if (ra.RegisterResource(resource_path, "", share) == nullptr)
				return PLATFORM_DATA_PARSING_ERROR;
			logger->Debug("Registration of <%s>: %d", resource_path.c_str(), share);
		}
	}

	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t
TestPlatformProxy::RegisterMEM(const PlatformDescription::Memory &mem)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	std::string resource_path(mem.GetPath());
	const auto q_bytes = mem.GetQuantity();

	if (ra.RegisterResource(resource_path, "", q_bytes)  == nullptr)
		return PLATFORM_DATA_PARSING_ERROR;

	logger->Debug("Registration of <%s> %d bytes done",
		resource_path.c_str(), q_bytes);

	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t
TestPlatformProxy::RegisterIODev(const PlatformDescription::IO &io_dev)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	std::string resource_path(io_dev.GetPath());
	const auto q_bytes = io_dev.GetBandwidth();

	if (ra.RegisterResource(resource_path, "", q_bytes)  == nullptr)
		return PLATFORM_DATA_PARSING_ERROR;

	logger->Debug("Registration of <%s> %d bytes done",
		resource_path.c_str(), q_bytes);

	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t TestPlatformProxy::Refresh()
{
	logger->Info("Refresh...");
	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t TestPlatformProxy::Release(SchedPtr_t papp)
{
	logger->Info("Release: %s", papp->StrId());
	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t TestPlatformProxy::ReclaimResources(SchedPtr_t papp)
{
	logger->Info("ReclaimResources: %s", papp->StrId());
	return PLATFORM_OK;
}

TestPlatformProxy::ExitCode_t
TestPlatformProxy::MapResources(SchedPtr_t papp,
				ResourceAssignmentMapPtr_t pres,
				bool excl)
{
	(void) pres;
	(void) excl;
	logger->Info(" MapResources: %s", papp->StrId());
	return PLATFORM_OK;
}

void TestPlatformProxy::Exit()
{
	logger->Info("Exit: Termination...");
}

bool
TestPlatformProxy::IsHighPerformance(bbque::res::ResourcePathPtr_t const & path) const
{
	UNUSED(path);
	return true;
}


}	// namespace pp
}	// namespace bbque
