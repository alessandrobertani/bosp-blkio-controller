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

#ifndef BBQUE_EXTRA_DATA_CONTAINER_H_
#define BBQUE_EXTRA_DATA_CONTAINER_H_

#include <map>
#include <memory>
#include <string>


namespace bbque {
namespace utils {

/**
 * @struct PluginData
 *
 * The structure must be inherited in order to implement a specific
 * attribute to manage. This provides just a string to classify the
 * attribute, and the identifying key string.
 */
struct PluginDataKey
{
	/** Plugin identification string */
	std::string plugin_name;

	/** ID key  */
	std::string key;

	PluginDataKey(std::string const & _pname, std::string const & _key) :
	    plugin_name(_pname),
	    key(_key) { }

};

using PluginDataKeyPtr_t = std::shared_ptr<PluginDataKey>;

/**
 * @brief A container of module-specific attributes
 *
 * The class provide an interface for setting and getting specific attributes.
 * We expect to use this class for extending classes as Recipe, Application
 * and WorkingMode, in order to manage information specific for each plugin in
 * a flexible way.
 *
 * Considering a scheduler module, the extension provided by this class could
 * be exploit to append information to a descriptor object.
 *
 * For
 * instance how many times it has been re-scheduled, or in the WorkingMode
 * descriptor (e.g. How much that working mode is "good" for the scheduling).
 */
class ExtraDataContainer
{
public:

	/** Data type for the structure storing the plugin-specific data */
	using PluginDataMap_t = std::map<std::string, PluginDataKeyPtr_t>;

	/** Data type for the structure storing the attributes */
	using AttributesMap_t = std::map<std::string, std::string>;


	ExtraDataContainer();

	virtual ~ExtraDataContainer();

	/******************************************************************
	 *                   Plugin-specific data                         *
	 ******************************************************************/

	/**
	 * @brief Insert/Set a plugin specific data
	 *
	 * @param pdata Shared pointer to the PluginData object (commonly a
	 * derived class)
	 */
	void SetPluginData(PluginDataKeyPtr_t pdata)
	{
		plugin_data.emplace(pdata->plugin_name, pdata);
	}

	/**
	 * @brief Get a plugin specific data
	 *
	 * @param plugin_name The namespace of the attribute
	 * @param key The ID key of the attribute
	 *
	 * @return A shared pointer to the PluginData object
	 */
	PluginDataKeyPtr_t GetPluginData(std::string const & plugin_name, std::string const & key) const;

	/**
	 * @brief Clear a plugin specific data
	 *
	 * Remove all the attributes under a given namespace or a specific one
	 * if the key is specified.
	 *
	 * @param plugin_name The attribute namespace
	 * @param key The attribute key
	 */
	void ClearPluginData(std::string const & plugin_name, std::string const & key = "");

	/******************************************************************
	 *                       Attributes                               *
	 ******************************************************************/

	/**
	 * @brief Insert/Set an attribute value (string)
	 *
	 * Set the value of a specific attribute referenced by the given key
	 *
	 * @param key The attribute key
	 * @param value A string value
	 */
	void SetAttribute(std::string const & key, std::string const & value)
	{
		attributes[key] = value;
	}

	/**
	 * @brief Get an attribute value
	 *
	 * Return the value of a specific attribute referenced by the given key
	 *
	 * @param key The attribute key
	 */
	std::string GetAttribute(std::string const & key) const
	{
		AttributesMap_t::const_iterator c_it(attributes.find(key));
		if (c_it == attributes.end())
			return "";
		return c_it->second;
	}

	/**
	 * @brief Clear an attribute
	 *
	 * Remove a specific attribute referenced by the given key
	 *
	 * @param key The attribute key
	 */
	void ClearAttribute(std::string const & key)
	{
		attributes.erase(key);
	}

protected:

	/**
	 * @brief Multi-map storing the plugin-specific data
	 *
	 * The first level key is the plugin name. The value is a
	 * PluginDataPtr_t storing the ID key for the specific data.
	 * Users should must provide a proper extension of the PluginData
	 * class in order to store the data value.
	 */
	PluginDataMap_t plugin_data;

	/**
	 * @brief Map storing the attributes
	 *
	 * Key value pairs of type string.
	 */
	AttributesMap_t attributes;
};

} // namespace utils

} // namespace bbque

#endif // BBQUE_EXTRA_DATA_CONTAINER_H_
