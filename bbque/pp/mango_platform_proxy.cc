#include <utility>

#include "bbque/pp/mango_platform_proxy.h"
#include "bbque/config.h"
#include "bbque/res/resource_path.h"
#include "bbque/utils/assert.h"
#include "bbque/pp/mango_platform_description.h"
#include "bbque/resource_partition_validator.h"

#ifdef CONFIG_BBQUE_WM
#include "bbque/power_monitor.h"
#endif

#include <libhn/hn.h>

#define BBQUE_PP_MANGO_PLATFORM_ID   "org.mango"

#ifndef CONFIG_MANGO_GN_EMULATION
#define BBQUE_PP_MANGO_HARDWARE_ID   "prodesign-fpga"
#else
#define BBQUE_PP_MANGO_HARDWARE_ID   "emulated"
#endif

namespace bb = bbque;
namespace br = bbque::res;
namespace po = boost::program_options;

namespace bbque {
namespace pp {

/****************************************************************************
 *  Static functions                                                        *
 ****************************************************************************/

std::unique_ptr<bu::Logger> logger;



/* It follows some static local function not member of any class: we selected this approach in order
   to avoid to add the hn.h in the header file mango_platform_proxy.h. This was done in order to
   avoid the pollution of the global namespace of Barbeque with all the mess of the HN library.
   This is definetely not elegant, so maybe we want to FIXME this in the future. */

/**
 * @brief From the TG we have the information of threads number and the architecture type, but
 *	  we have to pass in aggregated form to the HN library, so this function will convert
 *	  to HN library type
 */
static uint32_t ArchTypeToMangoFamily(ArchType type, unsigned int nr_thread)
{
	UNUSED(nr_thread);

	switch (type) {
	case ArchType::PEAK:
		return HN_TILE_FAMILY_PEAK;
	case ArchType::NUPLUS:
		return HN_TILE_FAMILY_NUPLUS;
	case ArchType::DCT:
		return HN_TILE_FAMILY_DCT;
	case ArchType::TETRAPOD:
		return HN_TILE_FAMILY_TETRAPOD;

	case ArchType::GN:
		return HN_TILE_FAMILY_GN; // In GN emulation case we are not interested in the real-time
		// TODO add other architectures
	default:
		throw std::runtime_error("Unsupported architecture");
	}
}

static ArchType MangoFamilyToArchType(int mango_arch_type)
{
	switch (mango_arch_type) {
	case HN_TILE_FAMILY_PEAK:
		return ArchType::PEAK;
	case HN_TILE_FAMILY_NUPLUS:
		return ArchType::NUPLUS;
	case HN_TILE_FAMILY_DCT:
		return ArchType::DCT;
	case HN_TILE_FAMILY_TETRAPOD:
		return ArchType::TETRAPOD;

	case HN_TILE_FAMILY_GN:
		return ArchType::GN;
		// TODO add other architectures
	default:
		throw std::runtime_error("Unsupported architecture");
	}
}

/**
 * @brief This function gets sets of units for running the tasks in the TG
 */
static void FindUnitsSets(const TaskGraph &tg,
			  uint32_t hw_cluster_id,
			  unsigned int ***tiles,
			  unsigned int ***families_order,
			  unsigned int *num_sets)
{
	int num_tiles = tg.TaskCount();
	unsigned int *tiles_family = new unsigned int[num_tiles];
	std::vector<uint32_t> tiles_id;

	// Fill the computing resources requested
	int i = 0;
	for (auto t : tg.Tasks()) {
#ifndef CONFIG_MANGO_GN_EMULATION
		if (t.second->GetAssignedArch() == ArchType::GN) {
			logger->Error("Tile id=%i is of type GN but BarbequeRTRM is not "
				"compiled in GN emulation mode. "
				"This will probably lead to an allocation failure.", i);
		}
#endif
		tiles_family[i++] = ArchTypeToMangoFamily(
							t.second->GetAssignedArch(),
							t.second->GetThreadCount());

		int mapped_processor = t.second->GetAssignedProcessor();
		if (mapped_processor >= 0) {
			logger->Debug("FindUnitsSets: task=%d mapped_processor=%d",
				t.second->Id(), mapped_processor);
			tiles_id.push_back(mapped_processor);
		}
	}

	uint32_t start_tile = 0;
	if (!tiles_id.empty()) {
		start_tile = tiles_id[0];
		logger->Debug("FindUnitsSets: start finding from tile=%d", start_tile);
	}

	int res = hn_find_units_sets(
				start_tile, num_tiles, tiles_family, tiles, families_order, num_sets,
				hw_cluster_id);

	if (tiles_family == nullptr) {
		logger->Warn("FindUnitsSets: unexpected null pointer (tiles_family)"
			" [libhn suspect corruption]");
		return;
	}

	delete[] tiles_family;
	if (res != HN_SUCCEEDED)
		throw std::runtime_error("FindUnitsSets: Unable to find units sets");
}

/**
 * @brief This function gets a set of memories for the buffers in the TG
 */
static bool FindMemoryAddresses(const TaskGraph &tg,
				uint32_t hw_cluster_id,
				unsigned int *tiles_set,
				unsigned int *mem_buffers_tiles,
				unsigned int *mem_buffers_addr)
{

	// We request the number of shared buffer + the number of task, since we have
	// to load also the kernels in memory
	int num_mem_buffers = tg.BufferCount() + tg.TaskCount();
	unsigned int *mem_buffers_size = new unsigned int[num_mem_buffers];

	int i = 0;
	for (auto b : tg.Buffers()) {
		bbque_assert(i < num_mem_buffers);
		mem_buffers_size[i++] = b.second->Size();
	}

	int filled_buffers = i;

	// We now register the mapping task-buffer and we allocate a per-task buffer
	// to allocate the kernel image.
	// The bandwidth is not currently managed by the HN library, so we use only
	// a boolean value to indicate that the buffer uses that task and vice versa.
	i = 0;
	for (auto t : tg.Tasks()) {
		// Extra buffer for kernel and stack memory
		auto arch =  t.second->GetAssignedArch();
		auto ksize = t.second->Targets()[arch]->BinarySize();
		auto ssize = t.second->Targets()[arch]->StackSize();
		int kimage_index = filled_buffers + i;
		mem_buffers_size[kimage_index] = ksize + ssize;
		i++;
	}

	// Find and pre-allocate memory space for kernels and buffers
	for (i = 0; i < num_mem_buffers; i++) {
		unsigned int tile = 0;
		if (i >= filled_buffers) {
			// Let's set the kernel buffer close to the tile where the task will run on
			tile = tiles_set[i - filled_buffers];
		}
		else {
			// for input and output buffers.
			// FIXME Better to allocate the buffer close to the
			// tiles the unit that are using it will be mapped on
			tile = tiles_set[0];
		}

		logger->Debug("FindMemoryAddresses: find "
			"cluster=%d tile=%d, buffer=%d, mem_buffer_size=%d",
			hw_cluster_id, tile, i, mem_buffers_size[i]);

		// Find a memory space for allocating the buffer or kernel
		int res = hn_find_memory(
					tile,
					mem_buffers_size[i],
					&mem_buffers_tiles[i],
					&mem_buffers_addr[i],
					hw_cluster_id);
		if (res != HN_SUCCEEDED) {
			logger->Error("FindMemoryAddresses: unable to find memory");
			return false;
		}

		// TRICK we allocated memory areas when finding them, in order
		// to get different areas (see try above),
		// Next, when setting the partition it will be allocated again
		res = hn_allocate_memory(
					mem_buffers_tiles[i],
					mem_buffers_addr[i],
					mem_buffers_size[i],
					hw_cluster_id);
		if (res != HN_SUCCEEDED) {
			logger->Error("FindMemoryAddresses: unable to allocate memory");
			return false;
		}

		logger->Debug("FindMemoryAddresses: found tile=%d allocation address=%p",
			mem_buffers_tiles[i], mem_buffers_addr[i]);
	}

	if (mem_buffers_size == nullptr) {
		logger->Warn("FindMemoryAddresses: unexpected null pointer: mem_buffers_size"
			" [libhn suspect corruption]");
		return false;
	}

	delete[] mem_buffers_size;
	return true;
}

static Partition GetPartition(const TaskGraph &tg,
			      unsigned int hw_cluster_id,
			      unsigned int *tiles,
			      unsigned int *families_order,
			      unsigned int *mem_buffers_tiles,
			      unsigned int *mem_buffers_addr,
			      int partition_id) noexcept
{
	auto it_task      = tg.Tasks().begin();
	int tasks_size    = tg.Tasks().size();
	auto it_buff      = tg.Buffers().begin();
	int buff_size     = tg.Buffers().size();
	bool *tile_mapped = new bool[tasks_size];
	std::fill_n(tile_mapped, tasks_size, false);

	// The partition has a cluster scope
	Partition part(partition_id, hw_cluster_id);
	logger->Debug("GetPartition: id=%d filling mapping information", partition_id);

	// FIXME UPV -> POLIMI do it in a more efficient way if required
	//       We have to map the task to a tile according to its family type
	for (int j = 0; j < tasks_size; j++) {
		uint32_t family = ArchTypeToMangoFamily(
							it_task->second->GetAssignedArch(),
							it_task->second->GetThreadCount());

		// look for the family type of the task
		int k = 0;
		for (k = 0; k < tasks_size; k++) {
			if ((families_order[k] == family) && (!tile_mapped[k])) {
				tile_mapped[k] = true;
				break;
			}
		}
		// we are always going to find an unmapped tile as the sets provided by
		// hn_find_units_sets hnlib function return sets of task_size tiles
		part.MapTask(it_task->second, tiles[k],
			mem_buffers_tiles[buff_size + j],
			mem_buffers_addr[buff_size + j]);
		it_task++;
	}

	delete[] tile_mapped;
	bbque_assert(it_task == tg.Tasks().end());
	for (int j = 0; j < buff_size; j++) {
		part.MapBuffer(it_buff->second, mem_buffers_tiles[j], mem_buffers_addr[j]);
		it_buff++;
	}
	bbque_assert(it_buff == tg.Buffers().end());
	logger->Debug("GetPartition: id=%d mapping information filled",
		partition_id);

	return part;
}

static bool AssignMemory(const TaskGraph &tg,
			 const Partition &partition) noexcept
{
	uint32_t hw_cluster_id = partition.GetClusterId();

	// Assign a memory area to buffers
	for (auto & buffer : tg.Buffers()) {
		uint32_t memory_bank = partition.GetMemoryBank(buffer.second);
		uint32_t phy_addr    = partition.GetBufferAddress(buffer.second);

		buffer.second->SetMemoryBank(memory_bank);
		buffer.second->SetPhysicalAddress(phy_addr);

		int res = hn_allocate_memory(
					memory_bank, phy_addr, buffer.second->Size(), hw_cluster_id);
		if (res != HN_SUCCEEDED) {
			logger->Error("AssignMemory: error while allocating space for"
				"buffer id=%d size=%lu [bank=%d address=%p error=%d]",
				buffer.second->Id(),
				buffer.second->Size(),
				memory_bank,
				phy_addr,
				res);
			return false;
		}
		logger->Info("AssignMemory: buffer id=%d allocated at memory id=%d [address=%p]",
			buffer.second->Id(), memory_bank, phy_addr);
	}

	// Assign a memory area to kernels (executable and stack)
	for (auto & task : tg.Tasks()) {
		auto arch = task.second->GetAssignedArch();
		uint32_t phy_addr    = partition.GetKernelAddress(task.second);
		uint32_t mem_tile    = partition.GetKernelBank(task.second);
		auto     ksize       = task.second->Targets()[arch]->BinarySize();
		auto     ssize       = task.second->Targets()[arch]->StackSize();
		task.second->Targets()[arch]->SetMemoryBank(mem_tile);
		task.second->Targets()[arch]->SetAddress(phy_addr);

		int res = hn_allocate_memory(mem_tile, phy_addr, ksize + ssize, hw_cluster_id);
		if (res != HN_SUCCEEDED) {
			logger->Error("AssignMemory: error while allocating space for"
				" kernel id=%d size=%d [bank=%d address=%p error=%d]",
				task.second->Id(), ksize + ssize, mem_tile, phy_addr, res);
			return false;
		}
		logger->Info("AssignMemory: task id=%d kernel for %s size=%lu "
			"allocated [mem_id=%u address=%p]",
			task.second->Id(), GetStringFromArchType(arch),
			ksize + ssize, mem_tile, phy_addr);
	}

	// Now, we have to ask for the location in TileReg of events
	// TODO: Manage the UNIZG case
	// TODO: what to do in case of failure?
	// TODO: Policy for tile selection
	for ( auto event : tg.Events()) {
		uint32_t phy_addr;
		int err = hn_get_synch_id (&phy_addr, 0, HN_READRESET_INCRWRITE_REG_TYPE,
					hw_cluster_id);
		if (err != HN_SUCCEEDED) {
			logger->Error("AssignMemory: cannot find sync register for event %d",
				event.second->Id());

			// TODO we should deallocate the other assigned events?
			return false;
		}

		logger->Debug("AssignMemory: event %d assigned to ID %p",
			event.second->Id(), phy_addr);
		event.second->SetPhysicalAddress(phy_addr);
	}

	return true;
}

/**
 * @brief This function gets a set of memories for the buffers in the TG
 */
static bool ReserveMemory(const TaskGraph &tg)
{
	uint32_t hw_cluster_id = tg.GetCluster();
	uint32_t mem_bank;
	uint32_t start_addr;

	// Get the memory space amount required for allocating the buffers
	for (auto & b : tg.Buffers()) {
		auto & buffer = b.second;
		// find...
		uint32_t tile_id = buffer->MemoryBank();
		logger->Debug("ReserveMemory: buffer=%d finding space "
			"(scheduled on mem=%d)...",
			b.first, buffer->MemoryBank());
		int ret = hn_find_memory(
					tile_id,
					buffer->Size(),
					&mem_bank,
					&start_addr,
					hw_cluster_id);
		if (ret != HN_SUCCEEDED) {
			logger->Error("ReserveMemory: not memory for buffer=%d [err=%d]",
				b.first, ret);
			return false;
		}

		// assign...
		logger->Debug("ReserveMemory: buffer=%d allocating space...", b.first);
		ret = hn_allocate_memory(mem_bank, start_addr, buffer->Size(), hw_cluster_id);
		if (ret != HN_SUCCEEDED) {
			logger->Error("ReserveMemory: an error occurred while allocating"
				" memory for buffer=%d [err=%d]", b.first, ret);
			return false;
		}
		logger->Debug("ReserveMemory: buffer=%d <size=%d> -> [mem:%d, addr=%p]",
			b.first, buffer->Size(), mem_bank, start_addr);

		// set task-graph information
		buffer->SetMemoryBank(mem_bank);
		buffer->SetPhysicalAddress(start_addr);
	}

	// Get the memory space amount required for allocating the kernel
	// binaries and related stacks
	for (auto & t : tg.Tasks()) {
		auto & task = t.second;
		auto arch       = task->GetAssignedArch();
		auto bin_size   = task->Targets()[arch]->BinarySize();
		auto stack_size = task->Targets()[arch]->StackSize();

		// find...
		uint32_t tile_id = task->GetAssignedProcessor();
		logger->Debug("ReserveMemory: task=%d finding space for binary and stack",
			t.first);
		int ret = hn_find_memory(
					tile_id,
					bin_size + stack_size,
					&mem_bank,
					&start_addr,
					hw_cluster_id);

		if (ret != HN_SUCCEEDED) {
			logger->Error("ReserveMemory: not memory for task=%d [err=%d]",
				t.first, ret);
			return false;
		}

		// assign...
		logger->Debug("ReserveMemory: task=%d allocating space...", t.first);
		ret = hn_allocate_memory(mem_bank, start_addr, bin_size + stack_size, hw_cluster_id);
		if (ret != HN_SUCCEEDED) {
			logger->Error("ReserveMemory: an error occurred while allocating"
				" memory for task=%d [err=%d]", t.first, ret);
			return false;
		}
		logger->Debug("ReserveMemory: task=%d <size=%d> -> [mem:%d, addr=%p]",
			t.first, bin_size + stack_size, mem_bank, start_addr);

		// set task-graph information
		task->Targets()[arch]->SetMemoryBank(mem_bank);
		task->Targets()[arch]->SetAddress(start_addr);
	}

	// Allocate memory space for the events
	for (auto & e : tg.Events()) {
		auto & event(e.second);
		int err = hn_get_synch_id(
					&start_addr, 0, HN_READRESET_INCRWRITE_REG_TYPE, hw_cluster_id);
		if (err != HN_SUCCEEDED) {
			logger->Error("ReserveMemory: event=%d no sync register available",
				event->Id());
			return false;
		}

		logger->Debug("ReserveMemory: event=%d assigned to address=%p",
			event->Id(), start_addr);
		event->SetPhysicalAddress(start_addr);
	}

	return true;
}

static bool ReleaseMemory(const TaskGraph &tg) noexcept
{

	uint32_t hw_cluster_id = tg.GetCluster();

	// Release events reservation
	for (auto & event : tg.Events()) {
		bbque_assert(event.second);
		uint32_t phy_addr = event.second->PhysicalAddress();

		int err = hn_release_synch_id(phy_addr, hw_cluster_id);
		if (err != HN_SUCCEEDED) {
			logger->Error("ReleaseMemory: unable to release event=%d (addr=%p)",
				event.second->Id(), phy_addr);
			return false;
		}
		logger->Debug("ReleaseMemory: released event=%d (addr=%p)", event.second->Id(), phy_addr);
	}

	// Release memory buffers
	for (auto & buffer : tg.Buffers()) {
		uint32_t memory_bank = buffer.second->MemoryBank();
		uint32_t phy_addr    = buffer.second->PhysicalAddress();
		uint32_t size        = buffer.second->Size();
		;

		int err = hn_release_memory(memory_bank, phy_addr, size, hw_cluster_id);
		if (err != HN_SUCCEEDED) {
			logger->Error("ReleaseMemory: error while releasing buffer=%d",
				buffer.second->Id());
			return false;
		}
		logger->Debug("ReleaseMemory: buffer=%d is released at bank %d [address=%p]",
			buffer.second->Id(), memory_bank, phy_addr);
	}

	// Release kernel binary memory areas
	for (auto & task : tg.Tasks()) {
		auto arch = task.second->GetAssignedArch();
		uint32_t phy_addr    = task.second->Targets()[arch]->Address();
		uint32_t mem_tile    = task.second->Targets()[arch]->MemoryBank();
		auto     ksize       = task.second->Targets()[arch]->BinarySize();
		auto     ssize       = task.second->Targets()[arch]->StackSize();

		int err = hn_release_memory(mem_tile, phy_addr, ksize + ssize, hw_cluster_id);
		if (err != HN_SUCCEEDED) {
			logger->Error("ReleaseMemory: error while releasing task=%d",
				task.second->Id());
			return false;
		}
		logger->Debug("ReleaseMemory: task=%d released space for kernel %s"
			" [bank=%d, address=%p size=%lu]",
			task.second->Id(),
			GetStringFromArchType(arch),
			mem_tile, phy_addr, ksize + ssize);
	}

	return true;
}

/**
 * @brief The function returns the id of the unit set matching the mapping option
 * selected by the scheduling policy
 * @param tg the application task-graph with the mapping information
 * @param unit_sets the set of units (processors) mappings available at runtime
 * @param num_sets the number of runtime units mappings
 * @return the id of the unit mapping set, or -1 in case of fail
 */
static int GetCoherentUnitSet(TaskGraph & tg,
			      unsigned int ** unit_sets,
			      unsigned int num_sets)
{
	unsigned int nr_tasks = tg.TaskCount();
	unsigned int matching_count = 0;
	logger->Debug("GetCoherentUnitSet: nr_tasks=%d", nr_tasks);

	for (unsigned int set_id = 0; set_id < num_sets; ++set_id) {
		for (auto & task_entry : tg.Tasks()) {
			auto task_id = task_entry.first;
			auto & task  = task_entry.second;
			bool mapping_matched = false;

			// Look for the mapped processor among the tiles in the current set
			for (unsigned int tile_index = 0; tile_index < nr_tasks; ++tile_index) {
				logger->Debug("GetCoherentUnitSet: [set=%d] task=%d -> proc=%d [=%d?]",
					set_id,
					task_id,
					unit_sets[set_id][tile_index],
					task->GetAssignedProcessor());

				if (unit_sets[set_id][tile_index] == (uint32_t) task->GetAssignedProcessor()) {
					mapping_matched = true;
					++matching_count;
					logger->Debug("GetCoherentUnitSet: [set=%d] task=%d mapping matched",
						set_id,	task_id);
					break;
				}
			}

			// Mapped processor found?
			if (!mapping_matched) {
				logger->Debug("GetCoherentUnitSet: [set=%d] does not match", set_id);
				break;
			}

		}

		if (matching_count == nr_tasks) {
			logger->Debug("GetCoherentUnitSet: [set=%d] matches "
				"the scheduled task mapping", set_id);
			return set_id;
		}
	}

	logger->Error("GetCoherentUnitSet: no matching for the scheduled mapping");
	return -1;
}

static int ReserveProcessingUnits(TaskGraph & tg)
{
	uint32_t hw_cluster_id = tg.GetCluster();
	int num_tiles = tg.TaskCount();
	unsigned int * units_set = new unsigned int[num_tiles];
	unsigned int i = 0;

	// Fill the array with the tiles IDs for the reservation request
	for (auto & task_entry : tg.Tasks()) {
		auto task_id = task_entry.first;
		auto & task  = task_entry.second;

		units_set[i] = task->GetAssignedProcessor();
		logger->Debug("ReserveProcessingUnits: task=%d to map onto unit=%d",
			task_id, units_set[i]);
		i++;
	}

	// Reserve units
	int err = hn_reserve_units_set(num_tiles, units_set, hw_cluster_id);
	if (err != HN_SUCCEEDED) {
		logger->Error("ReserveProcessingUnits: units reservation failed [err=%d]", err);
		return -3;
	}
	logger->Debug("ReserveProcessingUnits: units reservation done");

	return 0;
}

static bool ReleaseProcessingUnits(const TaskGraph & tg)
{
	uint32_t hw_cluster_id = tg.GetCluster();

	// Release units
	unsigned int num_tiles = tg.TaskCount();
	uint32_t * units = new uint32_t[num_tiles];
	unsigned int i = 0;
	for (auto task : tg.Tasks()) {
		auto arch = task.second->GetAssignedArch();
		units[i]  = task.second->GetAssignedProcessor();
		logger->Debug("ReleaseProcessingUnits: task %d released tile %u for kernel %s",
			task.second->Id(), units[i], GetStringFromArchType(arch));
		i++;
	}

	bool ret = true;
	int err = hn_release_units_set(num_tiles, units, hw_cluster_id);
	if (err != HN_SUCCEEDED) {
		logger->Error("ReleaseProcessingUnits: error while releasing the units set [err=%d]", err);
		ret = false;
	}
	else {
		logger->Info("ReleaseProcessingUnits: units set released");
	}

	if (units == nullptr) {
		logger->Fatal("ReleaseProcessingUnits: unexpected null pointer: units"
			" [libhn suspect corruption]");
		ret = false;
	}

	delete[] units;

	return ret;
}

/****************************************************************************
 *  MANGO Platform Proxy                                                    *
 ****************************************************************************/

MangoPlatformProxy * MangoPlatformProxy::GetInstance()
{
	static MangoPlatformProxy * instance;
	if (instance == nullptr)
		instance = new MangoPlatformProxy();
	return instance;
}

MangoPlatformProxy::MangoPlatformProxy() :
    refreshMode(false)
{
	//---------- Get a logger module
	logger = bu::Logger::GetLogger(MANGO_PP_NAMESPACE);
	bbque_assert(logger);

	this->platform_id = BBQUE_PP_MANGO_PLATFORM_ID;
	this->hardware_id = BBQUE_PP_MANGO_HARDWARE_ID;

	// In order to initialize the communication with the HN library we have to
	// prepare the filter to enable access to registers and statistics
	// TODO: this was a copy-and-paste of the example, we have to ask to UPV
	//       how to properly tune these parameters, in particular the meaning of
	//	 UPV_PARTITION_STRATEGY
	hn_filter_t filter;
	filter.target = HN_FILTER_TARGET_MANGO;
	filter.mode = HN_FILTER_APPL_MODE_SYNC_READS;
	filter.tile = 999;
	filter.core = 999;

	logger->Info("MangoPlatformProxy: initializing communication with HN daemon...");
	int hn_init_err = hn_initialize(filter, UPV_PARTITION_STRATEGY, 1, 0, 0);
	if (hn_init_err == HN_SUCCEEDED) {
		logger->Info("MangoPlatformProxy: HN daemon connection established");
	}
	else {
		logger->Fatal("MangoPlatformProxy: unable to establish HN daemon connection"
			"[error=%d]", hn_init_err);
	}

	bbque_assert ( 0 == hn_init_err );

	// Get the number of clusters
	int err = hn_get_num_clusters(&this->num_clusters);
	if ( HN_SUCCEEDED != err ) {
		logger->Fatal("MangoPlatformProxy: unable to get the number of clusters "
			"[error=%d]", err);
	}
	logger->Info("MangoPlatformProxy: nr. of clusters: %d", num_clusters);

	// Reset the platform (cluster by cluster)
	for (uint32_t cluster_id = 0; cluster_id < this->num_clusters; ++cluster_id) {
		logger->Debug("MangoPlatformProxy: resetting cluster=<%d>...", cluster_id);
		// This function call may take several seconds to conclude
		int hn_reset_err = hn_reset(0, cluster_id);
		if (hn_reset_err == HN_SUCCEEDED) {
			logger->Info("MangoPlatformProxy: HN cluster=<%d> successfully initialized",
				cluster_id);
		}
		else {
			logger->Crit("MangoPlatformProxy: unable to reset the HN cluster=%d"
				" [error= %d]", cluster_id, hn_reset_err);
			// We consider this error non critical and we try to continue
		}
	}

	// Register our skimmer for the incoming partitions (it actually fills the partition list,
	// not skim it)
	// Priority of 100 means the maximum priority, this is the first skimmer to be executed
	ResourcePartitionValidator &rmv = ResourcePartitionValidator::GetInstance();
	rmv.RegisterSkimmer(std::make_shared<MangoPartitionSkimmer>() , 100);
	logger->Info("MangoPlatformProxy: partition skimmer registered");
}

MangoPlatformProxy::~MangoPlatformProxy()
{
	logger->Info("MangoPlatformProxy: nothing left to be done");
}

bool
MangoPlatformProxy::IsHighPerformance(bbque::res::ResourcePathPtr_t const & path) const
{
	UNUSED(path);
	return false;
}

MangoPlatformProxy::ExitCode_t MangoPlatformProxy::Setup(SchedPtr_t papp) noexcept
{
	ExitCode_t result = PLATFORM_OK;
	UNUSED(papp);
	return result;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::Release(SchedPtr_t papp) noexcept
{
	logger->Info("Release: application [%s]...", papp->StrId());
	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::ReclaimResources(SchedPtr_t sched) noexcept
{

	ba::AppCPtr_t papp = std::dynamic_pointer_cast<ba::Application>(sched);

	auto partition = papp->GetPartition();
	if (partition != nullptr) {
		ResourcePartitionValidator & rmv(ResourcePartitionValidator::GetInstance());

		auto ret = rmv.RemovePartition(*papp->GetTaskGraph(), *partition);
		bbque_assert(ResourcePartitionValidator::PMV_OK == ret);
		if (ret != ResourcePartitionValidator::PMV_OK) {
			logger->Warn("ReclaimResources: [%s] hw partition release failed", papp->StrId());
		}
		else {
			papp->SetPartition(nullptr);
			logger->Info("ReclaimResources: [%s] hw partition released", papp->StrId());
		}
		return PLATFORM_OK;
	}

	logger->Warn("ReclaimResources: [%s] no partition to release", papp->StrId());
	auto tg = papp->GetTaskGraph();
	if (tg == nullptr) {
		logger->Error("ReclaimResources: [%s] missing task-graph", papp->StrId());
		return PLATFORM_MAPPING_FAILED;
	}

	// Release resources by navigating the task graph...
	int err = ReleaseProcessingUnits(*tg);
	if (err < 0) {
		logger->Error("ReclaimResources: [%s] failed while reserving processing units", papp->StrId());
		return PLATFORM_MAPPING_FAILED;
	}
	logger->Info("ReclaimResources: [%s] processing units released", papp->StrId());

	// Reserve memory
	bool retm = ReleaseMemory(*tg);
	if (!retm) {
		logger->Error("ReclaimResources: [%s] failed while reserving memory space", papp->StrId());
		return PLATFORM_MAPPING_FAILED;
	}
	logger->Info("ReclaimResources: [%s] memory released", papp->StrId());

	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::MapResources(
				 SchedPtr_t psched, ResourceAssignmentMapPtr_t pres, bool excl) noexcept
{
	UNUSED(pres);
	UNUSED(excl);

	if (psched->GetType() != ba::Schedulable::Type::ADAPTIVE) {
		logger->Warn("MapResources: [%s] not managed by this proxy", psched->StrId());
		return PLATFORM_MAPPING_FAILED;
	}

	auto papp = static_cast<ba::Application *>(psched.get());

	auto tg = papp->GetTaskGraph();
	if (tg == nullptr) {
		logger->Error("MapResources: [%s] task-graph missing", papp->StrId());
		return PLATFORM_MAPPING_FAILED;
	}

	// If we have a partition assigned, nothing must be done (ManGA policy version1)
	if (papp->GetPartition() != nullptr) {
		logger->Debug("MapResources: [%s] already performed via partition skimmer", papp->StrId());
		return PLATFORM_OK;
	}

	// Set the architecture type for each assigned processor
	ExitCode_t ret = SetProcessorArchInfo(*tg);
	if (ret != PLATFORM_OK) {
		logger->Error("MapResources: [%s] failed while getting processor architecture", papp->StrId());
		return ret;
	}

	// Reserve processing units
	int err = ReserveProcessingUnits(*tg);
	if (err < 0) {
		logger->Error("MapResources: [%s] failed while reserving processing units", papp->StrId());
		return PLATFORM_MAPPING_FAILED;
	}
	logger->Info("MapResources: [%s] processing units reserved", papp->StrId());

	// Reserve memory
	bool retm = ReserveMemory(*tg);
	if (!retm) {
		logger->Error("MapResources: [%s] failed while reserving memory space", papp->StrId());
		return PLATFORM_MAPPING_FAILED;
	}
	logger->Info("MapResources: [%s] memory space reserved", papp->StrId());


	// Send back to the application library the mapped task-graph
	papp->SetTaskGraph(tg);
	logger->Info("MapResources: [%s] task-graph mapping updated", papp->StrId());

	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::SetProcessorArchInfo(TaskGraph & tg) noexcept
{
	uint32_t cluster_id = tg.GetCluster();

	for (auto task_entry : tg.Tasks()) {
		hn_tile_info_t tile_info;
		int err = hn_get_tile_info(
					task_entry.second->GetAssignedProcessor(),
					&tile_info,
					cluster_id);
		if (err != HN_SUCCEEDED) {
			logger->Error("SetProcessorArchInfo: cannot retrieve tile info");
			return PLATFORM_GENERIC_ERROR;
		}

		auto arch = MangoFamilyToArchType(tile_info.unit_family);
		task_entry.second->SetAssignedArch(arch);
	}

	return PLATFORM_OK;
}

void MangoPlatformProxy::Exit()
{
	logger->Info("Exit: Termination...");

	// Stop HW counter monitors
	for (uint32_t cluster_id = 0; cluster_id < this->num_clusters; ++cluster_id) {
		hn_tile_info_t tile_info;
		for (uint_fast32_t tile_id = 0; tile_id < num_tiles; tile_id++) {
			int err = hn_get_tile_info(tile_id, &tile_info, cluster_id);
			if (HN_SUCCEEDED != err) {
				logger->Fatal("Exit: unable to get the info for cluster=<%d> tile=<%d>",
					cluster_id, tile_id);
				continue;
			}
#ifdef CONFIG_BBQUE_PM_MANGO
			logger->Debug("Exit: disabling monitors...");
			if (tile_info.unit_family == HN_TILE_FAMILY_PEAK) {
				err = hn_stats_monitor_configure_tile(tile_id, 0, cluster_id);
				if (err == 0)
					logger->Info("Exit: stopping monitor for cluster=<%d> tile=<%d>",
						cluster_id, tile_id);
				else
					logger->Error("Error while stopping monitor for cluster=<%d> tile=<%d>",
						cluster_id, tile_id);
			}
#endif
		}
	}

	// First release occupied resources, i.e., allocated memory for peakOS
	// ...hope partitions are correctly unset too
	for (uint32_t cluster_id = 0; cluster_id < this->num_clusters; ++cluster_id) {
		for (auto rsc : allocated_resources_peakos) {
			uint32_t tile_mem = rsc.first;
			uint32_t addr     = rsc.second;
			hn_release_memory(tile_mem, addr, MANGO_PEAKOS_FILE_SIZE, cluster_id);
			logger->Info("Exit: cluster=<%d> "
				"released PEAK OS memory %d address 0x%08x", cluster_id, tile_mem, addr);
		}
	}

	// Just clean up stuffs...
	int hn_err_ret = hn_end();
	if (hn_err_ret != 0) {
		logger->Warn("Exit: Error occurred while terminating: %d", hn_err_ret);
	}
}

MangoPlatformProxy::ExitCode_t MangoPlatformProxy::Refresh() noexcept
{
	refreshMode = true;
	// TODO (we really need this method?)
	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::LoadPlatformData() noexcept
{
	int err = -1;

	// Load platform data for each available HN cluster
	for (uint32_t cluster_id = 0; cluster_id < this->num_clusters; ++cluster_id) {
		// Get the number of tiles
		err = hn_get_num_tiles(&this->num_tiles, &this->num_tiles_x, &this->num_tiles_y, cluster_id);
		if ( HN_SUCCEEDED != err ) {
			logger->Fatal("LoadPlatformData: unable to get the number of tiles [error=%d]", err);
			return PLATFORM_INIT_FAILED;
		}

		// Get the number of VNs
		err = hn_get_num_vns(&this->num_vns, cluster_id);
		if ( HN_SUCCEEDED != err ) {
			logger->Fatal("LoadPlatformData: unable to get the number of VNs [error=%d]", err);
			return PLATFORM_INIT_FAILED;
		}

		logger->Info("LoadPlatformData: cluster=<%d>: num_tiles=%d (%dx%d) num_vns=%d.",
			cluster_id,
			this->num_tiles, this->num_tiles_x, this->num_tiles_y,
			this->num_vns);

		// Now we have to register the tiles to the PlatformDescription and ResourceAccounter
		ExitCode_t pp_err = RegisterTiles(cluster_id);
		if (PLATFORM_OK != pp_err) {
			return pp_err;
		}

		// The tiles should now be booted by Barbeque using the PeakOS image
		pp_err = BootTiles(cluster_id);
		if (PLATFORM_OK != pp_err) {
			return pp_err;
		}
	}

	if (err < 0) {
		logger->Info("LoadPlatformData: some error occurred [error=%d]", err);
		return PLATFORM_INIT_FAILED;
	}

	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::BootTiles_PEAK(uint32_t cluster_id, int tile_id) noexcept
{
	uint32_t req_size = MANGO_PEAKOS_FILE_SIZE;
	uint32_t tile_memory;	// This will be filled with the memory id
	uint32_t base_addr;	    // This is the starting address selected

	// TODO: This is currently managed by the internal HN find memory, however, we have
	//	 to replace this with an hook to the MemoryManager here.
	int err = hn_find_memory(tile_id, req_size, &tile_memory, &base_addr, cluster_id);
	if (HN_SUCCEEDED != err) {
		logger->Error("BootTiles_PEAK: unable to get memory for tile=%d", tile_id);
		return PLATFORM_LOADING_FAILED;
	}

	// Allocate memory for PEAK OS
	logger->Debug("BootTiles_PEAK: cluster=<%d> tile=<%d> allocating memory [BASE_ADDR=0x%x SIZE=%d]...",
		cluster_id, tile_id, base_addr, req_size);
	err = hn_allocate_memory(tile_memory, base_addr, req_size, cluster_id);
	if (HN_SUCCEEDED != err) {
		logger->Error("BootTiles_PEAK: unable to allocate memory for tile=%d", tile_id);
		return PLATFORM_LOADING_FAILED;
	}

	// Load PEAK OK
	logger->Debug("BootTiles_PEAK: loading PEAK OS in memory id=%d [address=0x%x]...",
		tile_memory, base_addr);
	allocated_resources_peakos.push_back(std::make_pair(tile_memory, base_addr));

	err = hn_boot_unit(tile_id, tile_memory, base_addr, MANGO_PEAK_PROTOCOL, MANGO_PEAK_OS, cluster_id);
	if (HN_SUCCEEDED != err) {
		logger->Error("BootTiles_PEAK: unable to boot PEAK tile=%d", tile_id);
		return PLATFORM_LOADING_FAILED;
	}
	logger->Info("BootTiles_PEAK: cluster=<%d> tile=<%d> [PEAK_OS:%s] [PEAK_PROT:%s] booted",
		cluster_id, tile_id, MANGO_PEAK_OS, MANGO_PEAK_PROTOCOL);

	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::BootTiles(uint32_t cluster_id) noexcept
{
	hn_tile_info_t tile_info;
	for (uint_fast32_t tile_id = 0; tile_id < num_tiles; tile_id++) {
		int err = hn_get_tile_info(tile_id, &tile_info, cluster_id);
		if (HN_SUCCEEDED != err) {
			logger->Fatal("BootTiles: unable to get info from tile=<%d> [error=%d].",
				tile_id, err);
			return PLATFORM_INIT_FAILED;
		}

		if (tile_info.unit_family == HN_TILE_FAMILY_PEAK) {
			err = BootTiles_PEAK(cluster_id, tile_id);
			if (PLATFORM_OK != err) {
				logger->Error("BootTiles: unable to boot cluster=<%d> tile=<%d>",
					cluster_id, tile_id);
				return PLATFORM_INIT_FAILED;
			}
#ifdef CONFIG_BBQUE_PM_MANGO
			// Enable monitoring stuff
			logger->Debug("BootTiles: cluster=<%d> tile=<%d> configuring monitors...",
				cluster_id, tile_id);
			err = hn_stats_monitor_configure_tile(tile_id, 1, cluster_id);
			if (err == 0) {
				err = hn_stats_monitor_set_polling_period(monitor_period_len);
				if (err == 0)
					logger->Info("BootTiles: cluster=<%d> tile=<%d> "
						"set monitoring period=%dms",
						cluster_id, tile_id, monitor_period_len);
				else
					logger->Error("BootTiles: cluster=<%d> tile=<%d> "
						"set monitoring period failed",
						cluster_id, tile_id);
			}
			else
				logger->Error("BootTiles: cluster=<%d> tile=<%d> unable to enable "
					"profiling", cluster_id, tile_id);
#endif
		}
		logger->Info("BootTiles: cluster=<%d> tile=<%d> initialized", cluster_id, tile_id);
	}
	logger->Info("BootTiles: cluster=<%d> all tiles successfully booted", cluster_id);

	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::RegisterTiles(uint32_t cluster_id) noexcept
{
	typedef PlatformDescription pd_t;	// Just for convenience

	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	pd_t &pd = pli->getPlatformInfo();
	pd_t::System &sys = pd.GetLocalSystem();

#ifdef CONFIG_BBQUE_WM
	PowerMonitor & wm(PowerMonitor::GetInstance());
	monitor_period_len = wm.GetPeriodLengthMs();
#endif

	for (uint_fast32_t tile_id = 0; tile_id < num_tiles; tile_id++) {
		// First of all get the information of tiles from HN library
		hn_tile_info_t tile_info;
		int err = hn_get_tile_info(tile_id, &tile_info, cluster_id);
		if (HN_SUCCEEDED != err) {
			logger->Fatal("RegisterTiles: unable to get info about "
				"cluster=<%d> tile=<%d> [error=%d]",
				cluster_id, tile_id, err);
			return PLATFORM_INIT_FAILED;
		}

		logger->Info("RegisterTiles: cluster=<%d> tile={id=%d family=%s model=%s}",
			cluster_id, tile_id,
			hn_to_str_unit_family(tile_info.unit_family),
			hn_to_str_unit_model(tile_info.unit_model));

		MangoTile mt(tile_id,
			(MangoTile::MangoUnitFamily_t)(tile_info.unit_family),
			(MangoTile::MangoUnitModel_t)(tile_info.unit_model));
		sys.AddAccelerator(mt);

		// Map the HN cluster to resource of type "GROUP"
		std::string group_id(".");
		group_id += std::string(GetResourceTypeString(br::ResourceType::GROUP));
		group_id += std::to_string(cluster_id);
		std::string group_prefix(sys.GetPath() + group_id);
		mt.SetPrefix(group_prefix);

		// For each tile, we register how many PE how may cores the accelerator has, this
		// will simplify the tracking of ResourceAccounter resources
		for (int i = 0; i < MangoTile::GetCoreNr(mt.GetFamily(), mt.GetModel()); i++) {
			pd_t::ProcessingElement pe(i , 0, 100, pd_t::PartitionType_t::MDEV);
			pe.SetPrefix(mt.GetPath());
			logger->Debug("RegisterTiles: cluster=<%d> tile=<%d> core=<%d>: path=%s",
				cluster_id, tile_id, i, pe.GetPath().c_str());
			mt.AddProcessingElement(pe);

			// Register the processor core for resource accounting
			auto rsrc_ptr = ra.RegisterResource(pe.GetPath(), "", 100);
			rsrc_ptr->SetModel(hn_to_str_unit_family(tile_info.unit_family));
#ifdef CONFIG_BBQUE_WM
			// Register the processor core for power monitoring
			wm.Register(pe.GetPath());
			logger->Debug("RegisterTiles: [%s] registered for power monitoring",
				pe.GetPath().c_str());
#endif
		}

		// Let now register the memories. Unfortunately, memories are not easy to be
		// retrieved, we have to iterate over all tiles and search memories
		unsigned int mem_attached = tile_info.memory_attached;
		if (mem_attached != 0) {
			logger->Debug("RegisterTiles: cluster=<%d> tile=<%d>: mem_attached=%d",
				cluster_id, tile_id, mem_attached);
			// Register the memory attached if present and if not already registered
			// Fixed mem_id to tile_id, it can be only a memory
			// controller attached to a mango tile. So, the mem_id
			// equals to the tile_id, since the HN does not set IDs
			// for memories
			ExitCode_t reg_err = RegisterMemoryBank(
								group_prefix, cluster_id, tile_id, tile_id);
			if (reg_err) {
				return reg_err;
			}

			// And now assign in PD the correct memory pointer
			auto mem = sys.GetMemoryById(mem_attached);
			mt.SetMemory(mem);
		}
	}

	return PLATFORM_OK;
}

MangoPlatformProxy::ExitCode_t
MangoPlatformProxy::RegisterMemoryBank(
				       std::string const & group_prefix,
				       uint32_t cluster_id,
				       int tile_id,
				       int mem_id) noexcept
{
	typedef PlatformDescription pd_t;	// Just for convenience
	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	logger->Debug("RegisterMemoryBank: cluster=<%d> tile=<%d> memory=<%d>",
		cluster_id, tile_id, mem_id);

	uint32_t memory_size;
	int err = hn_get_memory_size(tile_id, &memory_size, cluster_id);
	if (HN_SUCCEEDED != err) {
		logger->Fatal("RegisterMemoryBank: cluster=<%d> tile=<%d> memory=<%d>: "
			"missing information on memory node [error=%d]",
			cluster_id, tile_id, mem_id, err);
		return PLATFORM_INIT_FAILED;
	}

	pd_t &pd = pli->getPlatformInfo();
	pd_t::System &sys = pd.GetLocalSystem();

	// HN memory banks are under the "sys.grp" scope (group_prefix), so
	// they are distinguished w.r.t to the memory banks accessed by CPU
	// code
	pd_t::Memory mem(mem_id, memory_size);
	mem.SetPrefix(group_prefix);
	logger->Debug("RegisterMemoryBank: memory id=<%d> path=<%s>", tile_id, mem.GetPath().c_str());

	sys.AddMemory(std::make_shared<pd_t::Memory>(mem));
	ra.RegisterResource(mem.GetPath(), "", memory_size);
	logger->Info("RegisterMemoryBank: memory id=<%d> size=%u", tile_id, memory_size);

	return PLATFORM_OK;
}

/****************************************************************************
 *  MANGO Partition Skimmer                                                 *
 ****************************************************************************/

MangoPlatformProxy::MangoPartitionSkimmer::ExitCode_t
MangoPlatformProxy::MangoPartitionSkimmer::Skim(
						const TaskGraph &tg,
						std::list<Partition>&part_list,
						uint32_t hw_cluster_id)
{
	unsigned int num_mem_buffers = tg.BufferCount() + tg.TaskCount();
	ExitCode_t res               = SK_OK;
	auto it_task                 = tg.Tasks().begin();
	size_t tasks_size            = tg.Tasks().size();
	auto it_buff                 = tg.Buffers().begin();
	size_t buff_size             = tg.Buffers().size();
	uint32_t **units_sets        = NULL;
	uint32_t **families_order    = NULL;
	uint32_t **mem_buffers_tiles = NULL;
	uint32_t **mem_buffers_addr  = NULL;
	uint32_t *mem_buffers_size   = new uint32_t[buff_size + tasks_size];
	uint32_t num_sets            = 0;

	part_list.clear();

	logger->Debug("Skim: request summary: ");
	for (size_t i = 0; i < tasks_size; i++) {
		// Kernel binary available for the assigned processor type?
		auto arch = it_task->second->GetAssignedArch();
		auto const & arch_it = it_task->second->Targets().find(arch);
		if (arch_it == it_task->second->Targets().end()) {
			logger->Warn("Skim: arch=%s binary not available",
				GetStringFromArchType(arch));
			delete[] mem_buffers_size;
			return SK_NO_PARTITION;
		}
		// MANGO architecture family...
		uint32_t unit_family = ArchTypeToMangoFamily(
							arch, it_task->second->GetThreadCount());
		logger->Debug("  -> Computing Resource %d, HN type %s",
			i, hn_to_str_unit_family(unit_family));

		// Required memory amount per-kernel (executable + stack)
		auto ksize = arch_it->second->BinarySize();
		auto ssize = arch_it->second->StackSize();
		mem_buffers_size[buff_size + i] = ksize + ssize;

		// Next task
		it_task++;
	}
	bbque_assert(it_task == tg.Tasks().end());

	// Required memory amount per-buffer
	for (size_t i = 0; i < buff_size; i++) {
		uint32_t mem_size = it_buff->second->Size();
		mem_buffers_size[i] = mem_size;
		it_buff++;
		logger->Debug("  -> Memory buffer %d, size %d", i, mem_size);
	}
	bbque_assert(it_buff == tg.Buffers().end());

	try {
		// Let's try to find the partitions that satisfy the task-graph
		// requirements
		// It may generate exception if the architecture is not supported
		// (in that case we should not be here ;-))
		logger->Debug("Skim: looking for HN resources...");

		// - Find different sets of resources (partitions)
		std::unique_lock<std::recursive_mutex> hn_lock(hn_mutex);
		FindUnitsSets(tg, hw_cluster_id, &units_sets, &families_order, &num_sets);
		logger->Debug("Skim: HN returned %d available units sets (partitions)",
			num_sets);

		// - Find and reserve memory for every set. We cannot call
		//   hn_find_memory more than once without allocate the memory
		//   returned, since it would return the same bank
		mem_buffers_tiles = new uint32_t*[num_sets];
		mem_buffers_addr  = new uint32_t*[num_sets];
		memset(mem_buffers_tiles, 0, num_sets * sizeof(uint32_t *));
		memset(mem_buffers_addr, 0, num_sets * sizeof(uint32_t *));

		for (unsigned int i = 0; i < num_sets; i++) {
			logger->Debug("Skim: partition id=%d...", i);
			mem_buffers_tiles[i] = new uint32_t[num_mem_buffers];
			mem_buffers_addr[i]  = new uint32_t[num_mem_buffers];

			// Find and allocate memory close the tiles/units in the set
			bool mem_ret = FindMemoryAddresses(
							tg,
							hw_cluster_id,
							units_sets[i],
							mem_buffers_tiles[i],
							mem_buffers_addr[i]);
			if (!mem_ret) {
				logger->Warn("Skim: filled %d (out of %d) partitions",
					i, num_sets);
				if (i == 0) {
					throw std::runtime_error(
								"Skim: "
								"unable to find available memory");
				}
				break;
			}

			// Set partition information and append to list
			Partition part = GetPartition(
						tg,
						hw_cluster_id,
						units_sets[i],
						families_order[i],
						mem_buffers_tiles[i],
						mem_buffers_addr[i],
						i); // partition id
			part_list.push_back(part);
			logger->Debug("Skim: partition id=%d added to the list", i);
		}

		// Release the pre-allocation of the memory areas
		for (unsigned int i = 0; i < num_sets; i++) {
			if (i < part_list.size()) {
				logger->Debug("Skim: partition id=%d releasing...", i);
				for (unsigned int j = 0; j < num_mem_buffers; j++) {
					std::unique_lock<std::recursive_mutex> hn_lock(hn_mutex);
					int res = hn_release_memory(
								mem_buffers_tiles[i][j],
								mem_buffers_addr[i][j],
								mem_buffers_size[j],
								hw_cluster_id);
					if (res != HN_SUCCEEDED) {
						logger->Error("Skim: "
							"tile=%d address=%p size=%d release error",
							mem_buffers_tiles[i][j],
							mem_buffers_addr[i][j],
							mem_buffers_size[j]);
					}
					else {
						logger->Debug("Skim: "
							"tile=%d address=%p size=%d released",
							mem_buffers_tiles[i][j],
							mem_buffers_addr[i][j],
							mem_buffers_size[j]);
					}
				}
			}

			delete[] mem_buffers_tiles[i];
			delete[] mem_buffers_addr[i];
		}
	}
	catch (const std::runtime_error &err) {
		logger->Error("Skim: %s", err.what());
		res = SK_NO_PARTITION;
	}

	// let's deallocate memory created in the hn_find_units_sets hnlib function
	if (units_sets != nullptr) {
		for (unsigned int i = 0; i < num_sets; i++) {
			free(units_sets[i]);
		}
		free(units_sets);
	}

	if (families_order != nullptr) {
		for (unsigned int i = 0; i < num_sets; i++) {
			free(families_order[i]);
		}
		free(families_order);
	}

	// let's deallocate memory created in this method
	if (mem_buffers_tiles != nullptr) {
		delete[] mem_buffers_tiles;
	}

	if (mem_buffers_addr != nullptr) {
		delete[] mem_buffers_addr;
	}

	if (mem_buffers_size != nullptr) {
		delete[] mem_buffers_size;
	}
	else {
		logger->Warn("Skim: unexpected null pointer: mem_buffers_size"
			" [libhn suspect corruption]");
	}

	return res;
}

MangoPlatformProxy::MangoPartitionSkimmer::ExitCode_t
MangoPlatformProxy::MangoPartitionSkimmer::SetPartition(
							TaskGraph & tg,
							const Partition & partition) noexcept
{
	// Set the HW cluster including the mapped resources
	uint32_t hw_cluster_id = partition.GetClusterId();
	tg.SetCluster(hw_cluster_id);

	// We set the mapping of buffers-addresses based on the selected partition
	bool is_ok = AssignMemory(tg, partition);
	if (!is_ok) {
		logger->Error("SetPartition: memory assignment failed");
		return SK_GENERIC_ERROR;
	}

	// Set the assigned processor (for each task)
	for ( auto task : tg.Tasks()) {
		auto tile_id = partition.GetUnit(task.second);
		task.second->SetAssignedProcessor(tile_id);
		logger->Debug("SetPartition: task %d mapped to processor (tile) %d",
			task.second->Id(), tile_id);
	}

	// Reserve the units (processors)
	unsigned int num_tiles = tg.TaskCount();
	uint32_t *units = new uint32_t[num_tiles];
	int i = 0;
	for ( auto task : tg.Tasks()) {
		auto arch = task.second->GetAssignedArch();
		units[i]  = partition.GetUnit(task.second);
		logger->Debug("SetPartition: task %d reserved a unit at tile %u for kernel %s",
			task.second->Id(), units[i], GetStringFromArchType(arch));
		i++;
	}

	ExitCode_t err = PartitionSkimmer::SK_OK;
	std::unique_lock<std::recursive_mutex> hn_lock(hn_mutex);
	int hn_ret = hn_reserve_units_set(num_tiles, units, hw_cluster_id);
	if (hn_ret != HN_SUCCEEDED) {
		logger->Error("SetPartition: units reservation failed");
		err = SK_GENERIC_ERROR;
	}
	else if (units == nullptr) {
		logger->Fatal("SetPartition: unexpected null pointer: units"
			" [libhn suspect corruption]");
		return SK_GENERIC_ERROR;
	}
	delete[] units;

	return err;
}

MangoPlatformProxy::MangoPartitionSkimmer::ExitCode_t
MangoPlatformProxy::MangoPartitionSkimmer::UnsetPartition(
							  const TaskGraph & tg,
							  const Partition & partition) noexcept
{

	uint32_t part_id = partition.GetId();
	logger->Debug("UnsetPartition: [id=%d] deallocating partition...", part_id);

	bool is_ok = ReleaseMemory(tg);
	if (!is_ok) {
		logger->Error("UnsetPartition: [id=%d] error while releasing memory", part_id);
		return SK_GENERIC_ERROR;
	}

	is_ok = ReleaseProcessingUnits(tg);
	if (!is_ok) {
		logger->Error("UnsetPartition: [id=%d] error while releasing kernels space", part_id);
		return SK_GENERIC_ERROR;
	}

	return SK_OK;
}

MangoPlatformProxy::MangoPartitionSkimmer::MangoPartitionSkimmer() : PartitionSkimmer(SKT_MANGO_HN)
{
	logger = bu::Logger::GetLogger(MANGO_PP_NAMESPACE ".skm");
	bbque_assert(logger);
}

}	// namespace pp
}	// namespace bbque
