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

#ifndef BBQUE_TG_TASK_H_
#define BBQUE_TG_TASK_H_

#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/string.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "tg/hw.h"
#include "tg/profilable.h"

namespace bbque {


using ArchMap_t = std::map<ArchType, std::shared_ptr<ArchInfo>>;


class Task: public Profilable {

public:

	/**
	 * \brief Task constructor
	 */
	Task() {}

	/**
	 * \brief Task constructor
	 * \param _id Task id number
	 * \param _nthreads Number of threads to spawn
	 * \param _name Task name (optional)
	 */
	Task(uint32_t _id, int _nthreads = 1, std::string const & _name = ""):
		id(_id), name(_name), thread_count(_nthreads) { }

	/**
	 * \brief Task constructor
	 * \param _id Task id number
	 * \param _inb Input buffers id list
	 * \param _outb Output buffers id list
	 * \param _nthreads Number of threads to spawn
	 * \param _name Task name (optional)
	 */
	Task(uint32_t _id,
			std::list<uint32_t> & _inb, std::list<uint32_t> & _outb,
			int _nthreads = 1, std::string const & _name = ""):
		id(_id), name(_name), thread_count(_nthreads),
		in_buffers(_inb), out_buffers(_outb) { }

	/**
	 * \brief Destructor
	 */
	virtual ~Task() {
		in_buffers.clear();
		out_buffers.clear();
	}

	/**
	 * \brief Task identification number
	 * \return The id number
	 */
	inline uint32_t Id() const { return id; }

	/**
	 * \brief Task name (optional)
	 * \return A string object with the task name
	 */
	inline std::string const & Name() const { return name; }


	/**
	 * \brief Number of threads spawned by this task (kernel)
	 * \note This is a useful information for the resource manager in
	 * charge of assigning a computing unit to the task
	 * \return The number of threads
	 */
	inline int GetThreadCount() const { return thread_count; }

	/**
	 * \brief Set the number of threads spawned by this task (kernel)
	 * \param nr_threads The number of threads
	 */
	inline void SetThreadCount(int nr_threads) { thread_count = nr_threads; }

	/**
	 * \brief Set the system node assigned to the task
	 * \param p_id Identification number of the processing unit
	 */
	inline void SetAssignedSystem(int sys_id) { system_node_id = sys_id; }

	/**
	 * \brief Get the system node assigned to the task
	 * \return The identification number of the system node (-1 if not
	 * assigned)
	 */
	inline int GetAssignedSystem() const { return system_node_id; }

	inline void SetAssignedSystemIp(const std::string& ip) { system_node_ip = ip; }

	inline std::string const GetAssignedSystemIp() const { return system_node_ip; }

	/**
	 * \brief Set the processor assigned to the task
	 * \param p_id Identification number of the processing unit
	 */
	inline void SetAssignedProcessor(int p_id) { processor_id = p_id; }

	/**
	 * \brief Get the processor assigned to the task
	 * \return The identification number of the processing unit (-1 if not
	 * assigned)
	 */
	inline int GetAssignedProcessor() const { return processor_id; }

	/**
	 * \brief Get the number of assigned cores
	 * \param The number of assigned cores
	 */
	inline void SetAssignedCoresCount(int nr) { nr_cores = nr; }

	/**
	 * \brief Get the number of assigned cores
	 * \return The number of assigned cores (0 if not assigned)
	 */
	inline int GetAssignedCoresCount() const { return nr_cores; }

	/**
	 * \brief Set the amount of interconnect bandwidth reserved to this task
	 * \param bw data structure including in/out bandwidth information
	 */
	inline void SetAssignedBandwidth(Bandwidth_t bw) {
		assigned_bandwidth = bw;
	}

	/**
	 * \brief Get the amount of interconnect bandwidth reserved to this task
	 * \return data structure including in/out bandwidth information
	 */
	inline Bandwidth_t GetAssignedBandwidth() const {
		return assigned_bandwidth;
	}


	/**
	 * \brief Add a buffer (id) to which read from
	 * \param buff_id Identification number of the buffer
	 */
	inline void AddInputBuffer(uint32_t buff_id) {
		in_buffers.push_back(buff_id);
	}

	/**
	 * \brief Add a buffer (id) to which write to
	 * \param buff_id Identification number of the buffer
	 */
	inline void AddOutputBuffer(uint32_t buff_id) {
		out_buffers.push_back(buff_id);
	}

	/**
	 * \brief Buffers (id) to read from
	 * \return the list of input buffers id
	 */
	inline const std::list<uint32_t> & InputBuffers() const {
		return in_buffers;
	}

	/**
	 * \brief Buffers (id) to write to
	 * \return the list of output buffers id
	 */
	inline const std::list<uint32_t> & OutputBuffers() const {
		return out_buffers;
	}

	/**
	 * \brief Event (if) for synchronization purposes
	 * \return the event identification number
	 */
	inline int32_t Event() const { return event_id; }

	/**
	 * \brief Set the id of the event used synchronization purposes
	 * \param id the event identification number
	 */
	inline void SetEvent(int32_t id) { event_id = id; }


	/**
	 * \brief Add a HW target architecture supported (i.e., for which
	 * the binary is available)
	 * \param arch Type of HW architecture
	 * \param prio If the application specified a preference for
	 * each target. priority=0 means highest preference value.
	 */
	inline void AddTarget(
		ArchType arch,
		uint8_t _prio = 0,
		size_t _addr  = 0,
		size_t _bs    = 0,
		size_t _ss    = 0) {

		hw_targets.emplace(arch,
			std::make_shared<ArchInfo>(_prio, _addr, _bs, _ss));
	}

	/**
	 * \brief Add a HW target architectures supported (i.e., for which
	 * the binary is available)
	 * \return A reference to the map of target architectures
	 */
	inline ArchMap_t & Targets() { return hw_targets; }

	/**
	 * \brief Remove a HW target architecture supported (i.e., for
	 * which the binary is available)
	 * \param arch Type of HW architecture
	 */
	inline void RemoveTarget(ArchType arch) {
		hw_targets.erase(arch);
	}

	/**
	 * \brief Set the selected HW architecture for this task  by the policy
	 * \param arch Type of HW architecture
	 */
	inline void SetAssignedArch(ArchType arch) {
		assigned_arch = arch;
	}

	/**
	 * \brief Get the selected HW architecture for this task  by the policy
	 */
	inline ArchType GetAssignedArch() const {
		return assigned_arch;
	}



private:

	uint32_t id = 0;

	std::string name;

	int thread_count = 0;


	int processor_id = -1;

	int system_node_id = -1;

	std::string system_node_ip;

	int nr_cores = 0;


	ArchType assigned_arch;

	Bandwidth_t assigned_bandwidth;


	std::list<uint32_t> in_buffers;

	std::list<uint32_t> out_buffers;


	int32_t event_id = -1;

	ArchMap_t hw_targets;


	friend class boost::serialization::access;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version) {
		(void) version;
	        ar & boost::serialization::base_object<Profilable>(*this);
		ar & id;
		ar & name;
		ar & thread_count;
		ar & processor_id;
		ar & system_node_id;
		ar & nr_cores;
		ar & assigned_arch;
		ar & in_buffers;
		ar & out_buffers;
		ar & event_id;
		ar & hw_targets;
	}
};

} //  namespace bbque

#endif // BBQUE_TG_TASK_H_
