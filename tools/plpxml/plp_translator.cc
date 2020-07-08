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
 *
 * Author: Federico Reghenzani <federico1.reghenzani@mail.polimi.it>
 */
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <boost/filesystem.hpp>

#include "plp_translator.hpp"

// Internal use only:
#define HOST 0
#define MDEV 1

namespace bbque {
namespace tools {

int PLPTranslator::parse(const std::string &filename) noexcept {

	try {
		std::string filename_local;

		// Get the path of filename
		boost::filesystem::path pil_path = filename;
		pil_path.remove_filename();

		// And use it to compose the path of the local filename
		filename_local = this->explore_systems(filename);
		filename_local = pil_path.string() + "/" + filename_local;

		this->explore_localsys(filename_local);
	} catch (std::runtime_error e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

std::string PLPTranslator::explore_systems(const std::string &filename) {
	// Open the systems.xml file  and search for the
	// local node.
	std::ifstream systems_file(filename);
	if(!systems_file)
		throw std::runtime_error("System file is not accessible.");

	// Read all characters to a local buffer
	std::stringstream buffer;
	buffer << systems_file.rdbuf();
	systems_file.close();
	std::string content(buffer.str());

	systems_doc.parse<0>(&content[0]);

	rapidxml::xml_node<> * root_node;
	root_node = systems_doc.first_node("systems");

	// Search all systems to find the local one
	for (rapidxml::xml_node<> * node = root_node->first_node("include");
				node;
				node->next_sibling()) {

		if ( node->first_attribute("local") &&
		                node->first_attribute("local")->value() == std::string("true")) {
			return node->value();
		}
	}

	throw std::runtime_error("Local system not found.");
}

void PLPTranslator::explore_localsys(const std::string &filename) {
	// Open the systems.xml file  and search for the
	// local node.
	std::ifstream localsys_file(filename);
	if(!localsys_file)
		throw std::runtime_error("Local system file " +filename+ " is not accessible.");

	// Read all characters to a local buffer
	std::stringstream buffer;
	buffer << localsys_file.rdbuf();
	localsys_file.close();
	std::string content(buffer.str());

	localsys_doc.parse<0>(&content[0]);

	// Now explore it!
	rapidxml::xml_node<> * root_node;
	root_node = localsys_doc.first_node("system");

	if(!root_node)
		throw std::runtime_error("Missing <system> root node in "+filename+".");

	// For all memory
	for (rapidxml::xml_node<> * node = root_node->first_node("mem");
	                node;
	                node = node->next_sibling("mem")) {

		if ( ! node->first_attribute("id")) {
			throw std::runtime_error("Missing mandatory 'id' argument in <mem>");
		}
		if ( ! node->first_attribute("quantity")) {
			throw std::runtime_error("Missing mandatory 'quantity' argument in <mem>");
		}
		if ( ! node->first_attribute("unit")) {
			throw std::runtime_error("Missing mandatory 'unit' argument in <mem>");
		}

		long multiplier;
		if      (node->first_attribute("unit")->value() == std::string( "B"))
			multiplier = 1;
		else if (node->first_attribute("unit")->value() == std::string("KB"))
			multiplier = 1024L;
		else if (node->first_attribute("unit")->value() == std::string("MB"))
			multiplier = 1024L*1024;
		else if (node->first_attribute("unit")->value() == std::string("GB"))
			multiplier = 1024L*1024*1024;
		else if (node->first_attribute("unit")->value() == std::string("TB"))
			multiplier = 1024L*1024*1024*1024;
		else throw std::runtime_error("Invalid 'unit' argument in <mem>");

		long mem_int;
		try {
			mem_int = std::stol(node->first_attribute("quantity")->value());
		} catch (...) {
			throw std::runtime_error("Invalid 'quantity' argument in <mem>");
		}

		memories_size[node->first_attribute("id")->value()] = mem_int * multiplier;
	}

	// For all cpu groups
	for (rapidxml::xml_node<> * node = root_node->first_node("cpu");
	                node;
	                node = node->next_sibling("cpu")) {

		std::string curr_mem;

		// Get the memory group of this cpu group
		if ( node->first_attribute("mem_id")) {
			curr_mem = node->first_attribute("mem_id")->value();
		} else {
			throw std::runtime_error("Missing mandatory 'mem_id' argument in <cpu>");
		}

		for (rapidxml::xml_node<> * pe = node->first_node("pe");
		                pe;
		                pe = pe->next_sibling("pe")) {

			if ( ! pe->first_attribute("id")) {
				throw std::runtime_error("Missing mandatory 'id' argument in <pe>");
			}

			if (!pe->first_attribute("partition")) {
				throw std::runtime_error("Missing mandatory 'partition' argument in <pe>");
			}

			if (! pe->first_attribute("share")) {
				throw std::runtime_error("Missing mandatory 'share' argument in <pe>");
			}

			int quota;
			try {
				quota = std::stoi(pe->first_attribute("share")->value());
			} catch(...) {
				throw std::runtime_error("Invalid 'share' argument in <pe>");
			}

			if (pe->first_attribute("partition")->value() == std::string("host")) {
				add_pe(HOST, pe->first_attribute("id")->value(), curr_mem);
			}
			else if (pe->first_attribute("partition")->value() == std::string("mdev")) {
				add_pe(MDEV, pe->first_attribute("id")->value(), curr_mem);
				quota_sum += quota;
			} else {
				add_pe(HOST, pe->first_attribute("id")->value(), curr_mem);
				add_pe(MDEV, pe->first_attribute("id")->value(), curr_mem);
				quota_sum += quota;
			}
		}

		this->commit_mdev(curr_mem);
	}
} // explore_localsys

void PLPTranslator::add_pe(
        int type, const std::string & pe_id, const std::string & memory) {

	int pe_id_int;
	try {
		pe_id_int = std::stoi(pe_id);
	} catch(...) {
		throw std::runtime_error("Invalid id of PE.");
	}

	int mem_id_int;
	try {
		mem_id_int = std::stoi(memory);
	} catch(...) {
		throw std::runtime_error("Invalid id of MEM.");
	}

	if (type==HOST) {
		host_proc_elements[pe_id_int] = 1;
		host_memory_nodes[mem_id_int] = 1;
	} else {
		mdev_proc_elements[pe_id_int] = 1;
		mdev_memory_nodes[mem_id_int] = 1;
		mdev_node_proc_elements[pe_id_int] = 1;
		mdev_node_memory_nodes[mem_id_int] = 1;
	}
}

std::string PLPTranslator::get_output() const noexcept {

	// Now I create the string like "1-3,4-9" for cpus and memory
	std::string host_pes_str  = bitset_to_string(this->host_proc_elements);
	std::string host_mems_str = bitset_to_string(this->host_memory_nodes);
	std::string mdev_pes_str  = bitset_to_string(this->mdev_proc_elements);
	std::string mdev_mems_str = bitset_to_string(this->mdev_memory_nodes);

	return std::string("") +

	       "# BarbequeRTRM Root Container\n"
	       "group user.slice {\n"
	       "    perm {\n"
	       "        task {\n"
	       "            uid = root ;\n"
	       "            gid = root ;\n"
	       "        }\n"
	       "        admin {\n"
	       "            uid = root ;\n"
	       "            gid = root ;\n"
	       "        }\n"
	       "    }\n"
	       "\n"
	       "# This enables configuring a system so that several independent jobs can share\n"
	       "# common kernel data, such as file system pages, while isolating each job's\n"
	       "# user allocation in its own cpuset.  To do this, construct a large hardwall\n"
	       "# cpuset to hold all the jobs, and construct child cpusets for each individual\n"
	       "# job which are not hardwall cpusets.\n"
	       "    cpuset {\n"
	       "        cpuset.cpus = \"" + data.system_cpuset + "\";\n"
	       "        cpuset.mems = \"" + data.system_mems + "\";\n"
	       "        cpuset.cpu_exclusive = \"" + data.cpu_exclusive + "\";\n"
	       "        cpuset.mem_exclusive = \"" + data.mem_exclusive + "\";\n"
	       "    }\n"
	       "}\n"
	       "\n"
	       "# BarbequeRTRM Host Container\n"
	       "group user.slice/host {\n"
	       "    perm {\n"
	       "        task {\n"
	       "            uid = root ;\n"
	       "            gid = root ;\n"
	       "        }\n"
	       "        admin {\n"
	       "            uid = root ;\n"
	       "            gid = root ;\n"
	       "        }\n"
	       "    }\n"
	       "    cpuset {\n"
	       "        cpuset.cpus = \"" + host_pes_str + "\";\n"
	       "        cpuset.mems = \"" + host_mems_str + "\";\n"
	       "    }\n"
	       "}\n"
	       "\n"
	       "# BarbequeRTRM MDEV Container\n"
	       "group user.slice/res {\n"
	       "    perm {\n"
	       "        task {\n"
	       "            uid = " + data.user_id  + ";\n"
	       "            gid = " + data.group_id + ";\n"
	       "        }\n"
	       "        admin {\n"
	       "            uid = " + data.user_id  + ";\n"
	       "            gid = " + data.group_id + ";\n"
	       "        }\n"
	       "    }\n"
	       "    cpuset {\n"
	       "        cpuset.cpus = \"" + mdev_pes_str + "\";\n"
	       "        cpuset.mems = \"" + mdev_mems_str + "\";\n"
	       "    }\n"
	       "}\n" + subnodes
	       ;
} // get_output

void PLPTranslator::commit_mdev(const std::string & memory_id) {
	static int n = 0;

	std::string mdev_pes_str  = bitset_to_string(this->mdev_node_proc_elements);
	std::string mdev_mems_str = bitset_to_string(this->mdev_node_memory_nodes);

	if (this->mdev_node_proc_elements.count() == 0)
		return;

	subnodes +=  std::string("") +
	             "# BarbequeRTRM MDEV Node\n"
	             "group user.slice/res/node"+ std::to_string(++n) +" {\n"
	             "    perm {\n"
	             "        task {\n"
	             "            uid = root ;\n"
	             "            gid = root ;\n"
	             "        }\n"
	             "        admin {\n"
	             "            uid = root ;\n"
	             "            gid = root ;\n"
	             "        }\n"
	             "    }\n"
	             "    cpuset {\n"
	             "        cpuset.cpus = \"" + mdev_pes_str + "\";\n"
	             "        cpuset.mems = \"" + mdev_mems_str + "\";\n"
	             "    }\n"
	             "    cpu {\n"
	             "        cpu.cfs_period_us = \"100000\";\n"
	             "        cpu.cfs_quota_us =  \"" + std::to_string(quota_sum*1000/this->mdev_node_proc_elements.count()) + "\";\n"
	             "    }\n"
	             "    memory {\n"
	             "        memory.limit_in_bytes = \"" + std::to_string(memories_size[memory_id]) + "\";\n"
	             "    }\n"
	             "}\n"
	             ;

	this->mdev_node_proc_elements.reset();
	this->mdev_node_memory_nodes.reset();
	quota_sum=0;
}

std::string PLPTranslator::bitset_to_string(
		const std::bitset<MAX_ALLOWED_PES> & bs) noexcept {
	std::string ret_s;
	bool in_1=false;
	unsigned int start;
	for (unsigned int i=0; i <= bs.size(); i++) {
		if (i != bs.size() && bs[i]) {
			if (!in_1) {
				start=i;
				in_1 = true;
			}
		} else {
			if (in_1) {
				if (ret_s.size() > 0) {
					ret_s += ",";
				}
				if (start == i-1) {
					ret_s += std::to_string(start);
				} else {
					ret_s += std::to_string(start) + "-" + std::to_string(i-1);
				}
				in_1 = false;
			}
		}
	}

	return ret_s;
} // bitset_to_string

} // namespace tools
} // namespace bbque
