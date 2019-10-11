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

#ifndef BBQUE_RESOURCE_TREE_H_
#define BBQUE_RESOURCE_TREE_H_

#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "bbque/utils/logging/logger.h"
#include "bbque/res/resources.h"
#include "bbque/res/resource_utils.h"

#define RESOURCE_TREE_NAMESPACE "bq.rt"

/*** Matching flags used for search functions */

/**
 * Stop search after the first matching
 * Whether this flag is unset, the search will not stop until all the matching
 * paths have been retrieved from the tree
 */

//                 ALL       ...000
#define RT_MATCH_FIRST  1 // ...001

/**
 * Ask for a per resource type maatching will be performed
 * Whether this flag is unset, an "exact matching" (type + ID) will be
 * performed
 */

//               EXACT       ...00x
#define RT_MATCH_TYPE   2 // ...01x
#define RT_MATCH_MIXED  4 // ...10x


namespace bu = bbque::utils;

namespace bbque
{
namespace res
{

// Forward declaration
class ResourcePath;

/**
 * @class ResourceTree
 *
 * @brief A tree-based representation of resources
 * The class allow the management of the resource descriptors in a hierachical
 * way. The hierarchy is structured as a tree. The access to the content is
 * based on a namespace-like approach (i.e. sys.cpu0.pe2 ...)
 */
class ResourceTree
{

public:

	// Forward declarations
	struct ResourceNode;
	typedef std::shared_ptr<ResourceNode> ResourceNodePtr_t;
	typedef std::list<ResourceNodePtr_t>  ResourceNodesList_t;

	/**
	 * @class ResourceNode
	 *
	 * The base node of the ResourceTree containing a reference to a Resource
	 * descriptor.
	 */
	class ResourceNode
	{
	public:
		ResourceNode(ResourcePtr_t r):
			data(r) {};

		ResourceNode(
		        ResourcePtr_t r, ResourceNodePtr_t pnode, uint16_t d):
			data(r), parent(pnode), depth(d) {};

		virtual ~ResourceNode() {
			children.clear();
		}

		/** Data node (resource descriptor pointer) */
		ResourcePtr_t data = nullptr;
		/** Parent node */
		ResourceNodePtr_t parent = nullptr;
		/** Depth in the tree */
		uint16_t depth = 0;
		/** Children nodes */
		ResourceNodesList_t children;
	};


	/**
	 * @brief Constructor
	 */
	ResourceTree();

	/**
	 * @brief Destructor
	 */
	~ResourceTree() {
		clear_node(root);
	}

	/**
	 * @brief Insert a new resource
	 *
	 * @param rsrc_path Path object to of the resource to insert
	 *
	 * @return A shared pointer to the resource descriptor just created
	 */
	ResourcePtr_t & insert(ResourcePath const & rsrc_path);

	/**
	 * @brief Find a set of resources
	 *
	 * According to the resource path, the function member can return
	 * different results. Basically, there are three cases of resource path,
	 * related to the resource IDs provided:
	 *
	 * <ul>
	 *   <li> <b>All IDs</b>: Return a single specific resource descriptor.
	 *          This is the same behavior of function member @ref find().<br>
	 *          Example: <tt>sys0.cpu2.pe3</tt>
	 *   </li>
	 *   <li> <b>Some IDs</b>: Return all the descriptors matching path levels
	 *          per type+ID (when provided) or only type (if missing, i.e.
	 *          R_ID_NONE or R_ID_ANY) ("mixed path").<br>
	 *          Example: <tt>sys0.cpu.pe2</tt>
	 *   </li>
	 *   <li> <b>No IDs</b>:  Return all the descriptors matching path levels
	 *          per type ("template path"). <br>
	 *          Example: <tt>sys.cpu.pe</tt>
	 *   </li>
	 * </ul>
	 *
	 * The wanted behavior can be obtained by setting the matching flags:
	 *
	 * <tt>
	 *               match_flags
	 *           ------------------
	 *           |   unused | 0 0 0
	 *                        \ / ^
	 *                         |  |
	 *   00x: EXACT -----------`  |
	 *   01x: TYPE                |
	 *   10x: MIXED               |
	 *   xx0: ALL   --------------`
	 *   xx1: FIRST
	 * </tt>
	 *
	 * @param rsrc_path   A resource path object to match
	 * @param match_flags The matching flags
	 *
	 * @return A list of resource descriptors (pointers)
	 */
	ResourcePtrList_t find_list(
	        ResourcePath & rsrc_path,
	        uint16_t match_flags = 0) const;

	/**
	 * @brief Maximum depth of the tree
	 * @return The maxim depth value
	 */
	inline uint16_t depth() {
		return max_depth;
	}

	/**
	 * @brief Print the tree content
	 */
	inline void printTree() {
		print_children(root, 0);
		logger->Debug("Max depth: %d", max_depth);
	}

	/**
	 * @brief Clear the tree
	 */
	inline void clear() {
		clear_node(root);
	}

private:

	/** The logger used by the resource accounter */
	std::unique_ptr<bu::Logger> logger;

	/** Pointer to the root of the tree*/
	ResourceNodePtr_t root;

	/** Maximum depth of the tree */
	uint16_t max_depth;

	/** Counter of resources */
	uint16_t count;

	/**
	 * @brief Find a node
	 *
	 * The function is called internally by the public member functions
	 *
	 * @param curr_node The root node from which start
	 * @param rp_it Starting ResourcePath iterator
	 * @param rp_end Ending ResourcePath iterator
	 * @param match_flags The type of search to perform
	 * @param matchings A list to fill with the descriptors matching the path
	 *
	 * @return True if the search have found some matchings.
	 */
	bool find_node(ResourceNodePtr_t curr_node,
	               std::vector<ResourceIdentifierPtr_t>::iterator & rp_it,
	               std::vector<ResourceIdentifierPtr_t>::iterator const & rp_end,
	               uint16_t match_flags,
	               ResourcePtrList_t & matchings) const;

	/**
	 * @brief Append a child to the current node
	 *
	 * @param curr_node Current resource node
	 * @param pres The new resource descriptor to append
	 *
	 * @return The child node just created
	 */
	ResourceNodePtr_t add_node(ResourceNodePtr_t curr_node, ResourcePtr_t pres);

	/**
	 * @brief Recursive method for printing nodes content in a tree-like form
	 *
	 * @param node Pointer to the starting tree node
	 * @param depth Node depth
	 */
	void print_children(ResourceNodePtr_t node, int depth) const;

	/**
	 * @brief Clear a node of the tree
	 *
	 * @param node Pointer to the node to clear
	 */
	void clear_node(ResourceNodePtr_t node);

};

}   // namespace res

}   // namespace bbque

#endif // BBQUE_RESOURCE_TREE_H_
