/*
 * Copyright (C) 2012  Politecnico di Milano
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

#ifndef BBQUE_WORKING_MODE_STATUS_IF_H_
#define BBQUE_WORKING_MODE_STATUS_IF_H_

#include <bitset>
#include <list>
#include <map>
#include <memory>

#include "bbque/res/identifier.h"
#include "bbque/utils/extra_data_container.h"
#include "bbque/utils/utility.h"

namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque {

namespace res {

class Resource;
class ResourcePath;
class ResourceBitset;
typedef size_t RViewToken_t;
typedef std::shared_ptr<ResourcePath> ResourcePathPtr_t;

class ResourceAssignment;

typedef std::shared_ptr<ResourceAssignment> ResourceAssignmentPtr_t;
typedef std::map<ResourcePathPtr_t, ResourceAssignmentPtr_t, CompareSP<ResourcePath> >
	ResourceAssignmentMap_t;
typedef std::shared_ptr<ResourceAssignmentMap_t> ResourceAssignmentMapPtr_t;
}

namespace app {

class Schedulable;
class WorkingModeStatusIF;

/** Shared pointer to the class here defined */
using SchedPtr_t = std::shared_ptr<Schedulable>;
using AwmSPtr_t  = std::shared_ptr<WorkingModeStatusIF>;

/**
 * @class WorkingMode
 * @brief Read-only interface for the WorkingMode runtime status
 */
class WorkingModeStatusIF: public bu::ExtraDataContainer {

public:

	/**
	 * @brief Error codes returned by methods
	 */
	enum ExitCode_t {
		/** Success */
		WM_SUCCESS = 0,
		/** Application working mode not found */
		WM_NOT_FOUND,
		/** Resource not found */
		WM_RSRC_NOT_FOUND,
		/** Resource usage request exceeds the total availability */
		WM_RSRC_USAGE_EXCEEDS,
		/** Resource name error */
		WM_RSRC_ERR_TYPE,
		/** Missing some resource bindings */
		WM_RSRC_MISS_BIND,
		/** Exceeded resource binding ID */
		WM_BIND_ID_OVERFLOW,
		/** Trying to schedule invalid resource binding */
		WM_BIND_FAILED
	};

	/**
	 * @brief Return the identifying name of the AWM
	 */
	virtual std::string const & Name() const = 0;

	/**
	 * @brief Working Mode ID
	 * @return An integer number
	 */
	virtual int32_t Id() const = 0;

	/**
	 * @brief Get the application owning the working mode
	 * @return A shared pointer to the application descriptor
	 */
	virtual SchedPtr_t const & Owner() const = 0;

	/**
	 * @brief Get the static QoS value
	 * @return A value in the [0..1] range
	 */
	virtual float Value() const = 0;

	/**
	 * @brief Get the (normalized) configuration time profiled
	 * @return A value in the [0..1] range
	 */
	virtual float ConfigTime() const = 0;

	/**
	 * @brief A string identifier for log messaging purpose
	 */
	virtual const char * StrId() const = 0;

	/**
	 * @brief A specific resource request amount
	 * @param r_path Resource path object (shared pointer)
	 * @return The requested amount
	 */
	virtual uint64_t RequestedAmount(br::ResourcePathPtr_t r_path) const = 0;

	/**
	 * @brief Return a map of all the requested resources
	 * @return A constant reference to the map of resource assignments object
	 */
	virtual br::ResourceAssignmentMap_t const & ResourceRequests() const = 0;

	/**
	 * @brief How many resources the working mode uses
	 * @return The number of resource assignments
	 */
	virtual size_t NumberOfResourceRequests() const = 0;

	/**
	 * @brief Current resource assignments bound with the system resources
	 *
	 * This is the map of resource assignments that should be built by the
	 * scheduling policy. If the method returns a null pointer then the
	 * resources have been set for being allocated yet.
	 *
	 * @param b_id A binding identifier. This should be specified if the
	 * scheduling policy aims to handle more than one binding. Currently the
	 * range of acceptable values goes from 0 to 255.
	 *
	 * @return A map of Usage objects
	 */
	virtual br::ResourceAssignmentMapPtr_t GetSchedResourceBinding(uint32_t b_id = 0) const = 0;

	/**
	 * @brief Get the bitmap of the clusters currently used.
	 *
	 * Eeach bit set represents a cluster in use. When SetResourceBinding() is
	 * called the set of clusters is properly filled.
	 *
	 * @return A bitset data structure
	 */
	virtual br::ResourceBitset BindingSet(const br::ResourceType & r_type) const = 0;

	/**
	 * @brief Get the bitmap of the clusters previously used.
	 *
	 * When SetResourceBinding() is called the set of clusters is
	 * saved as "prev".
	 *
	 * @return A bitset data structure
	 */
	virtual br::ResourceBitset BindingSetPrev(const br::ResourceType & r_type) const = 0;

	/**
	 * @brief Check if clusters used are changed
	 *
	 * When a new resource binding is set, if the clusters map has changed keep
	 * track of the change by setting a boolean variable. When the method is
	 * call it returns the value of such variale. This is more efficient than
	 * checking if curr != prev every time.
	 *
	 * @return True if the AWM use a different map of clusters, false otherwise.
	 */
	virtual bool BindingChanged(const br::ResourceType & r_type) const = 0;

};

} // namespace app

} // namespace bbque

#endif // BBQUE_WORKING_MODE_STATUS_IF_H_
