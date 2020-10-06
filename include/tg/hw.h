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

#ifndef BBQUE_TG_HW_H
#define BBQUE_TG_HW_H

#include "bbque/utils/string_utils.h"
#include "bbque/utils/utility.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

namespace bbque {


typedef enum class ArchType {
	NONE,
	// CPUs
	CPU, // Generic test CPU
	X86,
	X86_64,
	ARM_V7,
	ARM_V8,
	// GPUs
	NVIDIA,
	// MANGO accelerators
	GN, // Emulated accelerator
	PEAK,
	NUPLUS,
	DCT,
	TETRAPOD,

	STOP
} ArchType_t;

typedef struct Bandwidth_t {
	uint32_t in_kbps  = 0;
	uint32_t out_kbps = 0;
} Bandwidth_t;

inline ArchType GetArchTypeFromString(std::string const & str) {
	std::string arch_str(utils::UpperString(str));
	switch(ConstHashString(arch_str.c_str())) {

		case ConstHashString("CPU"):
			return ArchType::CPU;
		case ConstHashString("X86"):
			return ArchType::X86;
		case ConstHashString("X86_64"):
			return ArchType::X86_64;
		case ConstHashString("ARM_V7"):
			return ArchType::ARM_V7;
		case ConstHashString("ARM_V8"):
			return ArchType::ARM_V8;

		case ConstHashString("NVIDIA"):
			return ArchType::NVIDIA;

		case ConstHashString("GN"):
			return ArchType::GN;
		case ConstHashString("PEAK"):
			return ArchType::PEAK;
		case ConstHashString("NUP"):
		case ConstHashString("NUPLUS"):
			return ArchType::NUPLUS;
		case ConstHashString("DCT"):
			return ArchType::DCT;
		case ConstHashString("TETRAPOD"):
			return ArchType::TETRAPOD;
		default:
			return ArchType::NONE;
	}

	return ArchType::NONE;
}

inline const char* GetStringFromArchType(ArchType type) {
	switch(type) {
		case ArchType::NONE:  return "NONE";

		case ArchType::GN: return "GN";

		case ArchType::CPU:    return "CPU";
		case ArchType::X86:    return "X86";
		case ArchType::X86_64: return "X86_64";
		case ArchType::ARM_V7: return "ARM_V7";
		case ArchType::ARM_V8: return "ARM_V8";

		case ArchType::NVIDIA: return "NVIDIA";

		case ArchType::PEAK:  return "PEAK";
		case ArchType::NUPLUS:return "NUP";
		case ArchType::DCT:   return "DCT";
		case ArchType::TETRAPOD:  return "TETRAPOD";

		default:  return "UNKW";
	}
}

class ArchInfo {

public:

	ArchInfo():
		priority(0), address(0), binary_size(0), stack_size(0) {
	}

	ArchInfo(uint8_t _prio, size_t _addr, size_t _bins, size_t _ss):
		priority(_prio), address(_addr),
		binary_size(_bins), stack_size(_ss) {
	}


	/**
	 * \brief Set the priority of HW target architecture supported
	 * \param priority If the application specified a preference for
	 * each target. priority=0 means highest preference value.
	 */
	inline void SetPriority(uint8_t _prio) noexcept { priority = _prio; }

	/**
	 * \brief Get the priority of HW target architecture supported
	 * \param priority If the application specified a preference for
	 * each target. priority=0 means highest preference value.
	 */
	inline uint8_t Priority() const noexcept { return priority; }


	/**
	 * \brief The memory address to which deploy the task binary
	 * \return The memory address
	 */
	inline uint32_t Address() const noexcept { return address; }

	/**
	 * \brief Set the memory address to which deploy the task binary
	 * \param _addr The memory address to set
	 */
	inline void SetAddress(uint32_t _addr) noexcept { address = _addr; }

	/**
	 * \brief The memory address to which deploy the task binary
	 * \return The memory address
	 */
	inline uint32_t MemoryBank() const noexcept { return mem_bank; }

	/**
	 * \brief Set the memory address to which deploy the task binary
	 * \param _addr The memory address to set
	 */
	inline void SetMemoryBank(uint32_t bank) noexcept { mem_bank = bank; }


	/**
	 * \brief The current task binary size
	 * \return The memory address
	 */
	inline uint32_t BinarySize() const noexcept { return binary_size; }

	/**
	 * \brief Set the current task binary size
	 * \return The memory address
	 */
	inline void SetBinarySize(uint32_t _binsize) noexcept { binary_size = _binsize; }


	/**
	 * \brief The stack size for the task execution
	 * \return The memory stack size
	 */
	inline uint32_t StackSize() const noexcept { return stack_size; }

	/**
	 * \brief Set the stack size for the task executio
	 * \param _ss The memory stack size to set
	 */
	inline void SetStackSize(uint32_t _ss) noexcept { stack_size = _ss; }


protected:

	uint8_t priority;

	uint32_t address;
	uint32_t mem_bank;

	uint32_t binary_size;

	uint32_t stack_size;


	friend class boost::serialization::access;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version) {
		(void) version;
		ar & priority;
		ar & address;
		ar & mem_bank;
		ar & binary_size;
		ar & stack_size;
	}
};


}


#endif // BBQUE_TG_HW_H
