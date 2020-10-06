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

#ifndef BBQUE_TG_REQS_H
#define BBQUE_TG_REQS_H

#include "tg/hw.h"

namespace bbque {

/**
 * @class TaskRequirements
 * @brief This includes the set of performance requirements associated to a task
 */
class TaskRequirements {

public:

	TaskRequirements(): throughput(0.0), ctime_ms(0) {}

	TaskRequirements(float t, uint32_t ct, uint32_t inb, uint32_t outb):
			throughput(t), ctime_ms(ct) {
		bandwidth.in_kbps  = inb;
		bandwidth.out_kbps = outb;
	}

	virtual ~TaskRequirements() {
		hw_prefs.clear();
	}

	/**
	 * @brief Throughput: data fetching + processing + data output writing
	 * @return Number of processing cycles per second
	 */
	inline float Throughput() const noexcept { return throughput; }

	/**
	 * @brief Task completion time required
	 * @return Time in milliseconds
	 */
	inline uint32_t CompletionTime() const noexcept { return ctime_ms; }


	/**
	 * @brief Input bandwidth: data fetching rate (read memory)
	 * @return Kbps value
	 */
	inline uint32_t InBandwidth() const noexcept { return bandwidth.in_kbps; }

	/**
	 * @brief Output bandwidth: data output writing rate (write memory)
	 * @return Kbps value
	 */
	inline uint32_t OutBandwidth() const noexcept { return bandwidth.out_kbps; }

	/**
	 * \brief Get the selected HW architecture for this task  by the policy
	 */
	inline Bandwidth_t GetAssignedBandwidth() const {
		return bandwidth;
	}


	/**
	 * @brief The task (kernel) target architectures in ascendent order of
	 * preferences
	 * @return a reference to the vector of target architectures
	 */
	inline std::vector<ArchType> const & ArchPreferences() const {
		return hw_prefs;
	}

	/**
	 * @brief The preferences related to HW architecture types
	 * @param level preference level or priority
	 * @return HW architecture type
	 */
	inline ArchType ArchPreference(size_t level) const {
		if (level >= hw_prefs.size())
			return ArchType::NONE;
		return hw_prefs[level];
	}

	/**
	 * @brief Append a HW architecture preference
	 * @param HW architecture type
	 */
	inline void AddArchPreference(ArchType arch_type) {
		hw_prefs.push_back(arch_type);
	}

	/**
	 * @brief Number of HW architecture preference
	 * @param How many HW architecture preferences have been specified
	 */
	inline size_t NumArchPreferences() const noexcept {
		return hw_prefs.size();
	}


private:

	float throughput;

	uint32_t ctime_ms;

	Bandwidth_t bandwidth;

	std::vector<ArchType> hw_prefs;
};

} // namespace bbque

#endif // BBQUE_TG_REQS_H

