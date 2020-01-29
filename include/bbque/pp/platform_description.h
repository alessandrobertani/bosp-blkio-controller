#ifndef BBQUE_PLATFORM_DESCRIPTION_H_
#define BBQUE_PLATFORM_DESCRIPTION_H_

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bbque/res/resource_type.h"

#ifdef UINT64_MAX
#define BBQUE_PP_ARCH_SUPPORTS_INT64 1
#else
#define BBQUE_PP_ARCH_SUPPORTS_INT64 0
#endif

namespace bbque {
namespace pp {

/**
 * @class PlatformDescription
 *
 * @brief A PlatformDescription object includes the description of the
 * underlying platform, provided through the systems.xml file.
 *
 * @warning Non thread safe.
 */
class PlatformDescription {

public:
	typedef enum PartitionType {
	        HOST,
	        MDEV,
	        SHARED
	} PartitionType_t;


	class Resource {
	public:
		Resource() {}

		Resource(uint16_t id, res::ResourceType type = res::ResourceType::UNDEFINED):
			id(id), type(type)
		{}

		inline uint16_t GetId() const {
			return this->id;
		}

		inline void SetId(uint16_t id) {
			this->id = id;
		}

		inline res::ResourceType GetType() const {
			return this->type;
		}

		inline void SetType(res::ResourceType type) {
			this->type = type;
		}

		inline void SetPrefix(std::string prefix) {
			this->prefix.assign(prefix + ".");
		}

		inline std::string const & GetPrefix() const {
			return this->prefix;
		}

		inline std::string GetPath() const {
			return prefix + res::GetResourceTypeString(type) +
				std::to_string(id);
		}


	protected:
		uint16_t id = 0;
		res::ResourceType type = res::ResourceType::UNDEFINED;
		std::string prefix = "";
	};


	class ProcessingElement : public Resource {
	public:

		ProcessingElement(
				uint16_t id = 0,
				res::ResourceType type = res::ResourceType::PROC_ELEMENT) :
		Resource(id, type)
		{}

		ProcessingElement(
		        uint16_t id,
		        uint16_t core_id,
		        uint8_t share,
		        PartitionType_t ptype)
			: Resource(id, res::ResourceType::PROC_ELEMENT),
			core_id(core_id), share(share), ptype(ptype)
		{}

		inline uint16_t GetCoreId() const {
			return this->core_id;
		}

		inline void SetCoreId(uint16_t core_id) {
			this->core_id = core_id;
		}

		inline uint32_t GetQuantity() const {
			return this->quantity;
		}

		inline void SetQuantity(uint32_t quantity) {
			this->quantity = quantity;
		}


		inline uint8_t GetShare() const {
			return this->share;
		}

		inline void SetShare(uint8_t share) {
			this->share = share;
		}

		inline PartitionType_t GetPartitionType() const {
			return this->ptype;
		}

		inline void SetPartitionType(PartitionType_t ptype) {
			this->ptype = ptype;
		}

		void SetType(res::ResourceType type) = delete;

	private:
		uint16_t core_id;
		uint32_t quantity;
		uint8_t share;
		PartitionType_t ptype;
	};


	class Memory : public Resource {

	public:

		Memory(uint16_t id = 0): Resource(id, res::ResourceType::MEMORY) {}

		Memory(uint16_t id, uint64_t quantity)
			: Resource(id, res::ResourceType::MEMORY), quantity(quantity)
		{}

		inline uint64_t GetQuantity() const {
			return this->quantity;
		}

		inline void SetQuantity(uint64_t quantity) {
			this->quantity = quantity;
		}

		void SetType(res::ResourceType type) = delete;


	private:
		uint64_t quantity;


	};

	typedef std::shared_ptr<Memory> MemoryPtr_t;

	class MulticoreProcessor : public Resource {
	public:

		MulticoreProcessor(
				uint16_t id = 0,
				res::ResourceType type = res::ResourceType::ACCELERATOR):
			Resource(id, type)
		{}

		inline const std::string & GetArchitecture() const {
			return this->architecture;
		}

		inline void SetArchitecture(const std::string & arch) {
			this->architecture = arch;
		}

		inline const std::vector<ProcessingElement> & GetProcessingElementsAll() const {
			return this->pes;
		}

		inline std::vector<ProcessingElement> & GetProcessingElementsAll() {
			return this->pes;
		}


