/*
 * Copyright (C) 2015  Politecnico di Milano
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

#ifndef BBQUE_UTILS_IOFS_H_
#define BBQUE_UTILS_IOFS_H_

#include <cstring>
#include <fstream>
#include <string>
#include <algorithm>

namespace bbque
{
namespace utils
{

class IoFs
{

public:

	enum ExitCode_t {
		OK,
		ERR_FILE_NOT_FOUND,
		ERR_ACCESS
	};

	/**
	 * @brief Read a string value from an attribute file
	 *
	 * @param path The attribute file path
	 * @param value The buffer to fill with the value
	 * @param len The size of the buffer
	 */
	static ExitCode_t ReadValueFrom(
	        std::string const & filepath, char * value, int len = 1)
	{
		try {
			memset(value, '\0', len);
			std::ifstream fd(filepath);
			if (!fd.is_open()) {
				fprintf(stderr, "File not found (%s)\n\n ", filepath.c_str());
				return ExitCode_t::ERR_FILE_NOT_FOUND;
			}
			fd.read(value, len);
			fd.close();

			*std::remove(value, value + strlen(value), '\n') = '\0';

			return ExitCode_t::OK;
		} catch(std::exception & ex) {
			fprintf(stderr, " (%s)\n\n ", ex.what());
			return ExitCode_t::ERR_ACCESS;
		}
	}

	/**
	 * @brief Read a string value from an attribute file
	 *
	 * @param path The attribute file path
	 * @param value The string object to set to the value
	 */
	static ExitCode_t ReadValueFrom(std::string const & filepath, std::string & value)
	{
		try {
			std::ifstream fd(filepath);
			if (!fd.is_open()) {
				fprintf(stderr, "File not found (%s)\n\n ", filepath.c_str());
				return ExitCode_t::ERR_FILE_NOT_FOUND;
			}


			while (!fd.eof()) {
				std::string substr;
				fd >> substr;
				value += substr;
				value += " ";

			}
			value[value.length() - 1] = '\0';
			fd.close();
			return ExitCode_t::OK;
		} catch(std::exception & ex) {
			fprintf(stderr, " (%s)\n\n ", ex.what());
			return ExitCode_t::ERR_ACCESS;
		}
	}

	static ExitCode_t ReadValueFromWithOffset(
	        std::string const & filepath,
	        std::string & value,
	        int len,
	        int offset)
	{
		ExitCode_t result;
		std::string buffer;
		result = ReadValueFrom(filepath, buffer);
		if (result != ExitCode_t::OK)
			return ExitCode_t::ERR_FILE_NOT_FOUND;

		value.assign(buffer, offset, len);
		// fprintf(stderr, "\nvalue: %s\n", value.c_str());
		return ExitCode_t::OK;
	}

	template<class T>
	static ExitCode_t ReadIntValueFrom(
	        std::string const & filepath, T & value, int scale = 1)
	{
		ExitCode_t result;
		std::string value_str;
		result = ReadValueFrom(filepath, value_str);
		if (result != ExitCode_t::OK)
			return result;
		try {
			value = std::stoi(value_str) * scale;
		} catch (const std::invalid_argument & ia) {
			value = 0;
		}
		return ExitCode_t::OK;
	}

	static ExitCode_t ReadFloatValueFrom(
	        std::string const & filepath, float & value, int scale = 1)
	{
		ExitCode_t result;
		std::string value_str;
		result = ReadValueFrom(filepath, value_str);
		if (result != ExitCode_t::OK)
			return result;
		try {
			value = std::stof(value_str) * scale;
		} catch (const std::invalid_argument & ia) {
			value = 0.0;
		}
		return ExitCode_t::OK;
	}

	template<class T>
	static ExitCode_t WriteValueTo(std::string const & filepath, T value)
	{
		std::ofstream fd(filepath);
		if (!fd) {
			return ExitCode_t::ERR_FILE_NOT_FOUND;
		}
		fd << value;
		fd.close();
		return ExitCode_t::OK;
	}

	/**
	 * @brief Read a numeric value from an attribute file, on a line matching
	 * a given pattern
	 *
	 * @param path The attribute file path
	 * @param pattern The pattern to match
	 * @param value The buffer to fill with the value
	 * @param len The size of the buffer
	 */
	static ExitCode_t ParseValue(
	        std::string const & filepath,
	        std::string const & pattern,
	        char * value,
	        int len)
	{
		std::string line;
		size_t b_pos, e_pos;
		std::ifstream fd(filepath);
		if (!fd)
			return ExitCode_t::ERR_FILE_NOT_FOUND;

		memset(value, '\0', len);
		while (!fd.eof()) {
			std::getline(fd, line);
			// Pattern matching
			if (line.find(pattern) == std::string::npos)
				continue;
			// Value substring
			b_pos = line.find_first_of("0123456789");
			e_pos = line.find_last_of("0123456789") - b_pos;
			strncpy(value, (line.substr(b_pos, e_pos + 1)).c_str(), len);
			break;
		}
		fd.close();
		return ExitCode_t::OK;
	}
};


} // namespace utils

} // namespace bbque

#endif // BBQUE_UTILS_IOFS_H_
