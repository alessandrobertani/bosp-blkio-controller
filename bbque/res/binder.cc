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

#include <utility>

#include "bbque/resource_accounter.h"
#include "bbque/res/binder.h"
#include "bbque/res/bitset.h"
#include "bbque/res/resource_path.h"

#define MODULE_NAMESPACE  "bq.rbind"

namespace bu = bbque::utils;

namespace bbque {
namespace res {

void ResourceBinder::Bind(
			  ResourceAssignmentMap_t const & source_map,
			  br::ResourceType r_type,
			  BBQUE_RID_TYPE source_id,
			  BBQUE_RID_TYPE out_id,
			  ResourceAssignmentMapPtr_t out_map,
			  br::ResourceType filter_rtype,
			  ResourceBitset * filter_mask)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	ResourcePath::ExitCode_t rp_result;
	std::unique_ptr<bu::Logger> logger = bu::Logger::GetLogger(MODULE_NAMESPACE);

	// Proceed with the resource binding...
	for (auto & ru_entry : source_map) {
		br::ResourcePathPtr_t const & source_path(ru_entry.first);
		br::ResourceAssignmentPtr_t const & source_assign(ru_entry.second);

		if (!source_path->IncludesType(r_type)) {
			logger->Debug("Bind: <%s> does not contain resource <%s>",
				source_path->ToString().c_str(),
				br::GetResourceTypeString(r_type));
			continue;
		}

		// Build a new resource path
		auto out_path =
			std::make_shared<ResourcePath>(source_path->ToString());
		if (out_path->NumLevels() == 0) {
			logger->Debug("Bind: empty out path...");
			return;
		}

		// Replace ID of the given resource type with the bound ID
		rp_result = out_path->ReplaceID(r_type, source_id, out_id);
		if (rp_result == ResourcePath::OK) {
			logger->Debug("Bind: source=<%s> -> out=<%s> replacement done",
				source_path->ToString().c_str(),
				out_path->ToString().c_str());
		}
		else
			logger->Debug("Bind: source=<%s> ...nothing to do",
				source_path->ToString().c_str());

		// Create a new ResourceAssignment object and set the binding list
		auto out_assign =
			std::make_shared<ResourceAssignment>(
			source_assign->GetAmount(),
			source_assign->GetPolicy());
		logger->Debug("Bind: out=<%s> amount=%ld",
			out_path->ToString().c_str(), out_assign->GetAmount());

		// Retrieve the list of resources to which the request can be bound
		ResourcePtrList_t r_bindings = ra.GetResources(out_path);
		logger->Debug("Bind: out=<%s> binding list length=%d",
			out_path->ToString().c_str(), r_bindings.size());

		// Binding with filter
		if ((filter_rtype != br::ResourceType::UNDEFINED)
		&& (filter_mask != nullptr)) {
			out_assign->SetResourcesList(r_bindings, filter_rtype, *filter_mask);
			logger->Debug("Bind: <%s> applied filter <%s>={%s} to populate resources list",
				out_path->ToString().c_str(),
				GetResourceTypeString(filter_rtype),
				filter_mask->ToString().c_str());
		}
		else {
			out_assign->SetResourcesList(r_bindings);
		}
		logger->Debug("Bind: <%s> set resources list [length=%d]",
			out_path->ToString().c_str(),
			out_assign->GetResourcesList().size());

		// Insert the resource usage object in the output map and exit
		// from the loop
		logger->Debug("Bind: <%s> adding the resource assignment...",
			out_path->ToString().c_str());

		(*out_map)[out_path] = out_assign;
		logger->Debug("Bind: <%s> added assignment [amount=%ld nr_resources=%d]",
			out_path->ToString().c_str(),
			(*out_map)[out_path]->GetAmount(),
			(*out_map)[out_path]->GetResourcesList().size());
	}

	for (auto & m : *out_map) {
		logger->Debug("Bind: map containing <%s>: assignment=@%p",
			m.first->ToString().c_str(), m.second.get());
	}
}

void ResourceBinder::Bind(
			  br::ResourceAssignmentMap_t const & source_map,
			  br::ResourcePathPtr_t resource_path,
			  br::ResourceBitset const & filter_mask,
			  br::ResourceAssignmentMapPtr_t out_map)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	std::unique_ptr<bu::Logger> logger = bu::Logger::GetLogger(MODULE_NAMESPACE);

	auto assign_it = source_map.find(resource_path);
	if (assign_it == source_map.end()) {
		logger->Warn("Bind: <%s> missing", resource_path->ToString().c_str());
		return;
	}

	auto source_assign = assign_it->second;
	auto out_assign =
		std::make_shared<ResourceAssignment>(source_assign->GetAmount());

	// Retrive the list of resources to which the request can be bound
	auto r_list = ra.GetResources(resource_path);
	logger->Debug("Bind: <%s> has %d candidates",
		resource_path->ToString().c_str(), r_list.size());

