#include "bbque/pp/android_platform_proxy.h"

namespace bbque {
namespace pp {


AndroidPlatformProxy * AndroidPlatformProxy::GetInstance() {
	static AndroidPlatformProxy * instance;
	if (instance == nullptr)
		instance = new AndroidPlatformProxy();
	return instance;
}


AndroidPlatformProxy::AndroidPlatformProxy()
#ifdef CONFIG_BBQUE_PM
	:
	pm(PowerManager::GetInstance())
#endif
	this->logger = bu::Logger::GetLogger(ANDROID_PP_NAMESPACE);
	assert(logger);

#ifdef CONFIG_TARGET_ARM_BIG_LITTLE
	InitCoresType();
#endif
}

#ifdef CONFIG_TARGET_ARM_BIG_LITTLE

void AndroidPlatformProxy::InitCoresType() {
	std::stringstream ss;
	ss.str(BBQUE_BIG_LITTLE_HP);
	std::string first_core_str, last_core_str;

	size_t sep_pos = ss.str().find_first_of("-", 0);
	first_core_str = ss.str().substr(0, sep_pos);
	uint16_t first, last;
	if (!first_core_str.empty()) {
		first = std::stoi(first_core_str);
		logger->Debug("InitCoresType: first big core: %d", first);
		last_core_str = ss.str().substr(++sep_pos, std::string::npos);
		last = std::stoi(last_core_str);
		logger->Debug("InitCoresType: last big core: %d", last);
	}

	BBQUE_RID_TYPE core_id;
	for (core_id = first; core_id <= last; ++core_id) {
		logger->Debug("InitCoresType: %d is high-performance", core_id);
		high_perf_cores[core_id] = true;
	}
/*
	while (std::getline(ss, core_str, ',')) {


	}
*/
}

#endif

bool AndroidPlatformProxy::IsHighPerformance(
		bbque::res::ResourcePathPtr_t const & path) const {
#ifdef CONFIG_TARGET_ARM_BIG_LITTLE

	BBQUE_RID_TYPE core_id =
		path->GetID(bbque::res::ResourceType::PROC_ELEMENT);
	if (core_id >= 0 && core_id <= BBQUE_TARGET_CPU_CORES_NUM) {
		logger->Debug("IsHighPerformance: <%s> = %d",
				path->ToString().c_str(), high_perf_cores[core_id]);
		return high_perf_cores[core_id];
	}
	logger->Error("IsHighPerformance: cannot find process element ID in <%s>",
			path->ToString().c_str());
#else
	(void) path;
#endif
	return false;
}

const char* AndroidPlatformProxy::GetPlatformID(int16_t system_id) const {
	(void) system_id;
	return "android";
}

const char* AndroidPlatformProxy::GetHardwareID(int16_t system_id) const {
	(void) system_id;
	return "device";
}

AndroidPlatformProxy::ExitCode_t AndroidPlatformProxy::Setup(AppPtr_t papp) {
	logger->Info("Setup: %s", papp->StrId());
	return PLATFORM_OK;
}

AndroidPlatformProxy::ExitCode_t AndroidPlatformProxy::LoadPlatformData() {
	logger->Info("LoadPlatformData...");
	if (platformLoaded)
		return PLATFORM_OK;

	logger->Warn("Loading DEVICE platform data");

	const PlatformDescription *pd;
	try {
		pd = &this->GetPlatformDescription();
	}
	catch(const std::runtime_error& e) {
		UNUSED(e);
		logger->Fatal("Unable to get the PlatformDescription object");
		return PLATFORM_LOADING_FAILED;
	}

	for (const auto & sys_entry: pd->GetSystemsAll()) {
		auto & sys = sys_entry.second;
		logger->Debug("[%s@%s] Scanning the CPUs...",
				sys.GetHostname().c_str(), sys.GetNetAddress().c_str());
		for (const auto cpu : sys.GetCPUsAll()) {
			ExitCode_t result = this->RegisterCPU(cpu);
			if (unlikely(PLATFORM_OK != result)) {
				logger->Fatal("Register CPU %d failed", cpu.GetId());
				return result;
			}
		}
		logger->Debug("[%s@%s] Scanning the memories...",
				sys.GetHostname().c_str(), sys.GetNetAddress().c_str());
		for (const auto mem : sys.GetMemoriesAll()) {
			ExitCode_t result = this->RegisterMEM(*mem);
			if (unlikely(PLATFORM_OK != result)) {
				logger->Fatal("Register MEM %d failed", mem->GetId());
				return result;
			}

			if (sys.IsLocal()) {
				logger->Debug("[%s@%s] is local",
						sys.GetHostname().c_str(), sys.GetNetAddress().c_str());
			}
		}
	}

	platformLoaded = true;

	return PLATFORM_OK;
}

AndroidPlatformProxy::ExitCode_t
AndroidPlatformProxy::RegisterCPU(const PlatformDescription::CPU &cpu) {
	ResourceAccounter &ra(ResourceAccounter::GetInstance());

	for (const auto pe: cpu.GetProcessingElementsAll()) {
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


AndroidPlatformProxy::ExitCode_t
AndroidPlatformProxy::RegisterMEM(const PlatformDescription::Memory &mem) {
	ResourceAccounter &ra(ResourceAccounter::GetInstance());

	std::string resource_path(mem.GetPath());
	const auto q_bytes = mem.GetQuantity();

	if (ra.RegisterResource(resource_path, "", q_bytes)  == nullptr)
				return PLATFORM_DATA_PARSING_ERROR;

	logger->Debug("Registration of <%s> %d bytes done",
			resource_path.c_str(), q_bytes);

	return PLATFORM_OK;
}


AndroidPlatformProxy::ExitCode_t AndroidPlatformProxy::Refresh() {
	logger->Info("Refresh...");
	// TODO implementing as LinuxPP
	return PLATFORM_OK;
}

AndroidPlatformProxy::ExitCode_t AndroidPlatformProxy::Release(AppPtr_t papp) {
	logger->Info("Release: %s", papp->StrId());
	return PLATFORM_OK;
}

AndroidPlatformProxy::ExitCode_t AndroidPlatformProxy::ReclaimResources(AppPtr_t papp) {
	logger->Info("ReclaimResources: %s", papp->StrId());
	return PLATFORM_OK;
}

AndroidPlatformProxy::ExitCode_t AndroidPlatformProxy::MapResources(
		AppPtr_t papp,
		ResourceAssignmentMapPtr_t pres,bool excl) {

	(void) pres;
	(void) excl;
	logger->Info(" MapResources: %s", papp->StrId());
	return PLATFORM_OK;
}

void AndroidPlatformProxy::Exit() {
	logger->Info("Exit: Termination...");
#ifdef CONFIG_BBQUE_LINUX_PROC_LISTENER
	proc_listener.Terminate();
#endif
}

/*
bool AndroidPlatformProxy::IsHighPerformance(
			bbque::res::ResourcePathPtr_t const & path) const {
	UNUSED(path);
	return true;
}
*/

}	// namespace pp
}	// namespace bbque
