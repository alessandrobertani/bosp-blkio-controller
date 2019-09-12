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

#include "bbque/res/resource_tree.h"

#include <iostream>
#include <memory>

#include "bbque/modules_factory.h"
#include "bbque/res/resources.h"
#include "bbque/res/resource_path.h"
#include "bbque/res/resource_utils.h"
#include "bbque/utils/utility.h"

namespace bu = bbque::utils;

namespace bbque { namespace res {


ResourceTree::ResourceTree():
	max_depth(0),
	count(0) {

	// Get a logger
	logger = bu::Logger::GetLogger(RESOURCE_TREE_NAMESPACE);
	assert(logger);

	// Initialize the root node
	root = std::make_shared<ResourceNode>(
	               std::make_shared<Resource>(
	                       ResourceType::UNDEFINED, 0, 0));
}

ResourcePtrList_t
ResourceTree::find_list(ResourcePath & rsrc_path, uint16_t match_flags) const {
	ResourcePtrList_t matchings;
	auto head_path(rsrc_path.Begin());
	auto const & end_path(rsrc_path.End());

	// match_flags = "11x" is a not valid configuration
	if (match_flags & RT_MATCH_TYPE & RT_MATCH_MIXED)
		match_flags = RT_MATCH_MIXED;

	find_node(root, head_path, end_path, match_flags, matchings);
	return matchings;
}

ResourcePtr_t & ResourceTree::insert(ResourcePath const & rsrc_path) {

	// Seeking on the last matching resource path level (tree node)
	ResourceNodePtr_t curr_node = root;
	for (auto path_it = rsrc_path.Begin();
			path_it != rsrc_path.End(); ++path_it) {
		br::ResourceIdentifierPtr_t const & curr_rid(*path_it);

		// No children -> add the first one
		if (curr_node->children.empty()) {
			ResourcePtr_t resource_ptr = std::make_shared<Resource>
				(curr_rid->Type(), curr_rid->ID());
			curr_node = add_node(curr_node, resource_ptr);
			curr_node->depth > max_depth ?
				max_depth = curr_node->depth: max_depth;
			continue;
		}

		// Children
		logger->Debug("insert: %s has %d children",
				curr_node->data->Name().c_str(),
				curr_node->children.size());

		// Current resource path level matches: go one level down
		bool node_exist = false;
		for (auto tree_node: curr_node->children) {
			ResourcePtr_t & resource_ptr(tree_node->data);
			if (resource_ptr->Compare(*curr_rid) != Resource::EQUAL) {
				logger->Debug("%-4s != %-4s",
						resource_ptr->Name().c_str(),
						curr_rid->Name().c_str());
				continue;
			}
			// Matching
			curr_node  = tree_node;
			node_exist = true;
			break;
		}

		// No matching: add a children node
		if (!node_exist) {
			ResourcePtr_t resource_ptr = std::make_shared<Resource>
				(curr_rid->Type(), curr_rid->ID());
			curr_node = add_node(curr_node, resource_ptr);
		}
	}

	++count;
	logger->Debug("insert: count = %d, depth: %d", count, max_depth);
	return curr_node->data;
}

bool ResourceTree::find_node(
		ResourceNodePtr_t curr_node,
		ResourcePath::Iterator & path_it,
		ResourcePath::Iterator const & path_end,
		uint16_t match_flags,
		ResourcePtrList_t & matchings) const {
	Resource::CResult_t rresult;
	bool found;

	// Sanity/end of path checks
	if ((!curr_node) || (curr_node->children.empty()) || (path_it == path_end))
		return false;

	// Look for the current resource path level
	logger->Debug("find_node: %s has %d children",
			curr_node->data->Name().c_str(), curr_node->children.size());
	for (auto & tree_node: curr_node->children) {
		auto & resource_ptr(tree_node->data);
		auto & path_node(*path_it);
		found = false;

		// Compare the resource identities (type and ID) and traverse the
		// resource tree accordingly
		rresult = resource_ptr->Compare(*path_node);
		logger->Debug("find_node: compare T:%4s to P:%4s = %d [match_flags %d]",
				resource_ptr->Name().c_str(),
				path_node->Name().c_str(),
				rresult, match_flags);

		if (rresult == Resource::EQUAL_TYPE) {
			//  Skip if mixed matching required but resource IDs do not match
			if (match_flags & RT_MATCH_MIXED) {
				if (path_node->ID() >= 0) {
					continue;
				}
			}
			// Skip if no type matching required
			else if (!(match_flags & RT_MATCH_TYPE)) {
				continue;
			}
		}

		// Go deeper in the resource tree (if the not is not a leaf)
		if (rresult != Resource::NOT_EQUAL) {
			if (path_it != path_end)
				find_node(tree_node, ++path_it, path_end, match_flags, matchings);
			found = true;
		}
		else
			continue;

		// End of the resource path, and resource identity matching?
		// Y: Add the the resource descriptor (from tree) to the matchings list
		// N: Continue recursively
		if ((path_it == path_end) && (found)) {
			matchings.push_back(resource_ptr);
			logger->Debug("find_node: added back %s [%d]",
					resource_ptr->Name().c_str(), matchings.size());
			// Back to a "valid" iterator
			--path_it;
		}

		// Stop whether only the first matching has been required
		if (found && (match_flags & RT_MATCH_FIRST))
			return true;
	}

	// Back to one level up
	--path_it;
	logger->Debug("find_node: back to one level up");
	return !matchings.empty();
}

ResourceTree::ResourceNodePtr_t
ResourceTree::add_node(ResourceNodePtr_t curr_node, ResourcePtr_t resource_ptr) {

	// Create the new resource node
	ResourceNodePtr_t new_node = std::make_shared<ResourceNode>(
		resource_ptr, curr_node, curr_node->depth+1);

	// Append it as child of the current node
	curr_node->children.push_back(new_node);
	return new_node;
}

void ResourceTree::print_children(ResourceNodePtr_t _node, int _depth) const {
	++_depth;
	for (auto & curr_node: _node->children) {
		for (int i= 0; i < _depth-1; ++i)
			logger->Debug("\t");

		logger->Debug("|-------%s", curr_node->data->Name().c_str());
		if (!curr_node->children.empty())
			print_children(curr_node, _depth);
	}
}

void ResourceTree::clear_node(ResourceNodePtr_t _node) {
	for (auto & curr_node: _node->children) {
		if (!curr_node->children.empty())
			clear_node(curr_node);
		curr_node->children.clear();
	}
}

}   // namespace res

}   // namespace bbque

