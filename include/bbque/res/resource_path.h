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

#ifndef BBQUE_RESOURCE_PATH_H_
#define BBQUE_RESOURCE_PATH_H_

#include <bitset>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "bbque/res/identifier.h"
#include "bbque/utils/logging/logger.h"


#define MAX_NUM_LEVELS    	10
#define MAX_LEN_RPATH_STR 	(MAX_NUM_LEVELS * MAX_R_NAME_LEN)

namespace bu = bbque::utils;

namespace bbque { namespace res {



/**
 * @class ResourcePath
 *
 * A resource path is a sort of reference object resource descriptors.
 * Instances of this class can be exploited for querying the status of a
 * specific resource, or a set of them.
 *
 * Basically, the resource path is based on a chain of ResourceIdentifier
 * objects, where each object is viewed as a "level" in a sort of namespace
 * fashion.
 *
 * Example: For resource path (string) "sys0.cpu1.pe2", a ResourcePath object
 * will create 3 ResourceIdentifier, having respectively type SYSTEM and ID 0,
 * type CPU and ID 1, type PROC_ELEMENT and ID 2.
 *
 * @see ResourceIdentifier for further details.
 */
class ResourcePath {

public:

	/** Wrapper types */
	typedef std::vector<ResourceIdentifierPtr_t>::iterator Iterator;
	typedef std::vector<ResourceIdentifierPtr_t>::const_iterator ConstIterator;

	/**
	 * @enum ExitCode_t
	 */
	enum ExitCode_t {
		OK        = 0 ,
		WRN_MISS_ID   ,
		ERR_MISS_TYPE ,
		ERR_USED_TYPE
	};

	/**
	 * @enum ExitCode_t
	 *
	 * Results of a comparison operation
	 */
	enum CResult_t {
		EQUAL   = 0 ,
		EQUAL_TYPES ,
		NOT_EQUAL
	};

	/**
	 * @enum PathClass
	 * @brief The class of resource path specified in the query functions
	 */
	enum Class_t {
		UNDEFINED = 0,
		/** Exact resource path matching (type+ID). <br>
		 *  Example: sys1.cpu2.pe0 */
		EXACT ,
		/** Type matching if no ID provided, otherwise type+ID. <br>
		 *  Example: sys1.cpu.pe0  */
		MIXED ,
		/** Only type matching. <br>
		 *  Example: sys.cpu.pe    */
		TEMPLATE
	};

	/**
	 * @brief Constructor
	 *
	 * Builds a ResourcePath object from a resource path string
	 *
	 * @param r_path The resource path string
	 */
	ResourcePath(std::string const & str_path);

	/**
	 * @brief Copy constructor
	 */
	ResourcePath(ResourcePath const & r_path);

	/**
	 * @brief Destructor
	 */
	~ResourcePath();


	/**
	 * @brief Compare two resource identifiers
	 *
	 * @param r_path The ResourcePath to compare
	 *
	 * @return The result of the comparison
	 */
	CResult_t Compare(ResourcePath const & r_path) const;

	/**
	 * @brief Append a resource type+id
	 *
	 * @param r_type_str The type in char string format
	 * @param r_id The integer ID
	 *
	 * @return OK for success, ERR_UNKN_TYPE for unknown resource type,
	 * ERR_USED_TYPE if the type has been already included in the path
	 */
	ExitCode_t Append(std::string const & str_type, BBQUE_RID_TYPE r_id);

	/**
	 * @brief Append a resource type+id
	 *
	 * @param r_type The resource type
	 * @param r_id   The integer ID
	 *
	 * @return OK for success, ERR_UNKN_TYPE for unknown resource type,
	 * ERR_USED_TYPE if the type has been already included in the path
	 */
	ExitCode_t Append(ResourceType r_type, BBQUE_RID_TYPE r_id);

	/**
	 * @brief Append a set of resource identifiers from a string path
	 *
	 * @param str_path The resource string path to append
	 * @param smart_mode If true, skip resource identifiers of already used
	 * type
	 *
	 * @return OK for success, ERR_UNKN_TYPE for unknown resource type,
	 * ERR_USED_TYPE if the type has been already included in the path and
	 * smart_mode is set to 'false'
	 */
	ExitCode_t AppendString(
			std::string const & str_path,
			bool smart_mode = false);

	/**
	 * @brief Clear a resource path and copy a new one into
	 *
	 * @param Source resource path object
	 * @param Number of levels to copy
	 *
	 * @return OK for success, otherwise and error code due to @see Append
	 * fails
	 */
	ExitCode_t Copy(ResourcePath const & rp_src, int num_levels = 0);

	/**
	 * @brief Concatenate a resource path
	 *
	 * @param Source resource path object
	 * @param Number of levels to copy
	 * @param smart_mode If true, skip resource identifiers of already used
	 * type
	 *
	 * @return OK for success, otherwise and error code due to @see Append
	 * fails
	 */
	ExitCode_t Concat(
			ResourcePath const & rp_src,
			int num_levels = 0,
			bool smart_mode = true);

	/**
	 * @brief Concatenate a resource path from a string
	 *
	 * @param Source resource path string
	 *
	 * @return OK for success, otherwise and error code due to @see Append
	 * fails
	 */
	ExitCode_t Concat(std::string const & str_path);


