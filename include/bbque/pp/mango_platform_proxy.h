#ifndef BBQUE_MANGO_PLATFORM_PROXY_H
#define BBQUE_MANGO_PLATFORM_PROXY_H

#include <mutex>

#include "bbque/config.h"
#include "bbque/platform_proxy.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/pp/mango_platform_description.h"
#include "tg/task_graph.h"
#include "bbque/resource_partition_validator.h"

#define MANGO_PP_NAMESPACE "bq.pp.mango"

// UPV -> POLIMI there could be as much memories as tiles in MANGO
#define MANGO_MAX_MEMORIES       256
#define MANGO_PEAKOS_FILE_SIZE   256*1024*1024

#define BBQUE_PP_MANGO_PLATFORM_ID   ".mango"

#ifndef CONFIG_MANGO_GN_EMULATION
#define BBQUE_PP_MANGO_HARDWARE_ID   "prodesign-fpga"
#else
#define BBQUE_PP_MANGO_HARDWARE_ID   "emulated"
#endif

namespace bbque {
namespace pp {

class MangoPlatformProxy : public PlatformProxy
{
public:

	static MangoPlatformProxy * GetInstance();

	virtual ~MangoPlatformProxy();

	/**
	 * @brief Mango specific resource setup interface.
	 */
	ExitCode_t Setup(SchedPtr_t papp) noexcept override final;

	/**
	 * @brief Mango specific resources enumeration
	 *
	 * The default implementation of this method loads the TPD, is such a
	 * function has been enabled
	 */
	ExitCode_t LoadPlatformData() noexcept override final;

	/**
	 * @brief Mango specific resources refresh
	 */
	ExitCode_t Refresh() noexcept override final;

	/**
	 * @brief Mango specific resources release interface.
	 */
	ExitCode_t Release(SchedPtr_t papp) noexcept override final;

	/**
	 * @brief Mango specific resource claiming interface.
	 */
	ExitCode_t ReclaimResources(SchedPtr_t papp) noexcept override final;

	/**
	 * @brief Mango specific resource binding interface.
	 */
	ExitCode_t MapResources(
				SchedPtr_t papp, ResourceAssignmentMapPtr_t pres, bool excl) noexcept override final;

	/**
	 * @brief Mango specific termiantion.
	 */
	void Exit() override;


	bool IsHighPerformance(bbque::res::ResourcePathPtr_t const & path) const override;

	/**
	 * @brief Mango specific resource claiming interface.
	 */
	ExitCode_t LoadPartitions(SchedPtr_t papp) noexcept;


private:
	//-------------------- CONSTS

	//-------------------- ATTRIBUTES

	bool refreshMode;

	uint32_t num_clusters;
	uint32_t num_tiles;
	uint32_t num_tiles_x;
	uint32_t num_tiles_y;
	uint32_t num_vns;

	uint32_t alloc_nr_req_cores;
	uint32_t alloc_nr_req_buffers;

	// the next keeps track of the memory tiles & addresses where peakos has been uploaded
	std::vector<std::pair<uint32_t, uint32_t>> allocated_resources_peakos;

	/// length of the monitoring period (default=2000ms if PowerMonitor not compiled)
	uint32_t monitor_period_len = 2000;


	//-------------------- METHODS

	MangoPlatformProxy();

	ExitCode_t BootTiles(uint32_t cluster_id) noexcept;
	ExitCode_t BootTiles_PEAK(uint32_t cluster_id, int tile_id) noexcept;
	ExitCode_t RegisterTiles(uint32_t cluster_id) noexcept;
	ExitCode_t RegisterMemoryBank(
				std::string const & group_prefix,
				uint32_t cluster_id, int tile_id, int mem_id) noexcept;

	ExitCode_t SetProcessorArchInfo(TaskGraph & tg) noexcept;

	class MangoPartitionSkimmer : public PartitionSkimmer
	{
	public:
		MangoPartitionSkimmer();

		virtual ExitCode_t Skim(
					const TaskGraph & tg,
					std::list<Partition> &,
					uint32_t hw_cluster_id) override final;

		virtual ExitCode_t SetPartition(
						TaskGraph & tg,
						const Partition & partition) noexcept override final;

		virtual ExitCode_t UnsetPartition(
						const TaskGraph & tg,
						const Partition & partition) noexcept override final;

	private:
		std::unique_ptr<bu::Logger> logger;
		std::recursive_mutex hn_mutex;
	};

};

} // namespace pp
} // namespace bbque

#endif // MANGO_PLATFORM_PROXY_H
