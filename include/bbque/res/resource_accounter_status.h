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

#include <memory>
#include <string>

#include "bbque/res/resources.h"

// Following macros are defined in order to give a lightweight abstraction
// upon the path template details of some typical resources. The purpose is
// simply to give a more clean way in writing code for Barbeque modules using
// ResourceAccounter calls.

/** System memory */
#define RSRC_SYS_MEM 	"mem"

/** Platform internal memory */
#define RSRC_PLAT_MEM 	"arch.mem"

/** Set of clusters */
#define RSRC_TILE   	"arch.tile"

/** Memory at Tile scope */
#define RSRC_TILE_MEM 	"arch.tile.mem"

/** Cluster of processing element */
#define RSRC_CLUSTER 	"arch.tile.cluster"

/** Memory at Cluster level */
#define RSRC_CLUST_MEM 	"arch.tile.cluster.mem"

/** Processing element of the Cluster */
#define RSRC_CLUST_PE 	"arch.tile.cluster.pe"


namespace bbque { namespace res {


/**
 * @brief Resource accounting data
 *
 * This definition provide the read-only status interface for interactions
 * between resource accounter and "periferical" components of the RTRM (i.e.
 * the RecipeLoader) for resource information querying.
 */
class ResourceAccounterStatusIF {

public:

	/**
	 * @brief Exit codes
	 */
	enum ExitCode_t {
		/** Successful return  */
		RA_SUCCESS = 0,
		/** Argument "path" missing */
		RA_ERR_MISS_PATH,
		/** Unable to allocate a new resource descriptor */
		RA_ERR_MEM,
		/** Unable to find the state view specified */
		RA_ERR_MISS_VIEW,
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

		// --- Synchronization mode ---

		/** Initialization failed */
		RA_ERR_SYNC_INIT,
		/** Error occured in using/getting the resource view  */
		RA_ERR_SYNC_VIEW,
		/** Synchronization session has not been started */
		RA_ERR_SYNC_START
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
	virtual uint64_t Total(std::string const & path) const = 0;

	/**
	 * @brief Total amount of resource
	 *
	 * This is a slighty more efficient version of method Total(), to invoke
	 * whenever we have a list of Resource descriptors yet. This usually
	 * happens when the set of resources required by an AWM has been bound by
	 * the scheduling policy. Accordingly, a map of ResourceUsage objects
	 * should be retrievable (by calling AWM->GetSchedResourceBinding() and
	 * similars). Each bound ResourceUsage provides the list of bound
	 * resources through GetBindingList().
	 *
	 * @param rsrc_list A list of shared pointer to Resource descriptors (of
	 * the same type).
	 *
	 * @return The total amount of resource
	 */
	virtual uint64_t Total(ResourcePtrList_t & rsrc_list) const = 0;

	/**
	 * @brief Amount of resource available
	 *
	 * This is used when the only available information is the resource path
	 * (wheter template or specific).

	 * @param path Resource path
	 * @param vtok The token referencing the resource state view
	 * @param papp The application interested in the query. This means that if
	 * the application pointed by 'papp' is using yet the resource, such
	 * amount is added to the real available quantity.
	 *
	 * @return The amount of resource available
	 */
	virtual uint64_t Available(std::string const & path, RViewToken_t vtok = 0,
			AppSPtr_t papp = AppSPtr_t()) const = 0;

	/**
	 * @brief Amount of resources available
	 *
	 * This is a slighty more efficient version of method Available(), to
	 * invoke whenever we have a list of Resource descriptors yet. This
	 * usually happens when the set of resources required by an AWM has been
	 * bound by the scheduling policy. Accordingly, a map of ResourceUsage
	 * objects should be retrievable (by calling
	 * AWM->GetSchedResourceBinding() and similars). Each bound ResourceUsage
	 * provides the list of bound resources through GetBindingList().
	 *
	 * @param rsrc_list A list of shared pointer to Resource descriptors (of
	 * the same type).
	 * @param vtok The token referencing the resource state view
	 * @param papp The application interested in the query. This means that if
	 * the application pointed by 'papp' is using yet the resource, such
	 * amount is added to the real available quantity.
	 *
	 * @return The amount of resource available
	 */
	virtual uint64_t Available(ResourcePtrList_t & rsrc_list,
			RViewToken_t vtok = 0, AppSPtr_t papp = AppSPtr_t()) const = 0;

