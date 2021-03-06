#ifndef BBQUE_LINUX_NVIDIAPROXY_H_
#define BBQUE_LINUX_NVIDIAPROXY_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "nvml.h"

#include "bbque/config.h"
#include "bbque/pp/opencl_platform_proxy.h"
#include "bbque/pm/power_manager.h"
#include "bbque/pm/power_manager_nvidia.h"

#define BBQUE_PP_NVML_PLATFORM_ID ".nvidia"
#define BBQUE_PP_NVML_HARDWARE_ID "cuda"

#define BBQUE_NVIDIA_GPU_PATH "sys.gpu.pe"

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque {
namespace pp {

class NVIDIAPlatformProxy : public OpenCLPlatformProxy
{
public:

	static NVIDIAPlatformProxy * GetInstance();

	/**
	 * @brief Destructor
	 */
	virtual ~NVIDIAPlatformProxy();

	/**
	 * @brief Load NVML device data function
	 */
	ExitCode_t LoadPlatformData() override final;

	/**
	 * @brief nVIDIA resource assignment mapping
	 */
	ExitCode_t MapResources(SchedPtr_t papp,
				ResourceAssignmentMapPtr_t pres,
				bool excl = true);

	/**
	 * nvml specific termination
	 */
	void Exit() override;

private:

	/*** Number of available GPU in the system */
	unsigned int device_count = 0;

	/*** Device descriptors   */
	nvmlDevice_t * nv_devices = nullptr;

	/*** The logger used by the module */
	std::unique_ptr<bu::Logger> logger;

	/*** Constructor */
	NVIDIAPlatformProxy();

	ExitCode_t RegisterDevices();
};

} // namespace pp
} // namespace bbque

#endif