	// Filter out resource descriptors from the list
	out_assign->SetResourcesList(r_list, filter_mask);
	logger->Debug("Bind: <%s> binding list size = %d",
		resource_path->ToString().c_str(),
		out_assign->GetResourcesList().size());

	// Insert the resource usage object in the output map
	(*out_map)[resource_path] = out_assign;
	logger->Debug("Bind: added bound path: <%s> [amount=%ld, length=%d]",
		resource_path->ToString().c_str(),
		(*out_map)[resource_path]->GetAmount(),
		(*out_map)[resource_path]->GetResourcesList().size());
}

void ResourceBinder::RemoveCompatibleAssignments(
						 br::ResourceAssignmentMapPtr_t out_map,
						 br::ResourcePathPtr_t out_path)
{
	std::unique_ptr<bu::Logger> logger = bu::Logger::GetLogger(MODULE_NAMESPACE);

	br::ResourcePathPtr_t replace_path = nullptr;
	for (auto & m : *out_map) {
		if (m.first->Compare(*out_path) == br::ResourcePath::EQUAL_TYPES) {
			replace_path = m.first;
		}
	}

	if (replace_path) {
		logger->Debug("Bind: removing compatible path: %s ",
			replace_path->ToString().c_str());
		out_map->erase(replace_path);
	}
}

inline void SetBit(
		   ResourcePathPtr_t ppath,
		   br::ResourceType r_type,
		   ResourceBitset & r_mask)
{

	// Get the ID of the resource type in the path
	BBQUE_RID_TYPE r_id = ppath->GetID(r_type);
	if ((r_id == R_ID_NONE) || (r_id == R_ID_ANY))
		return;

	// Set the ID-th bit in the mask
	r_mask.Set(r_id);
}

ResourceBitset ResourceBinder::GetMask(
				       ResourceAssignmentMapPtr_t assign_map,
				       br::ResourceType r_type)
{
	return GetMask(*(assign_map.get()), r_type);
}

ResourceBitset ResourceBinder::GetMask(
				       br::ResourceAssignmentMap_t const & assign_map,
				       br::ResourceType r_type)
{
	ResourceBitset r_mask;

	// Scan the resource assignments map
	for (auto & ru_entry : assign_map) {
		br::ResourcePathPtr_t const & ppath(ru_entry.first);
		br::ResourceAssignmentPtr_t const & r_assign(ru_entry.second);
		SetBit(ppath, r_type, r_mask);
		for (br::ResourcePtr_t const & rsrc : r_assign->GetResourcesList())
			SetBit(rsrc->Path(), r_type, r_mask);
	}
	return r_mask;
}

ResourceBitset ResourceBinder::GetMask(
				       ResourceAssignmentMapPtr_t assign_map,
				       br::ResourceType r_type,
				       br::ResourceType r_scope_type,
				       BBQUE_RID_TYPE r_scope_id,
				       SchedPtr_t papp,
				       RViewToken_t status_view)
{
	ResourceBitset r_mask;
	br::ResourceType found_rsrc_type, found_scope_type;
	BBQUE_RID_TYPE found_scope_id;
	std::unique_ptr<bu::Logger> logger = bu::Logger::GetLogger(MODULE_NAMESPACE);

	logger->Debug("GetMask: scope=<%s%d> resource=<%s> view=%d",
		br::GetResourceTypeString(r_scope_type), r_scope_id,
		br::GetResourceTypeString(r_type), status_view);

	// Scan the resource assignments map
	for (auto const & ru_entry : * (assign_map.get())) {
		br::ResourcePathPtr_t const & ppath(ru_entry.first);
		br::ResourceAssignmentPtr_t const & r_assign(ru_entry.second);

		// From the resource path extract the "scope" type (parent type), its
		// id number, and the resource type
		found_scope_id   = ppath->GetID(r_scope_type);
		found_rsrc_type  = ppath->Type();
		found_scope_type = ppath->ParentType(found_rsrc_type);

		// Skip in case scope or resource type do not match with the required
		// values
		logger->Debug("GetMask: path=<%s>", ppath->ToString().c_str());
		if ((found_scope_type != r_scope_type) || (found_rsrc_type != r_type)) {
			logger->Debug("GetMask: skipping scope=<%s> resource=<%s>",
				br::GetResourceTypeString(found_scope_type),
				br::GetResourceTypeString(r_type));
			continue;
		}
		logger->Debug("GetMask: search ID={%2d} found={%2d}",
			r_scope_id, found_scope_id);

		// Check the scope id number. Go deeper if it matches, or if the id
		// found is "ANY" or "NONE" => We need to inspect the list of resource
		// descriptors
		if ((r_scope_id < 0) || (found_scope_id < 0)
		|| (found_scope_id == r_scope_id)) {
			logger->Debug("GetMask: scope <%s> found in resource <%s>!",
				br::GetResourceTypeString(r_scope_type),
				ppath->ToString().c_str());
			r_mask |= GetMask(r_assign->GetResourcesList(),
					r_type,
					r_scope_type,
					r_scope_id,
					papp,
					status_view);
		}
	}
	logger->Debug("GetMask: type <%s> in scope <%s> = {%s}",
		br::GetResourceTypeString(r_type),
		br::GetResourceTypeString(r_scope_type),
		r_mask.ToString().c_str());
	return r_mask;
}