	/**
	 * @brief Amount of resources used
	 *
	 * This is used when the only available information is the resource path
	 * (wheter template or specific).
	 *
	 * @param path Resource path
	 * @param vtok The token referencing the resource state view
	 *
	 * @return The used amount of resource
	 */
	virtual uint64_t Used(std::string const & path, RViewToken_t vtok = 0)
		const = 0;

	/**
	 * @brief Amount of resources used
	 *
	 * This is a slighty more efficient version of method Used(), to invoke
	 * whenever we have a list of Resource descriptors yet. This usually
	 * happens when the set of resources required by an AWM has been bound by
	 * the scheduling policy. Accordingly, a map of ResourceUsage objects
	 * should be retrievable (by calling AWM->GetSchedResourceBinding() and
	 * similars). Each bound ResourceUsage provides the list of bound
	 * resources through GetBindingList().
	 *
	 * @param rsrc_list A list of shared pointer to Resource descriptors (of
	 * the same type).
	 * @param vtok The token referencing the resource state view
	 *
	 * @return The used amount of resource
	 */
	virtual uint64_t Used(ResourcePtrList_t & rsrc_list, RViewToken_t vtok = 0)
		const = 0;

	/**
	 * @brief Get a resource descriptor
	 * @param path Resource path
	 * @return A shared pointer to the resource descriptor
	 */
	virtual ResourcePtr_t GetResource(std::string const & path) const = 0;

	/**
	 * @brief Get a list of resource descriptors
	 *
	 * Given a "template path" the method return all the resource descriptors
	 * matching such template.
	 * For instance "arch.clusters.cluster.mem" will return all the
	 * descriptors having path "arch.clusters.cluster<N>.mem<M>".
	 *
	 * @param temp_path Template path to match
	 * @return The list of resource descriptors matching the template path
	 */
	virtual ResourcePtrList_t GetResources(std::string const & temp_path)
		const = 0;

	/**
	 * @brief Check the existence of a resource
	 * @param path Resource path
	 * @return True if the resource exists, false otherwise.
	 */
	virtual bool ExistResource(std::string const & path) const = 0;

	/**
	 * @brief The number of system resources
	 * @return This returns the total number of system resources, i.e. the
	 * number of resource allocables to the applications.
	 */
	virtual uint16_t GetTotalNumOfResources() const = 0;

	/**
	 * @brief App/EXC using a Processing Element resource
	 *
	 * @param path The resource path
	 * @param vtok The token referencing the resource state view
	 * @return A shared pointer to the App/EXC descriptor using the given PE
	 */
	AppSPtr_t const AppUsingPE(std::string const & path,
			RViewToken_t vtok = 0) const;

	/**
	 * @brief Clustering factor
	 *
	 * Check if the resource is a clustered one and return the clustering
	 * factor.
	 *
	 * @param path Resource path
	 * @return The number of clusters in the platform if the resource is a
	 * clustered one, 1 if there are no clusters, 0 otherwise.
	 */
	virtual uint16_t ClusteringFactor(std::string const & path) = 0;

	/**
	 * @brief Show the system resources status
	 *
	 * This is an utility function for debug purpose that print out all the
	 * resources path and values about usage and total amount.
	 *
	 * @param vtok Token of the resources state view
	 * @param verbose print in INFO logleve is ture, in DEBUG if false
	 */
	virtual void PrintStatusReport(RViewToken_t vtok = 0,
			bool verbose = false) const = 0;

};

}   // namespace res

}   // namespace bbque

#endif  // BBQUE_RESOURCE_ACCOUNT_STATUS_H_
