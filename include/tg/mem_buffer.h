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

#ifndef BBQUE_TG_BUFFER_H_
#define BBQUE_TG_BUFFER_H_

#include <boost/serialization/list.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

namespace bbque
{

/**
 * \class Buffer
 * \brief This class represents a memory buffer from which one or more
 * tasks can read or write into
 */
class Buffer
{

public:

	/**
	 * \brief Constructor
	 */
	Buffer() {}

	/**
	 * \brief Buffer constructor
	 * \param _id Identification number
	 * \param _size Size
	 * \param _addr Address
	 */
	Buffer(uint32_t _id, size_t _size, uint64_t _addr = 0):
		id(_id), phy_addr(_addr), size_in_bytes(_size) { }

	/**
	 * \brief Destructor
	 */
	virtual  ~Buffer() {
		writer_tasks.clear();
		reader_tasks.clear();
	}

	/**
	 * \brief Identification number
	 */
	inline uint32_t Id() const {
		return id;
	}

	/**
	 * \brief Size of the buffer
	 */
	inline size_t Size() const {
		return size_in_bytes;
	}

	/**
	 * \brief Physical address
	 */
	inline uint32_t PhysicalAddress() const {
		return phy_addr;
	}

	/**
	 * \brief Set the physical address
	 * \param address The memory address
	 */
	inline void SetPhysicalAddress(uint64_t address) {
		phy_addr = address;
	}

	/**
	 * \brief Memory ID
	 */
	inline int MemoryBank() const {
		return mem_bank;
	}

	/**
	 * \brief Set the physical address
	 * \param address The memory address
	 */
	inline void SetMemoryBank(uint32_t bank) {
		mem_bank = bank;
	}

	/**
	 * \brief The list of tasks writing into the buffer
	 * \return A list of task ids
	 */
	inline const std::list<uint32_t> & WriterTasks() const {
		return writer_tasks;
	}

	/**
	 * \brief The list of tasks reading from the buffer
	 * \return A list of task ids
	 */
	inline const std::list<uint32_t> & ReaderTasks() const {
		return reader_tasks;
	}

	/**
	 * \brief Add a writer task
	 * \param task_id task id
	 */
	inline void AddWriterTask(uint32_t task_id) {
		writer_tasks.push_back(task_id);
	}

	/**
	 * \brief Add a reader task
	 * \param task_id task id
	 */
	inline void AddReaderTask(uint32_t task_id) {
		reader_tasks.push_back(task_id);
	}

	/**
	 * \brief Remove a writer task
	 * \param task_id task id
	 */
	inline void RemoveWriterTask(uint32_t task_id) {
		writer_tasks.remove(task_id);
	}

	/**
	 * \brief Remove a reader task
	 * \param task_id task id
	 */
	inline void RemoveReaderTask(uint32_t task_id) {
		reader_tasks.remove(task_id);
	}

	/**
	 * \brief Remove a task (reader or writer)
	 * \param task_id task id
	 */
	inline void RemoveTask(uint32_t task_id) {
		RemoveWriterTask(task_id);
		RemoveReaderTask(task_id);
	}

	/**
	 * \brief Event associated to the buffer (used for synchronization
	 * purposes
	 * \return The event id number
	 */
	inline uint32_t Event() const {
		return event_id;
	}

	/**
	 * \brief Sert the event associated to the buffer
	 * \param id The event id number
	 */
	inline void SetEvent(uint32_t id) {
		event_id = id;
	}


private:

	uint32_t id;

	uint32_t phy_addr;

	size_t size_in_bytes;

	int32_t mem_bank = 0;


	std::list<uint32_t> writer_tasks;

	std::list<uint32_t> reader_tasks;


	uint32_t event_id;


	friend class boost::serialization::access;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version) {
		(void) version;
		ar & id;
		ar & phy_addr;
		ar & size_in_bytes;
		ar & mem_bank;
		ar & writer_tasks;
		ar & reader_tasks;
		ar & event_id;
	}
};

} // bbque

#endif // ifndef BBQUE_TG_BUFFER_H
