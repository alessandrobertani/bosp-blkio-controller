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

#ifndef BBQUE_RESOURCE_ACCOUNT_STATUS_H_
#define BBQUE_RESOURCE_ACCOUNT_STATUS_H_

#include <list>
#include <map>
#include <memory>
#include <string>

#include "bbque/res/identifier.h"
#include "bbque/res/resource_assignment.h"

namespace bbque
{

namespace res
{
class Resource;
class ResourcePath;
typedef std::shared_ptr<ResourcePath> ResourcePathPtr_t;

/** Resource state view token data type */
typedef size_t RViewToken_t;
typedef std::shared_ptr<Resource> ResourcePtr_t;
typedef std::list<ResourcePtr_t> ResourcePtrList_t;
}
namespace br = bbque::res;

namespace app
{
class ApplicationStatusIF;
typedef std::shared_ptr<ApplicationStatusIF> AppSPtr_t;
}
namespace ba = bbque::app;

/**
 * @class ResourceAccounterStatusIF
 *
 * @brief Resource accounting data
 *
 * This definition provide the read-only status interface for interactions
 * between resource accounter and "periferical" components of the RTRM (i.e.
 * the RecipeLoader) for resource information querying.
 */
class ResourceAccounterStatusIF
{

public:

	virtual ~ResourceAccounterStatusIF() {};

	/**
	 * @brief Exit codes
	 */
	enum ExitCode_t {
		/** Successful return  */
		RA_SUCCESS = 0,
		/** Generic ResourceAccounter errro */
		RA_FAILED,
		/** Argument "path" missing */
		RA_ERR_MISS_PATH,
		/** Unable to allocate a new resource descriptor */
		RA_ERR_MEM,
		/** Unable to find the state view specified */
		RA_ERR_MISS_VIEW,
		/** Unauthorized system state view manipulation attempt */
		RA_ERR_UNAUTH_VIEW,
		/** Application reference missing */
		RA_ERR_MISS_APP,
		/** Resource usages map missing	 */
		RA_ERR_MISS_USAGES,
		/** Next AWM is missing */
		RA_ERR_MISS_AWM,
		/** Application uses yet another resource set */
		RA_ERR_APP_USAGES,
		/** Resource usage required exceeds the availabilities */
		RA_ERR_USAGE_EXC,

		// --- Update mode ---

		/** Resource has not been registered at boot */
		RA_ERR_NOT_REGISTERED,
		/** The resource path specified is not valid */
		RA_ERR_INVALID_PATH,
		/** Amount exceeding registered total amount */
		RA_ERR_OVERFLOW,

		// --- Synchronization mode ---

		/** Initialization failed */
		RA_ERR_SYNC_INIT,
		/** Error occured in using/getting the resource view  */
		RA_ERR_SYNC_VIEW,
		/** Synchronization session has not been started */
		RA_ERR_SYNC_START
	};

	/**
	 * @enum PathClass
	 * @brief The class of resource path specified in the query functions
	 */
	enum PathClass_t {
		UNDEFINED = 0,
		/** Exact resource path matching (type+ID). <br>
		 *  Example: sys1.cpu2.pe0 */
		EXACT,
		/** Type matching if no ID provided, otherwise type+ID. <br>
		 *  Example: sys1.cpu.pe0  */
		MIXED,
		/** Only type matching. <br>
		 *  Example: sys.cpu.pe    */
		TEMPLATE
	};

	/**
	 * @brief Total amount of resources
	 *
	 * This is used when the only available information is the resource path
	 * (wheter template or specific).
	 *
	 * @param path Resource path
	 *
	 * @return The total amount of resource
	 */
	virtual uint64_t Total(std::string const & path) = 0;

	virtual uint64_t Total(
	        br::ResourcePathPtr_t ppath, PathClass_t rpc = EXACT) const = 0;

	/**
	 * @brief Total amount of not reserved resources
	 *
	 * This is used when the only available information is the resource path
	 * (wheter template or specific). Just the amout of resources not
	 * reserved is returned.
	 *
	 * @param path Resource path
	 *
	 * @return The total amount of not reserved resource
	 */
	virtual uint64_t Unreserved(std::string const & path) = 0;

