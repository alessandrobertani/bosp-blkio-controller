/*
 * Copyright (C) 2016  Politecnico di Milano
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

#ifndef BBQUE_RESOURCE_ASSIGNMENT_H_
#define BBQUE_RESOURCE_ASSIGNMENT_H_

#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "bbque/res/bitset.h"
#include "bbque/res/resources.h"
#include "bbque/utils/utility.h"

namespace bbque
{
namespace res
{

// Forward declaration
class ResourceAssignment;
class ResourcePath;

/** Shared pointer to Usage object */
using ResourceAssignmentPtr_t = std::shared_ptr<ResourceAssignment>;
/** Map of Usage descriptors. Key: resource path */
using ResourceAssignmentMap_t = std::map<ResourcePathPtr_t, ResourceAssignmentPtr_t, CompareSP<ResourcePath>>;
/** Constant pointer to the map of Usage descriptors */
using ResourceAssignmentMapPtr_t = std::shared_ptr<ResourceAssignmentMap_t>;

/**
 * @class ResourceAssignment
 * @brief How the resource requests are bound into assignments
 *
 * An application working modes defines a set of this resource requests
 * (then assignments).
 *
 * This class includes the following information:
 *
 * 1) the amount of requested resource;
 * 2) a list containing all the descriptors (shared pointer) of the resources
 * which to this request/assignment refers to - we expect that such list is
 * filled by a method of ResourceAccounter, after that Scheduler/Optimizer has
 * solved the resource binding;
 * 3) the power configuration (if set) to apply in phase of resource mapping
 *
 * The "resource binding" can be explained as follows: if a Working Mode
 * includes resource requests like "sys.cpu2.pe = 4", the scheduler/optimizer
 * must assign 4 processing element under the same CPU (2), that therefore must
 * be bound to one of the real CPUs available on the platform.
 * Thus, after that, the list "resources", will contain the descriptors of the
 * resources "pe (processing elements)" in the CPU assigned by the
 * scheduler/optimizer module.
 */
class ResourceAssignment {

friend class bbque::ResourceAccounter;

public:

	/**
	 * @enum ExitCode_t
	 */
	enum ExitCode_t {
	        /** Success */
	        RU_OK = 0,
	        /** Application pointer is null */
	        RU_ERR_NULL_POINTER,
	        /** Application pointer mismatch */
	        RU_ERR_APP_MISMATCH,
	        /** Resource state view token mismatch */
	        RU_ERR_VIEW_MISMATCH
	};

	/**
	 * @enum class Policy
	 *
	 * The usage must be bind to a set of physical resources. This can be
	 * done by the resource allocation policy through a per-resource
	 * fine-grained binding, or can be left to the Resource Accounter,
	 * specifying a predefined "filling" policy. The policy defines how the
	 * amount of resource required must be spread over the set of physical
	 * resources that the Usage object is referencing.
	 */
	enum class Policy
	{
	        /**
		 * Usage should be distributed over the resource list in a
		 * sequential manner
	         */
	        SEQUENTIAL,
	        /**
	         * Usage should be evenly distributed over all the resources in the
	         * list
	         */
	        BALANCED
	};

	/**
	 * @class PowerSettings
	 *
	 * @brief Power settings as of required via recipe or set by scheduling
	 * policy.
	 * freq_governor is the specific governor to set
	 * freq_khz is the operating frequency in KHz
	 * perf_state is a number [0,inf) to indicate the operating state of the
	 * resource
	 */
	class PowerSettings {
	public:
		PowerSettings():
			freq_governor(""),
			freq_khz(0),
			perf_state(0) {
		}

		PowerSettings(
			std::string const & gov,
			uint32_t freq,
			uint32_t pstate):

			freq_governor(gov),
			freq_khz(freq),
			perf_state(pstate) {
		}

		std::string freq_governor;
		uint32_t freq_khz;
		uint32_t perf_state;
	};

	/**
	 * @brief Constructor

	 * @param amount The amount of resource usage
	 * @param policy The filling policy
	 */
	ResourceAssignment(uint64_t amount, Policy policy = Policy::SEQUENTIAL);

	/**
	 * @brief Destructor
	 */
	~ResourceAssignment();