	/**
	 * @brief Completely reset the object
	 */
	void Clear();

	/**
	 * @brief Check if the path includes a given resource type
	 */
	bool IncludesType(ResourceType r_type);

	/**
	 * @brief Get the type of resource referenced by the path
	 *
	 * Example: path "sys.cpu.mem" will have global type equal to MEMORY
	 *
	 * @return The related @ref ResourceType value
	 **/
	ResourceType Type() const {
		return global_type;
	}

	/**
	 * @brief Get the type of resource at a given relative position of the
	 * path
	 *
	 * @param rel_pos The relative position
	 *
	 * Example: path "sys.cpu.mem", rel_pos=-1 will return CPU.
	 *
	 * @return The related @ref ResourceType value
	 **/
	ResourceType Type(ssize_t rel_pos) {
		ssize_t pos = rel_pos;
		if (pos < 0)
			pos += identifiers.size() - 1;
		return identifiers.at(pos)->Type();
	}

	/**
	 * @brief Get the type of the parent of a resource type in the path
	 *
	 * Example: The parent type of 'mem' in "sys.cpu.mem" is 'cpu'
	 *
	 * @return The related @ref ResourceType value
	 **/
	ResourceType ParentType(
		ResourceType r_type =
			ResourceType::PROC_ELEMENT) const;


	/** Iterators **/

	ResourcePath::Iterator Begin() {
		return identifiers.begin();
	}

	ResourcePath::ConstIterator Begin() const {
		return identifiers.begin();
	}

	ResourcePath::Iterator End() {
		return identifiers.end();
	}

	ResourcePath::ConstIterator End() const {
		return identifiers.end();
	}


	/**
	 * @brief Get the ID associated to a resource (type) in the path
	 */
	BBQUE_RID_TYPE GetID(ResourceType r_type) const;

	/**
	 * @brief Replace the ID associated to a resource (type) in the path
	 *
	 * @param r_type Resource type of the replacement
	 * @param source_id Source resource ID
	 * @param out_id Destination resource ID
	 *
	 * @return WRN_MISS_ID if no valid ID has been specified.
	 *	ERR_UNKN_TYPE if no valid type has been specified.
	 */
	ExitCode_t ReplaceID(
			ResourceType r_type,
			BBQUE_RID_TYPE source_id,
			BBQUE_RID_TYPE out_id);



	/**
	 * @brief Retrieve a resource identifier
	 *
	 * @param level_num The depth level
	 *
	 * @return A shared pointer to the resource identifier object
	 */
	ResourceIdentifierPtr_t GetIdentifier(uint8_t depth_level) const;

	/**
	 * @brief Retrieve a resource identifier
	 *
	 * @param r_type The resource type
	 *
	 * @return A shared pointer to the resource identifier object
	 */
	ResourceIdentifierPtr_t GetIdentifier(ResourceType r_type) const;

	/**
	 * @brief Retrieve the entire list of identifiers
	 *
	 * @return A reference to the resource identifier objects
	 */
	std::vector<ResourceIdentifierPtr_t> const & GetIdentifiers() const {
		return identifiers;
	}

	/**
	 * @brief The depth level of type in a path
	 *
	 * @param r_type The resource type to find in the path
	 *
	 * @return -1 if the type is not included in the current path,
	 * otherwise returns the positive integer value related to the
	 * depth level
	 */
	int8_t GetLevel(ResourceType r_type) const;

	/**
	 * @brief Return the number of levels of the path
	 */
	size_t NumLevels() const {
		return identifiers.size();
	}

	/**
	 * @brief Check if the resource path is of "template" class
	 *
	 * A template path does not feature any ID.
	 * Example: "sys.cpu.mem"
	 *
	 * @return true if the path is a template, false otherwise
	 */
	bool IsTemplate() const;

	/**
	 * @brief Return the resource path in text string format
	 */
	std::string ToString() const;


	/***********************************************************
	 * Operators                                               *
	 ***********************************************************/

	bool operator< (ResourcePath const & r_path) const;

	bool operator== (ResourcePath const & r_path) const;

	ResourcePath operator+ (std::string const & str_path) const {
		ResourcePath new_rp(*this);
		new_rp.Concat(str_path);
		return new_rp;
	}

	ResourcePath operator+ (ResourcePath const & r_path) const {
		ResourcePath new_rp(*this);
		new_rp.Concat(r_path);
		return new_rp;
	}

	ResourcePath & operator+= (ResourcePath const & r_path) {
		this->Concat(r_path);
		return *this;
	}

	ResourcePath & operator+= (std::string const & str_path) {
		this->Concat(str_path);
		return *this;
	}

private:

	/** Logger instance */
	std::unique_ptr<bu::Logger> logger;

	/** Resource identifiers: one for each level of the the path */
	std::vector<ResourceIdentifierPtr_t> identifiers;

	/** Keep track of the resource types in the path */
	std::bitset<R_TYPE_COUNT> types_bits;

	/** Keep track of the position of the resource type in the vector */
	std::unordered_map<uint16_t, uint8_t> types_idx;

	/** The type of resource referenced by the path. */
	ResourceType global_type;

	/** Number of levels counter */
	uint8_t level_count;
};

} // namespace bbque

} // namespace res

#endif // BBQUE_RESOURCE_PATH_H_


