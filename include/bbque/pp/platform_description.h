#ifndef BBQUE_PLATFORM_DESCRIPTION_H_
#define BBQUE_PLATFORM_DESCRIPTION_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef UINT64_MAX
#define BBQUE_PP_ARCH_SUPPORTS_INT64 1
#else
#define BBQUE_PP_ARCH_SUPPORTS_INT64 0
#endif

namespace bbque {
namespace pp {

class PlatformDescription {

public:
    typedef enum PartitionType {
        HOST,
        MDEV,
        SHARED
    } PartitionType_t;

    class ProcessingElement {
    public:

        ProcessingElement(uint16_t id, uint16_t core_id, uint8_t share, PartitionType_t ptype)
            : id(id), core_id(core_id), share(share), ptype(ptype)
        {}

        inline uint16_t GetId() const {
            return this->id;
        }

        inline void SetId(uint16_t id) {
            this->id = id;
        }

        inline uint16_t GetCoreId() const {
            return this->core_id;
        }

        inline void SetCoreId(uint16_t core_id) {
            this->core_id = core_id;
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

    private:
        uint16_t id;
        uint16_t core_id;
        uint8_t share;
        PartitionType_t ptype;

    };

    class Memory {

    public:
    #if BBQUE_PP_ARCH_SUPPORTS_INT64
        Memory(uint16_t id, uint64_t quantity)
            : id(id), quantity(quantity)
        {}
    #else
        Memory(uint16_t id, uint32_t quantity_hi, uint32_t quantity_lo)
            : id(id), quantity_hi(quantity_hi), quantity_lo(quantity_lo)
        {}
    #endif
        inline uint16_t GetId() const {
            return this->id;
        }

        inline void SetId(uint16_t id) {
            this->id = id;
        }

    #if BBQUE_PP_ARCH_SUPPORTS_INT64
        inline uint64_t GetQuantity() const {
            return this->quantity;
        }

        inline void SetQuantity(uint64_t quantity) {
            this->quantity = quantity;
        }
    #else
        inline uint32_t GetQuantityLO() const {
            return this->quantity_lo;
        }

        inline void SetQuantityLO(uint32_t quantity) {
            this->quantity_lo = quantity;
        }
        inline uint32_t GetQuantityHI() const {
            return this->quantity_hi;
        }

        inline void SetQuantityHI(uint32_t quantity) {
            this->quantity_hi = quantity;
        }

    #endif

    private:
        uint16_t id;
    #if BBQUE_PP_ARCH_SUPPORTS_INT64
        uint64_t quantity;
    #else
        uint32_t quantity_lo;
        uint32_t quantity_hi;
    #endif
    };

    typedef std::shared_ptr<Memory> MemoryPtr_t;

    class GenericCPU {

        inline const std::string & GetArchitecture() const {
            return this->architecture;
        }

        inline void SetArchitecture(const std::string & arch) {
            this->architecture = arch;
        }

        inline uint16_t GetId() const {
            return this->id;
        }

        inline void SetId(uint16_t id) {
            this->id = id;
        }

        inline const std::vector<ProcessingElement>& GetProcessingElementsAll() const {
            return this->pes;
        }

        inline std::vector<ProcessingElement>& GetProcessingElementsAll() {
            return this->pes;
        }


        inline void AddProcessingElement(const ProcessingElement& pe) {
            this->pes.push_back(pe);
        }

    private:
        std::string architecture;
        uint16_t id;
        std::vector<ProcessingElement> pes;
    };

    class CPU : public GenericCPU {
    public:
        inline uint16_t GetSocketId() const {
            return this->socket_id;
        }

        inline void SetSocketId(uint16_t socket_id) {
            this->socket_id = socket_id;
        }

        inline std::shared_ptr<Memory> GetMemory() const {
            return this->memory;
        }

        inline void SetMemory(std::shared_ptr<Memory> memory) {
            this->memory = memory;
        }

    private:
        uint16_t socket_id;
        std::shared_ptr<Memory> memory;

    };


    class System {

        inline bool IsLocal() const {
            return this->local;
        }

        inline const std::string& GetHostname() const {
            return this->hostname;
        }

        inline const std::string& GetNetAddress() const {
            return this->net_address;
        }

        inline void SetLocal(bool local) {
            this->local = local;
        }

        inline void GetHostname(const std::string& hostname) {
            this->hostname = hostname;
        }

        inline void GetNetAddress(const std::string& net_address) {
            this->net_address = net_address;
        }

        inline const std::vector<CPU>& GetCPUsAll() const {
            return this->cpus;
        }

        inline std::vector<CPU>& GetCPUsAll() {
            return this->cpus;
        }

        inline void AddCPU(const CPU& cpu) {
            this->cpus.push_back(cpu);
        }

        inline const std::vector<GenericCPU>& GetAcceleratorsAll() const {
            return this->accelerators;
        }

        inline std::vector<GenericCPU>& GetAcceleratorsAll() {
            return this->accelerators;
        }

        inline void AddAccelerator(const GenericCPU& accelerator) {
            this->accelerators.push_back(accelerator);
        }

        inline const std::vector<MemoryPtr_t>& GetMemories() const {
            return this->memories;
        }

        inline std::vector<MemoryPtr_t>& GetMemoryAll() {
            return this->memories;
        }

        inline void AddMemory(const MemoryPtr_t& memory) {
            this->memories.push_back(memory);
        }

    private:

        bool local;
        std::string hostname;
        std::string net_address;

        std::vector <CPU> cpus;
        std::vector <GenericCPU> accelerators;
        std::vector <MemoryPtr_t> memories;
    };

    inline const std::vector<System>& GetSystemsAll() const {
        return this->systems;
    }

    inline std::vector<System>& GetSystemsAll() {
        return this->systems;
    }

    inline void AddSystem(const System& sys) {
        this->systems.push_back(sys);
    }

private:
    std::vector <System> systems;

}; // class PlatformDescription

}   // namespace pp
}   // namespace bbque

#endif // BBQUE_PLATFORM_DESCRIPTION_H_