	/**
	 * @brief The amount of resource required/assigned to the
	 * Application/EXC
	 *
	 * @return The amount of resource
	 */
	inline uint64_t GetAmount() {
		return amount;
	}

	/**
	 * @brief Set the amount of resource
	 */
	inline void SetAmount(uint64_t value) {
		amount = value;
	}

	/**
	 * @brief Set a new power configuration to apply
	 */
	inline void SetPowerSettings(PowerSettings new_settings) {
		power_config = new_settings;
	}

	/**
	 * @brief Get the currently set power configuration
	 */
	inline PowerSettings const & GetPowerSettings() {
		return power_config;
	}

	/**
	 * @brief Get the entire list of resources
	 *
	 * @return A reference to the resources list
	 */
	inline ResourcePtrList_t & GetResourcesList() {
		return resources;
	}

	/**
	 * @brief Set the list of resources
	 *
	 * Commonly a Usage object specifies the request of a specific type of
	 * resource, which can be bound on a set of platform resources (i.e.
	 * "sys0,cpu2.pe" -> "...cpu2.pe{0|1|2|...}".  The resources list
	 * includes the pointers to all the resource descriptors that can
	 * satisfy the request. The method initialises the iterators pointing
	 * to the set of resources effectively granted to the Application/EXC.
	 *
	 * @param r_list The list of resource descriptor for binding
	 */
	void SetResourcesList(ResourcePtrList_t & r_list);

	/**
	 * @brief Set the a filtered list of resources
	 *
	 * Commonly a Usage object specifies the request of a specific type of
	 * resource, which can be bound on a set of platform resources (i.e.
	 * "sys0,cpu2.pe" -> "...cpu2.pe{0|1|2|...}".
	 *
	 * The resources list, in this case, includes the pointers to a subset
	 * of the resource descriptors that can satisfy the request. This
	 * subset is built on the bases of two parameters that allow the
	 * definition of a filter criteria.
	 *
	 * Considering the example above, for the path "sys0,.cpu2.pe" i can
	 * specify a filtered list of processing elements, such that it
	 * includes only PE2 and PE3: "sys0.cpu2.pe" -> "sys0cpu2.pe{2|3}"
	 *
	 * @param r_list The list of resource descriptor for binding
	 * @param filter_rtype The type of resource on which apply the
	 * filter
	 * @param filter_mask A bitmask where in the set bits represents the
	 * resource ID to include in the list
	 */
	void SetResourcesList(
	        ResourcePtrList_t & r_list,
	        ResourceType filter_rtype,
	        ResourceBitset & filter_mask);

	void SetResourcesList(
	        ResourcePtrList_t & r_list,
	        ResourceBitset const & filter_mask);


	/**
	 * @brief Check of the resource binding list is empty
	 *
	 * @return true if the list is empty, false otherwise.
	 */
	inline bool EmptyResourcesList() const {
		return resources.empty();
	}

	/**
	 * @brief Set the resources list filling policy
	 */
	inline void SetPolicy(Policy policy) {
		fill_policy = policy;
	}

	/**
	 * @brief Get the resources list filling policy
	 *
	 * @return The policy set
	 */
	inline Policy GetPolicy() const {
		return fill_policy;
	}

	/**
	 * @brief The mask representing the resources actually included in the
	 * assignment
	 *
	 * @return The bitset mask with set-to-1 bits representing IDs of the
	 * resources set included in the current assignment
	 */
	inline ResourceBitset & GetMask() {
		return mask;
	}

private:

	/** Usage amount request */
	uint64_t amount = 0;

	/** List of resource descriptors which to the resource usage is bound */
	ResourcePtrList_t resources;

	/** Power configuration to apply for the resource assignment */
	PowerSettings power_config;

	/**
	 * The resources list filling policy, i.e., how the resource amount
	 * should be distributed over the resources list
	 */
	Policy fill_policy;

	/**
	 * A bitmask keeping track of the assigned/requested resources id
	 * numbers
	 */
	ResourceBitset mask;

	/** The application/EXC owning this resource usage */
	SchedPtr_t owner_app;

	/** The token referencing the state view of the resource usage */
	RViewToken_t status_view = 0;

};

} // namespace res

} // namespace bbque

#endif // BBQUE_RESOURCE_USAGE_H