		inline void AddProcessingElement(ProcessingElement & pe) {
			this->pes.push_back(pe);
		}

		inline std::shared_ptr<Memory> GetMemory() const {
			return this->memory;
		}

		inline void SetMemory(std::shared_ptr<Memory> memory) {
			this->memory = memory;
		}

	private:
		std::string architecture;
		std::vector<ProcessingElement> pes;
		std::shared_ptr<Memory> memory;
	};

	typedef std::shared_ptr<MulticoreProcessor> MulticorePtr_t;

	class CPU : public MulticoreProcessor {
	public:
		CPU(uint16_t id = 0): MulticoreProcessor(id, res::ResourceType::CPU) {}

		inline uint16_t GetSocketId() const {
			return this->socket_id;
		}

		inline void SetSocketId(uint16_t socket_id) {
			this->socket_id = socket_id;
		}

		void SetType(res::ResourceType type) = delete;

	private:
		uint16_t socket_id;
	};


	class NetworkIF : public Resource {

	public:

		NetworkIF(uint16_t id, std::string name)
			: Resource(id, res::ResourceType::NETWORK_IF), name(name)
		{}

		inline std::string GetName() const {
			return this->name;
		}

		inline void SetName(std::string name) {
			this->name = name;
		}

		inline unsigned int GetFlags() const {
			return this->flags;
		}

		inline void SetFlags(unsigned int flags) {
			this->flags = flags;
		}

		inline void SetOnline(bool online) {
			this->online = online;
		}

		inline bool GetOnline() const {
			return this->online;
		}

		inline void SetAddress(std::shared_ptr<struct sockaddr> address) {
			this->address = address;
		}

		inline std::shared_ptr<struct sockaddr> GetAddress() const {
			return this->address;
		}


		void SetType(res::ResourceType type) = delete;


	private:
		bool online = false;
		unsigned int flags = 0;
		std::string name;

		std::shared_ptr<struct sockaddr> address;

	};

	typedef std::shared_ptr<NetworkIF> NetworkIF_t;

	class InterConnect : public Resource {

	public:

		InterConnect(uint16_t id)
			: Resource(id, res::ResourceType::INTERCONNECT)
		{}

		inline void SetBandwidth(uint64_t bandwidth) {
			this->bandwidth = bandwidth;
		}

		inline uint64_t GetBandwidth() const {
			return this->bandwidth;
		}


		void SetType(res::ResourceType type) = delete;


	private:

		uint64_t bandwidth;

	};

	typedef std::shared_ptr<InterConnect> InterConnect_t;

	class IO : public Resource {
	
	public:
		IO(uint16_t id = 0)
			: Resource(id, res::ResourceType::IO)
		{}

		inline void SetBandwidth(uint64_t bandwidth) {
			this->bandwidth = bandwidth;
		}

		inline uint64_t GetBandwidth() const {
			return this->bandwidth;
		}

		void SetType(res::ResourceType type) = delete;

	private:

		uint64_t bandwidth;

	};

	typedef enum StorageType {
	        HDD,
	        SSD,
	        SD,
	        FLASH,
	        CUSTOM
	} StorageType_t;

	class Storage : public IO {

	public:

		Storage(uint16_t id = 0)
			: IO(id)
		{}

		inline uint64_t GetQuantity() const {
			return this->quantity;
		}

		inline void SetQuantity(uint64_t quantity) {
			this->quantity = quantity;
		}

		inline StorageType_t GetStorageType() const {
			return this->storage_type;
		}

		inline void SetStorageType(StorageType_t type){
			this->storage_type = type;
		}

		void SetType(res::ResourceType type) = delete;

		private:

		uint64_t quantity;
		StorageType_t storage_type;

	};

	typedef std::shared_ptr<Storage> Storage_t;

	class System :  public Resource {
	public:

		System(uint16_t id = 0): Resource(id, res::ResourceType::SYSTEM) {}

		inline bool IsLocal() const {
			return this->local;
		}

		inline const std::string & GetHostname() const {
			return this->hostname;
		}

		inline const std::string & GetNetAddress() const {
			return this->net_address;
		}

		inline void SetLocal(bool local) {
			this->local = local;
		}

		inline void SetHostname(const std::string & hostname) {
			this->hostname = hostname;
		}