	/**
	 * @brief Total amount of resource
	 *
	 * This is a slighty more efficient version of method Total(), to invoke
	 * whenever we have a list of Resource descriptors yet. This usually
	 * happens when the set of resources required by an AWM has been bound by
	 * the scheduling policy. Accordingly, a map of Usage objects
	 * should be retrievable (by calling AWM->GetSchedResourceBinding() and
	 * similars). Each bound Usage provides the list of bound
	 * resources through GetBindingList().
	 *
	 * @param rsrc_list A list of shared pointer to Resource descriptors (of
	 * the same type).
	 *
	 * @return The total amount of resource
	 */
	virtual uint64_t Total(br::ResourcePtrList_t & rsrc_list) const = 0;

	/**
	 * @brief Total amount of not reserved resource
	 *
	 * This is a slighty more efficient version of method Unreserved(), to invoke
	 * whenever we have a list of Resource descriptors yet.
	 *
	 * @param rsrc_list A list of shared pointer to Resource descriptors (of
	 * the same type).
	 *
	 * @return The total amount of not reserved resource
	 */
	virtual uint64_t Unreserved(br::ResourcePtrList_t & rsrc_list) const = 0;

	/**
	 * @brief Amount of resource available
	 *
	 * This is used when the only available information is the resource path
	 * (wheter template or specific).

	 * @param path Resource path
	 * @param status_view The token referencing the resource state view
	 * @param papp The application interested in the query. This means that if
	 * the application pointed by 'papp' is using yet the resource, such
	 * amount is added to the real available quantity.
	 *
	 * @return The amount of resource available
	 */
	virtual uint64_t Available(
	        std::string const & path,
	        br::RViewToken_t status_view = 0,
	        ba::SchedPtr_t papp = ba::SchedPtr_t()) = 0;

	virtual uint64_t Available(br::ResourcePathPtr_t ppath,
	                           PathClass_t rpc = EXACT,
	                           br::RViewToken_t status_view = 0,
	                           ba::SchedPtr_t papp = ba::SchedPtr_t()) const = 0;

	/**
	 * @brief Amount of resources available
	 *
	 * This is a slighty more efficient version of method Available(), to
	 * invoke whenever we have a list of Resource descriptors yet. This
	 * usually happens when the set of resources required by an AWM has been
	 * bound by the scheduling policy. Accordingly, a map of Usage
	 * objects should be retrievable (by calling
	 * AWM->GetSchedResourceBinding() and similars). Each bound Usage
	 * provides the list of bound resources through GetBindingList().
	 *
	 * @param rsrc_list A list of shared pointer to Resource descriptors (of
	 * the same type).
	 * @param status_view The token referencing the resource state view
	 * @param papp The application interested in the query. This means that if
	 * the application pointed by 'papp' is using yet the resource, such
	 * amount is added to the real available quantity.
	 *
	 * @return The amount of resource available
	 */
	virtual uint64_t Available(
	        br::ResourcePtrList_t & rsrc_list,
	        br::RViewToken_t status_view = 0,
	        ba::SchedPtr_t papp = ba::SchedPtr_t()) const = 0;

	/**
	 * @brief Amount of resources used
	 *
	 * This is used when the only available information is the resource path
	 * (wheter template or specific).
	 *
	 * @param path Resource path
	 * @param status_view The token referencing the resource state view
	 *
	 * @return The used amount of resource
	 */
	virtual uint64_t Used(
	        std::string const & path, br::RViewToken_t status_view = 0) = 0;

	virtual uint64_t Used(br::ResourcePathPtr_t ppath,
	                      PathClass_t rpc = EXACT,
	                      br::RViewToken_t status_view = 0) const = 0;

	/**
	 * @brief Amount of resources used
	 *
	 * This is a slighty more efficient version of method Used(), to invoke
	 * whenever we have a list of Resource descriptors yet. This usually
	 * happens when the set of resources required by an AWM has been bound by
	 * the scheduling policy. Accordingly, a map of Usage objects
	 * should be retrievable (by calling AWM->GetSchedResourceBinding() and
	 * similars). Each bound Usage provides the list of bound
	 * resources through GetBindingList().
	 *
	 * @param rsrc_list A list of shared pointer to Resource descriptors (of
	 * the same type).
	 * @param status_view The token referencing the resource state view
	 *
	 * @return The used amount of resource
	 */
	virtual uint64_t Used(
	        br::ResourcePtrList_t & rsrc_list,
	        br::RViewToken_t status_view = 0) const = 0;