ResourceBitset ResourceBinder::GetMask(
				       ResourcePtrList_t const & rpl,
				       br::ResourceType r_type,
				       br::ResourceType r_scope_type,
				       BBQUE_RID_TYPE r_scope_id,
				       SchedPtr_t papp,
				       RViewToken_t status_view)
{
	ResourceBitset r_mask;
	std::unique_ptr<bu::Logger> logger = bu::Logger::GetLogger(MODULE_NAMESPACE);

	// Sanity check
	if (papp == nullptr) return r_mask;

	// Scan the resources list
	for (ResourcePtr_t const & rsrc : rpl) {
		if (rsrc->Type() != r_type) {
			logger->Warn("GetMask: Skipping resource type <%s>",
				br::GetResourceTypeString(rsrc->Type()));
			continue;
		}

		// Is the application using it?
		if (rsrc->UsedBy(papp, status_view) == 0) {
			logger->Debug("GetMask: <%s> not used by [%s]. Skipping...",
				rsrc->Path()->ToString().c_str(),
				papp->StrId());
			continue;
		}
		else
			logger->Debug("GetMask: <%s> used by [%s]. Continuing...",
				rsrc->Path()->ToString().c_str(),
				papp->StrId());

		// Scope resource (type and identifier)
		br::ResourcePathPtr_t r_path(rsrc->Path());
		if (r_path->ParentType(r_type) == r_scope_type
		&& (r_path->GetID(r_scope_type) == r_scope_id
		|| r_scope_id == R_ID_ANY)) {
			logger->Debug("GetMask: ready to set <%s>",
				br::GetResourceTypeString(r_type));
			SetBit(r_path, r_type, r_mask);
		}
	}
	logger->Debug("GetMask: type <%s> = {%s}",
		br::GetResourceTypeString(r_type),
		r_mask.ToString().c_str());
	return r_mask;
}

ResourceBitset ResourceBinder::GetMaskInRange(
					      ResourcePtrList_t const & resources_list,
					      size_t begin_pos,
					      size_t end_pos)
{
	ResourceBitset r_mask;
	size_t _count = 0;
	std::unique_ptr<bu::Logger> logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	for (ResourcePtr_t const & rsrc : resources_list) {
		if (_count >= begin_pos && _count <= end_pos) {
			r_mask.Set(rsrc->ID());
			logger->Debug("GetMaskInRange: current = %s",
				r_mask.ToString().c_str());
		}
		if (_count == end_pos)
			break;
		++_count;
	}
	return r_mask;
}

ResourceBitset ResourceBinder::GetMaskInRange(
					      ResourcePtrList_t const & resources_list,
					      ResourcePtrList_t::const_iterator iter,
					      size_t _count)
{
	ResourceBitset r_mask;
	std::unique_ptr<bu::Logger> logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	// Rewind
	if (iter == resources_list.end())
		iter = resources_list.begin();
	for (; iter != resources_list.end(); ++iter) {
		if (_count > 0) {
			r_mask.Set((*iter)->ID());
			logger->Debug("GetMaskInRange: current = %s",
				r_mask.ToString().c_str());
			--_count;
		}
		if (_count == 0)
			break;
	}
	return r_mask;
}

ResourceBinder::ExitCode_t ResourceBinder::Compatible(
						      ResourceAssignmentMapPtr_t source_map,
						      ResourceAssignmentMapPtr_t out_map)
{
	ResourceAssignmentMap_t::iterator src_it, src_end;
	ResourceAssignmentMap_t::iterator dst_it, dst_end;

	// Different size of the maps -> not compatible
	if (source_map->size() != out_map->size())
		return NOT_COMPATIBLE;

	// Compare each resource path
	for (src_it = source_map->begin(), dst_it = out_map->begin();
	src_it != src_end; ++src_it, ++dst_it) {
		ResourcePathPtr_t const & source_path(src_it->first);
		ResourcePathPtr_t const & out_path(dst_it->first);
		// The path should be equal or have equal types (template)
		if (source_path->Compare(*(out_path.get())) == ResourcePath::NOT_EQUAL) {
			return NOT_COMPATIBLE;
		}
	}

	return OK;
}

} // namespace res

} // namespace bbque