		inline void SetNetAddress(const std::string & net_address) {
			this->net_address = net_address;
		}

		inline const std::vector<CPU> & GetCPUsAll() const {
			return this->cpus;
		}

		inline std::vector<CPU> & GetCPUsAll() {
			return this->cpus;
		}

		inline void AddCPU(CPU & cpu) {
			this->cpus.push_back(cpu);
		}

		inline const std::vector<MulticoreProcessor> & GetGPUsAll() const {
			return this->gpus;
		}

		inline std::vector<MulticoreProcessor> & GetGPUsAll() {
			return this->gpus;
		}

		inline void AddGPU(MulticoreProcessor & gpu) {
			gpu.SetType(res::ResourceType::GPU);
			this->gpus.push_back(gpu);
		}

		inline const std::vector<MulticoreProcessor> & GetAcceleratorsAll() const {
			return this->accelerators;
		}

		inline std::vector<MulticoreProcessor> & GetAcceleratorsAll() {
			return this->accelerators;
		}

		inline void AddAccelerator(MulticoreProcessor & accelerator) {
			accelerator.SetType(res::ResourceType::ACCELERATOR);
			this->accelerators.push_back(accelerator);
		}

		inline const std::vector<MemoryPtr_t> & GetMemoriesAll() const {
			return this->memories;
		}

		inline std::vector<MemoryPtr_t> & GetMemoriesAll() {
			return this->memories;
		}

		MemoryPtr_t GetMemoryById(short id) const noexcept {
			for (auto x : memories) {
				if ( x->GetId() == id )
					return x;
			}

			return nullptr;
		}

		inline void AddMemory(MemoryPtr_t memory) {
			this->memories.push_back(memory);
		}

		inline const std::vector<NetworkIF_t> & GetNetworkIFsAll() const {
			return this->networkIFs;
		}

		inline std::vector<NetworkIF_t> & GetNetworkIFsAll() {
			return this->networkIFs;
		}

		inline void AddNetworkIF(NetworkIF_t networkIF) {
			this->networkIFs.push_back(networkIF);
		}

		inline const std::vector<InterConnect_t> & GetInterConnectsAll() const {
			return this->icns;
		}

		inline std::vector<InterConnect_t> & GetInterConnectsAll() {
			return this->icns;
		}

		inline void AddInterConnect(InterConnect_t icn) {
			this->icns.push_back(icn);
		}

		inline const std::vector<Storage_t> & GetStoragesAll() const {
			return this->storages;
		}

		inline std::vector<Storage_t> & GetStoragesAll() {
			return this->storages;
		}

		inline void AddStorage(Storage_t storage) {
			this->storages.push_back(storage);
		}

		void SetType(res::ResourceType type) = delete;

	private:

		bool local;
		std::string hostname;
		std::string net_address;

		std::vector <CPU> cpus;
		std::vector <MulticoreProcessor> gpus;
		std::vector <MulticoreProcessor> accelerators;
		std::vector <MemoryPtr_t> memories;
		std::vector <NetworkIF_t> networkIFs;
		std::vector <InterConnect_t> icns;
		std::vector <Storage_t> storages;
	};

	inline const System & GetLocalSystem() const {
		static std::shared_ptr<System> sys(nullptr);
		if (!sys) {
			for (auto & s_entry : this->GetSystemsAll()) {
				auto s = s_entry.second;
				if (s.IsLocal()) {
					sys = std::make_shared<System>(s);
				}
			}
			assert(sys);
		}
		return *sys;
	}
	inline System & GetLocalSystem() {
	        return const_cast<System&>(static_cast<const PlatformDescription*>(this)
							->GetLocalSystem());
	}

	inline const std::map<uint16_t, System> & GetSystemsAll() const {
		return this->systems;
	}

	inline std::map<uint16_t, System> & GetSystemsAll() {
		return this->systems;
	}

	inline void AddSystem(const System & sys) {
		this->systems.emplace(sys.GetId(), sys);
	}

	inline const System & GetSystem(uint16_t id) const {
		return systems.at(id);
	}

	inline bool ExistSystem(uint16_t id) const {
		return (systems.find(id) != systems.end());
	}

private:
	std::map<uint16_t, System> systems;

}; // class PlatformDescription

}   // namespace pp
}   // namespace bbque

#endif // BBQUE_PLATFORM_DESCRIPTION_H_