	/**
	 * @brief Amount of resource used by a given application
	 * @param path Resource path
	 * @param papp Target application
	 * @param status_view The token referencing the resource state view
	 * @return the amount of resource
	 */
	virtual uint64_t UsedBy(
	        std::string const & path,
	        ba::SchedPtr_t papp,
	        br::RViewToken_t status_view = 0) = 0;

	/**
	 * @brief Amount of resource used by a given application
	 * @param resources_list A list of shared pointer to Resource descriptors
	 * @param papp Target application
	 * @param status_view The token referencing the resource state view
	 * @return the amount of resource
	 */
	virtual uint64_t UsedBy(
	        br::ResourcePtrList_t & resources_list,
	        ba::SchedPtr_t papp,
	        br::RViewToken_t status_view = 0) const = 0;

	virtual uint64_t UsedBy(
	        br::ResourcePathPtr_t resource_path_ptr,
	        ba::SchedPtr_t papp,
	        PathClass_t rpc = EXACT,
	        br::RViewToken_t status_view = 0) const = 0;

	/**
	 * @brief Get the resource path object related to a string
	 *
	 * Given a resource path in char string format, the member function
	 * returns the related ResourcePath object. The string must be of "EXACT"
	 * type, meaning that it must reference a single resource
	 *
	 * @param path_str Resource path in char string format
	 *
	 * @return A shared pointer to a ResourcePath object
	 */
	virtual br::ResourcePathPtr_t const GetPath(std::string const & path_str) = 0;



	/**
	 * @brief Count of resources referenced by the given path
	 *
	 * Given a resource path (also template) return the number of resources,
	 * i.e. descriptors referenced by the given path. Usually, if the path is
	 * a template, the member function should return a value greater than 1.
	 * Conversely, a specific path should return a counting value equal to 1.
	 *
	 * @param path Resource path
	 *
	 * @return The number of referenced resource descriptors
	 */
	virtual uint16_t Count(br::ResourcePathPtr_t path) const = 0;

	/**
	 * @brief The number of resources of a given type
	 *
	 * @param type A string identifying the type of resource
	 *
	 * @return How many resources of a type have been registered
	 */
	virtual uint16_t CountPerType(br::ResourceType type) const = 0;

	/**
	 * @brief The number of resource types
	 *
	 * @return How many type (or class) of resource have been registered
	 */
	virtual uint16_t CountTypes() const = 0;

	/**
	 * @brief The list of (only) managed resource types
	 *
	 * @return A list filled of resource types
	 */
	virtual std::map<br::ResourceType, std::set<BBQUE_RID_TYPE>> const & GetTypes() const = 0;

	/**
	 * @brief Get a resource descriptor
	 *
	 * If the path provided is of type "template" or "mixed" the result is a
	 * list of resource descriptors. In such a case, the function will return
	 * the topmost
	 *
	 * @param path Resource path
	 * @return A shared pointer to the resource descriptor
	 */
	virtual br::ResourcePtr_t GetResource(std::string const & path) = 0;

	virtual br::ResourcePtr_t GetResource(br::ResourcePathPtr_t ppath) const = 0;

	/**
	 * @brief Get a list of resource descriptors
	 *
	 * Given a "template path" the method return all the resource descriptors
	 * matching such template.
	 * For instance "clusters.cluster.mem" will return all the
	 * descriptors having path "clusters.cluster<N>.mem<M>".
	 *
	 * @param temp_path Template path to match
	 * @return The list of resource descriptors matching the template path
	 */
	virtual br::ResourcePtrList_t GetResources(std::string const & temp_path) = 0;

	virtual br::ResourcePtrList_t GetResources(br::ResourcePathPtr_t ppath) const = 0;

	/**
	 * @brief Check the existence of a resource
	 * @param path Resource path
	 * @return True if the resource exists, false otherwise.
	 */
	virtual bool ExistResource(std::string const & path) = 0;

	virtual bool ExistResource(br::ResourcePathPtr_t ppath) const = 0;

	/**
	 * @brief Show the system resources status
	 *
	 * This is an utility function for debug purpose that print out all the
	 * resources path and values about usage and total amount.
	 *
	 * @param status_view Token of the resources state view
	 * @param verbose print in INFO log level is true, while false in DEBUG
	 */
	virtual void PrintStatusReport(br::RViewToken_t status_view, bool verbose) const = 0;
};

}   // namespace bbque

#endif  // BBQUE_RESOURCE_ACCOUNT_STATUS_H_
