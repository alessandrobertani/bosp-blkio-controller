#include "rxml_ploader.h"

#include "bbque/utils/logging/logger.h"
#include "bbque/utils/utility.h"
#include "bbque/config.h"

#include <boost/program_options.hpp>
#include <fstream>
#include <string>
#include <cstdlib>

#define CURRENT_VERSION "1.0"

namespace po = boost::program_options;


namespace bbque {
namespace plugins {

/** Set true it means the plugin has read its options in the config file*/
bool RXMLPlatformLoader::configured = false;

/** Recipes directory */
std::string RXMLPlatformLoader::platforms_dir = "";

/** Map of options (in the Barbeque config file) for the plugin */
po::variables_map xmlploader_opts_value;

RXMLPlatformLoader::~RXMLPlatformLoader() { }

RXMLPlatformLoader::RXMLPlatformLoader() : initialized(false)
{
	// Get a logger
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	logger->Debug("Built RXML PlatformLoader object @%p", (void*)this);

	// Save the hostname of the current machine for future use.
	local_hostname[1023] = '\0';
	gethostname(local_hostname, 1023);

}

RXMLPlatformLoader::ExitCode_t RXMLPlatformLoader::loadPlatformInfo() noexcept
{

	if (this->initialized) {
		logger->Warn("RXMLPlatformLoader already initialized (I will ignore"
			"the replicated loadPlatformInfo() call)");
		return PL_SUCCESS;
	}

	std::string   path(platforms_dir + "/" BBQUE_PIL_FILE);
	std::ifstream xml_file(path);

	if (BBQUE_UNLIKELY(!xml_file.good())) {
		logger->Error("systems.xml file not found, inaccessible or empty.");
		return PL_NOT_FOUND;
	}

	std::stringstream buffer;
	buffer << xml_file.rdbuf();
	xml_file.close();

	if (BBQUE_UNLIKELY(!buffer.good())) {
		logger->Error("Reading of systems.xml failed.");
		return PL_NOT_FOUND;
	}

	std::string xmlstr(buffer.str());

	try {
		doc.parse<0>(&xmlstr[0]);   // Note: c++11 guarantees that the memory is
		// sequentially allocated inside the string,
		// so no problem here.
	}
	catch (const rapidxml::parse_error &e) {
		logger->Error("XML syntax error near %.10s: %s.", e.where<char>(), e.what());
		return PL_SYNTAX_ERROR;
	}
	catch (const std::runtime_error &e) {
		logger->Error("Generic error: %s.", e.what());
		return PL_GENERIC_ERROR;
	}

	ExitCode_t error = PL_GENERIC_ERROR;
	try {
		error = ParseDocument();
	}
	catch (const rapidxml::parse_error &e) {
		logger->Error("XML syntax error near %.10s: %s.", e.where<char>(), e.what());
	}
	catch (const PlatformLoaderEXC &e) {
		logger->Error("XML not valid: %s.", e.what());
	}
	catch (const std::runtime_error &e) {
		logger->Error("Generic error: %s.", e.what());
	}
	catch (...) {
		logger->Error("Generic error.");
	}

	if (error != PL_SUCCESS) {
		// Clean the platform description in case of error
		this->pd = pp::PlatformDescription();
		return error;
	}


	this->initialized = true;
	return PL_SUCCESS;
}

const pp::PlatformDescription &RXMLPlatformLoader::getPlatformInfo() const
{
	if (!this->initialized) {
		throw PlatformLoaderEXC("getPlatformInfo() called before initialization.");
	}

	return this->pd;
}

pp::PlatformDescription &RXMLPlatformLoader::getPlatformInfo()
{
	if (!this->initialized) {
		throw PlatformLoaderEXC("getPlatformInfo() called before initialization.");
	}

	return this->pd;
}

// =======================[ Static plugin interface ]=========================

void * RXMLPlatformLoader::Create(PF_ObjectParams *params)
{
	if (!Configure(params))
		return nullptr;

	return new RXMLPlatformLoader();
}

int32_t RXMLPlatformLoader::Destroy(void *plugin)
{
	if (!plugin)
		return -1;
	delete (RXMLPlatformLoader *)plugin;
	return 0;
}

bool RXMLPlatformLoader::Configure(PF_ObjectParams * params)
{
	if (configured)
		return true;

	// Declare the supported options
	po::options_description xmlploader_opts_desc("RXML Platform Loader Options");
	xmlploader_opts_desc.add_options()
		(MODULE_CONFIG".platform_dir", po::value<std::string>
		(&platforms_dir)->default_value(BBQUE_PATH_PREFIX "/" BBQUE_PATH_PILS),
		"platform folder")
		;

	// Get configuration params
	PF_Service_ConfDataIn data_in;
	data_in.opts_desc = &xmlploader_opts_desc;
	PF_Service_ConfDataOut data_out;
	data_out.opts_value = &xmlploader_opts_value;

	PF_ServiceData sd;
	sd.id = MODULE_NAMESPACE;
	sd.request = &data_in;
	sd.response = &data_out;

	int32_t response =
		params->platform_services->InvokeService(PF_SERVICE_CONF_DATA, sd);

	if (response != PF_SERVICE_DONE)
		return false;

	if (daemonized)
		syslog(LOG_INFO, "Using RXMLPlatformLoader platform folder [%s]",
		platforms_dir.c_str());
	else
		fprintf(stdout, FI("Using RXMLPlatformLoader platform folder [%s]\n"),
			platforms_dir.c_str());

	return true;
}

// =======================[ XML releated methods ]=========================

RXMLPlatformLoader::node_ptr
RXMLPlatformLoader::GetFirstChild(node_ptr parent, const char * name, bool mandatory) const
{

	node_ptr child = parent->first_node(name, 0, true);
	if (child == nullptr) {
		if (mandatory) {
			logger->Error("Missing mandatory <%s> tag.", name);
			throw PlatformLoaderEXC("Missing mandatory tag.");
		}
		else {
			return nullptr;
		}
	}

	return child;
}

RXMLPlatformLoader::attr_ptr
RXMLPlatformLoader::GetFirstAttribute(node_ptr tag, const char * name, bool mandatory) const
{
	attr_ptr attr = tag->first_attribute(name, 0, true);
	if (attr == nullptr) {
		if (mandatory) {
			logger->Error("Missing argument '%s' in <%s> tag.", name, tag->name());
			throw PlatformLoaderEXC("Missing mandatory argument.");
		}
		else {
			return nullptr;
		}
	}

	return attr;
}

RXMLPlatformLoader::ExitCode_t RXMLPlatformLoader::ParseDocument()
{
	node_ptr root    = this->GetFirstChild(&doc, "systems", true);
	attr_ptr version = this->GetFirstAttribute(root, "version", true);

	if (strcmp(version->value(), CURRENT_VERSION) != 0) {
		logger->Error("Version mismatch: my version is " CURRENT_VERSION " but systems.xml"
			"has version %s.", version->value());
		return PL_GENERIC_ERROR;
	}

	assert(root);

	std::string is_local_str;
	bool local_found = false;
	node_ptr include_sys = this->GetFirstChild(root, "include", true) ;

	while (include_sys != NULL) {
		logger->Debug("Parsing system description from '%s'...", include_sys->value());

		// Check if it is the local system
		attr_ptr local_attr = this->GetFirstAttribute(include_sys, "local", true);
		is_local_str.assign(local_attr->value());
		bool is_local = 0 == is_local_str.compare("true");

		if (local_found && is_local) {
			// That's bad. I found two local systems. This is not possible.
			logger->Error("More than one local system found!");
			return PL_LOGIC_ERROR;
		}

		// If the local is not found yet, check if the current is local
		if (!local_found) {
			local_found = is_local;
		}

		this->ParseSystemDocument(include_sys->value(), is_local);
		include_sys = include_sys->next_sibling("include");
	}

	if (!local_found) {
		logger->Error("No local system found in systems.xml.");
		return PL_LOGIC_ERROR;
	}

	return PL_SUCCESS;
}

RXMLPlatformLoader::ExitCode_t
RXMLPlatformLoader::ParseSystemDocument(const char* name, bool is_local)
{
	logger->Info("Loading '%s' platform file...", name);

	std::string   path(platforms_dir + "/" + name);
	std::ifstream xml_file(path);

	if (BBQUE_UNLIKELY(!xml_file.good())) {
		logger->Error("'%s' file not found, inaccessible or empty.", name);
		return PL_NOT_FOUND;
	}

	std::stringstream buffer;
	buffer << xml_file.rdbuf();
	xml_file.close();

	if (BBQUE_UNLIKELY(!buffer.good())) {
		logger->Error("Reading of %s failed.", name);
		return PL_NOT_FOUND;
	}

	std::string xmlstr(buffer.str());

	rapidxml::xml_document<> inc_doc;
	// Not try{}catch{} it, leave it throwing, managed at upper levels
	inc_doc.parse<0>(&xmlstr[0]);    // Note: c++11 guarantees that the memory is
	// sequentially allocated inside the string,
	// so no problem here.

	node_ptr root     = this->GetFirstChild(&inc_doc, "system", true);

	attr_ptr id = this->GetFirstAttribute(root, "id", true);
	// Address mandatory only for remote systems
	attr_ptr hostname = this->GetFirstAttribute(root, "hostname", true);
	// The address is mandatory only if the system is remote.
	attr_ptr address  = this->GetFirstAttribute(root, "address", !is_local);
	logger->Debug("Parsing system id=%s hostname=[%s] address=<%s>", id->value(),
		hostname->value(), address->value() ? address->value() : "`localhost`");

	pp::PlatformDescription::System sys;
	sys.SetId(atoi(id->value()));
	sys.SetLocal(is_local);
	sys.SetHostname(hostname->value());
	if (address) {
		sys.SetNetAddress(address->value());
		logger->Info("System %d: IP=%s",
			sys.GetId(), sys.GetNetAddress().c_str());
	}
	else {
		logger->Warn("System %d: no IPD address provided", sys.GetId());
	}
	sys_count++;

	///   <memory>
	RXMLPlatformLoader::ExitCode_t ec = PL_SUCCESS;
	ec = ParseMemories(root, sys);
	if (ec != PL_SUCCESS)
		return ec;

	///   <cpu>
	ec = ParseCPUs(root, sys);
	if (ec != PL_SUCCESS)
		return ec;

	///   <gpu>
	ec = ParseManycores(root, sys, "gpu");
	if (ec != PL_SUCCESS)
		return ec;

	///   <acc>
	ec = ParseManycores(root, sys, "acc");
	if (ec != PL_SUCCESS)
		return ec;

	/// <netif>
	ec = ParseNetworkIFs(root, sys);
	if (ec != PL_SUCCESS)
		return ec;

	/// <storage>
	ec = ParseStorages(root, sys);
	if (ec != PL_SUCCESS)
		return ec;

	pd.AddSystem(sys);
	return is_local ? PL_SUCCESS : PL_SUCCESS_NO_LOCAL;
}

RXMLPlatformLoader::ExitCode_t
RXMLPlatformLoader::ParseMemories(node_ptr root,
				  pp::PlatformDescription::System & sys)
{
	// Now get all the memories and save them. This should be perfomed before <cpu> and
	// other tags, due to reference to memories inside them.
	node_ptr memory_tag = this->GetFirstChild(root, "mem", true) ;
	while ( memory_tag != NULL ) {
		pp::PlatformDescription::Memory mem;
		attr_ptr id_attr       = this->GetFirstAttribute(memory_tag, "id",       true);
		attr_ptr quantity_attr = this->GetFirstAttribute(memory_tag, "quantity", true);
		attr_ptr unit_attr     = this->GetFirstAttribute(memory_tag, "unit",     true);

		short        id;
		int          quantity;
		int_fast16_t exp;

		// id=""
		try {
			// Yes, this conversion is unsafe. However, there will not probably
			// be over 32767 memories in one machine...
			id = (short)std::stoi(id_attr->value());
		}
		catch (const std::invalid_argument& e) {
			logger->Error("ID for <memory> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range& e) {
			logger->Error("ID for <memory> is out-of-range, please change your unit.");
			return PL_LOGIC_ERROR;
		}

		// quantity=""
		try {
			quantity = std::stoi(quantity_attr->value());
		}
		catch (const std::invalid_argument &e) {
			logger->Error("Quantity for <memory> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range &e) {
			logger->Error("Quantity for <memory> is out-of-range, please increase your unit.");
			return PL_LOGIC_ERROR;
		}

		// unit=""
		switch (ConstHashString(unit_attr->value())) {
		case ConstHashString("B"):
			exp = 0;
			break;
		case ConstHashString("KB"):
			exp = 10;
			break;
		case ConstHashString("MB"):
			exp = 20;
			break;
		case ConstHashString("GB"):
			exp = 20;
			break;
		case ConstHashString("TB"):
			exp = 40;
			break;
		default:
			logger->Error("Invalid `unit` for <memory>.");
			return PL_LOGIC_ERROR;
			break;
		}

		mem.SetPrefix(sys.GetPath());
		mem.SetId(id);
		mem.SetQuantity(((int64_t)quantity) << exp);
		sys.AddMemory(std::make_shared<pp::PlatformDescription::Memory>(mem));

		memory_tag = memory_tag->next_sibling("mem");
	}

	return PL_SUCCESS;
}

RXMLPlatformLoader::ExitCode_t
RXMLPlatformLoader::ParseNetworkIFs(node_ptr root, pp::PlatformDescription::System & sys)
{
	node_ptr netif_tag = this->GetFirstChild(root, "netif", false);

	while ( netif_tag != NULL ) {
		attr_ptr id_attr   = this->GetFirstAttribute(netif_tag, "id",   true);
		attr_ptr name_attr = this->GetFirstAttribute(netif_tag, "name", true);

		short        id;
		std::string  name(name_attr->value());

		// id=""
		try {
			// Yes, this conversion is unsafe. However, there will not probably
			// be over 32767 memories in one machine...
			id = (short)std::stoi(id_attr->value());
		}
		catch (const std::invalid_argument& e) {
			logger->Error("ID for <netif> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range& e) {
			logger->Error("ID for <netif> is out-of-range, please change your unit.");
			return PL_LOGIC_ERROR;
		}

		pp::PlatformDescription::NetworkIF netif(id, name);
		netif.SetPrefix(sys.GetPath());

		sys.AddNetworkIF(std::make_shared<pp::PlatformDescription::NetworkIF>(netif));

		netif_tag = netif_tag->next_sibling("netif");
	}

	return PL_SUCCESS;
}

RXMLPlatformLoader::ExitCode_t
RXMLPlatformLoader::ParseCPUs(node_ptr root, pp::PlatformDescription::System & sys)
{
	node_ptr cpu_tag = this->GetFirstChild(root, "cpu");
	while ( cpu_tag != NULL ) {
		pp::PlatformDescription::CPU cpu;
		attr_ptr arch_attr     = this->GetFirstAttribute(cpu_tag, "arch",     true);
		attr_ptr id_attr       = this->GetFirstAttribute(cpu_tag, "id",       true);
		attr_ptr socket_id_attr = this->GetFirstAttribute(cpu_tag, "socket_id", true);
		attr_ptr mem_id_attr   = this->GetFirstAttribute(cpu_tag, "mem_id",   true);

		const char * arch = arch_attr->value();
		short        id;
		short        socket_id;
		short        mem_id;

		/// Read id=""
		try {
			// Yes, this conversion is unsafe. However, there will not probably
			// be over 32767 cpu in one machine...
			id = (short)std::stoi(id_attr->value());
		}
		catch (const std::invalid_argument &e) {
			logger->Error("ID for <cpu> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range &e) {
			logger->Error("ID for <cpu> is out-of-range.");
			return PL_LOGIC_ERROR;
		}

		// socket_id=""
		try {
			// Yes, this conversion is unsafe. However, there will not probably
			// be over 32767 cpu in one machine...
			socket_id = (short)std::stoi(socket_id_attr->value());
		}
		catch (const std::invalid_argument &e) {
			logger->Error("socket_id for <cpu> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range &e) {
			logger->Error("socket_id for <cpu> is out-of-range.");
			return PL_LOGIC_ERROR;
		}

		// mem_id=""
		try {
			// Yes, this conversion is unsafe. However, there will not probably
			// be over 32767 memories in one machine...
			mem_id = (short)std::stoi(mem_id_attr->value());
		}
		catch (const std::invalid_argument &e) {
			logger->Error("mem_id for <cpu> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range &e) {
			logger->Error("mem_id for <cpu> is out-of-range.");
			return PL_LOGIC_ERROR;
		}

		std::shared_ptr<pp::PlatformDescription::Memory>
			curr_memory = sys.GetMemoryById(mem_id);

		if (curr_memory == nullptr) {
			logger->Error("<cpu> with memory id %d not found in memories list.", mem_id);
			return PL_LOGIC_ERROR;
		}

		cpu.SetPrefix(sys.GetPath());
		cpu.SetArchitecture(arch);
		cpu.SetId(id);
		cpu.SetSocketId(socket_id);
		cpu.SetMemory(curr_memory);

		// TODO: extract <info> tag

		/// <cpu>
		///      <pe>
		node_ptr pe_tag = this->GetFirstChild(cpu_tag, "pe");
		while (pe_tag != NULL) {
			pp::PlatformDescription::ProcessingElement pe;
			ExitCode_t ec = ParseProcessingElement(pe_tag, pe);
			if (ec != PL_SUCCESS)
				return ec;

			pe.SetPrefix(cpu.GetPath());
			cpu.AddProcessingElement(pe);
			pe_tag = pe_tag->next_sibling("pe");
		}

		sys.AddCPU(cpu);
		cpu_tag = cpu_tag->next_sibling("cpu");
	}

	return PL_SUCCESS;
}

RXMLPlatformLoader::ExitCode_t
RXMLPlatformLoader::ParseProcessingElement(node_ptr pe_tag,
					   pp::PlatformDescription::ProcessingElement & pe)
{
	attr_ptr id_attr       = this->GetFirstAttribute(pe_tag, "id",       true);
	attr_ptr core_id_attr  = this->GetFirstAttribute(pe_tag, "core_id",  true);
	attr_ptr share_attr    = this->GetFirstAttribute(pe_tag, "share",    true);
	attr_ptr partition_attr  = this->GetFirstAttribute(pe_tag, "partition",  false);

	int id;
	short core_id, share;

	// id=""
	try {
		id = (short)std::stoi(id_attr->value());
	}
	catch (const std::invalid_argument &e) {
		logger->Error("id for <pe> is not a valid integer.");
		return PL_LOGIC_ERROR;
	}
	catch (const std::out_of_range &e) {
		logger->Error("id for <pe> is out-of-range.");
		return PL_LOGIC_ERROR;
	}

	// socket_id=""
	try {
		core_id = (short)std::stoi(core_id_attr->value());
	}
	catch (const std::invalid_argument &e) {
		logger->Error("core_id for <pe> is not a valid integer.");
		return PL_LOGIC_ERROR;
	}
	catch (const std::out_of_range &e) {
		logger->Error("core_id for <pe> is out-of-range.");
		return PL_LOGIC_ERROR;
	}

	// share=""
	try {
		share = (short)std::stoi(share_attr->value());
	}
	catch (const std::invalid_argument &e) {
		logger->Error("share for <pe> is not a valid integer.");
		return PL_LOGIC_ERROR;
	}
	catch (const std::out_of_range &e) {
		logger->Error("share for <pe> is out-of-range.");
		return PL_LOGIC_ERROR;
	}

	// Check that share is in the limits
	if (share < 0 || share > 100) {
		logger->Error("share for <pe> must be between 0 and 100, but it's %i.", share);
		return PL_LOGIC_ERROR;
	}

	// partition=""
	if (partition_attr != nullptr) {
		switch (ConstHashString(partition_attr->value())) {
		case ConstHashString("shared"):
			pe.SetPartitionType(pp::PlatformDescription::SHARED);
			break;
		case ConstHashString("host"):
			pe.SetPartitionType(pp::PlatformDescription::HOST);
			break;
		case ConstHashString("mdev"):
			pe.SetPartitionType(pp::PlatformDescription::MDEV);
			;
			break;
		default:
			logger->Error("'%s' is not a valid value for `partition` attribute.",
				partition_attr->value());
			return PL_LOGIC_ERROR;
			break;
		}
	}
	else {
		// If not specified the default is device-only.
		pe.SetPartitionType(pp::PlatformDescription::MDEV);
	}

	pe.SetId(id);
	pe.SetCoreId(core_id);
	pe.SetShare(share);

	return PL_SUCCESS;
}

RXMLPlatformLoader::ExitCode_t
RXMLPlatformLoader::ParseManycores(node_ptr root,
				   pp::PlatformDescription::System & sys,
				   const char * tag_str)
{
	node_ptr tag = this->GetFirstChild(root, tag_str) ;
	while (tag != NULL) {
		pp::PlatformDescription::MulticoreProcessor manycore;
		attr_ptr arch_attr = this->GetFirstAttribute(tag, "arch", true);
		attr_ptr id_attr   = this->GetFirstAttribute(tag, "id", true);

		const char * arch = arch_attr->value();
		short        id;

		// id=""
		try {
			// Yes, this conversion is unsafe. However, there will not probably
			// be over 32767 cpu in one machine...
			id = (short)std::stoi(id_attr->value());
		}
		catch (const std::invalid_argument &e) {
			logger->Error("ID for <%s> is not a valid integer.", tag_str);
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range &e) {
			logger->Error("ID for <%s> is out-of-range.", tag_str);
			return PL_LOGIC_ERROR;
		}

		manycore.SetPrefix(sys.GetPath());
		manycore.SetArchitecture(arch);
		manycore.SetId(id);

		// TODO: extract <info> tag
		///   <gpu> or <acc>
		///        <pe>
		node_ptr pe_tag = this->GetFirstChild(tag, "pe");
		while (pe_tag != NULL) {
			pp::PlatformDescription::ProcessingElement pe;
			ExitCode_t ec = ParseProcessingElement(pe_tag, pe);
			if (ec != PL_SUCCESS)
				return ec;

			manycore.AddProcessingElement(pe);
			pe_tag = pe_tag->next_sibling("pe");
		}

		if (strncmp(tag_str, "gpu", 3) == 0)
			sys.AddGPU(manycore);
		else if (strncmp(tag_str, "acc", 3) == 0)
			sys.AddAccelerator(manycore);
		tag = tag->next_sibling(tag_str);
	}

	return PL_SUCCESS;
}

RXMLPlatformLoader::ExitCode_t
RXMLPlatformLoader::ParseStorages(node_ptr root,
				  pp::PlatformDescription::System & sys)
{
	// Now get all the memories and save them.
	// This should be performed before <cpu> and
	// other tags, due to reference to memories inside them.
	node_ptr storage_tag = this->GetFirstChild(root, "storage", false) ;
	while ( storage_tag != NULL ) {
		pp::PlatformDescription::Storage storage;
		attr_ptr id_attr       = this->GetFirstAttribute(storage_tag, "id",       true);
		attr_ptr quantity_attr = this->GetFirstAttribute(storage_tag, "quantity", true);
		attr_ptr unit_attr     = this->GetFirstAttribute(storage_tag, "unit",     true);
		attr_ptr bw_attr       = this->GetFirstAttribute(storage_tag, "bandwidth",true);
		attr_ptr bw_unit_attr  = this->GetFirstAttribute(storage_tag, "bw_unit",  true);
		attr_ptr type_attr     = this->GetFirstAttribute(storage_tag, "type",     false);

		short        id;
		int          quantity;
		int_fast16_t exp;
		uint64_t     bandwidth;
		int_fast16_t bw_exp;

		// id=""
		try {
			// Yes, this conversion is unsafe. However, there will not probably
			// be over 32767 memories in one machine...
			id = (short)std::stoi(id_attr->value());
		}
		catch (const std::invalid_argument& e) {
			logger->Error("ID for <storage> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range& e) {
			logger->Error("ID for <storage> is out-of-range, please change your unit.");
			return PL_LOGIC_ERROR;
		}

		// quantity=""
		try {
			quantity = std::stoi(quantity_attr->value());
		}
		catch (const std::invalid_argument &e) {
			logger->Error("Quantity for <storage> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range &e) {
			logger->Error("Quantity for <storage> is out-of-range, please increase your unit.");
			return PL_LOGIC_ERROR;
		}

		// unit=""
		switch (ConstHashString(unit_attr->value())) {
		case ConstHashString("B"):
			exp = 0;
			break;
		case ConstHashString("KB"):
			exp = 10;
			break;
		case ConstHashString("MB"):
			exp = 20;
			break;
		case ConstHashString("GB"):
			exp = 20;
			break;
		case ConstHashString("TB"):
			exp = 40;
			break;
		default:
			logger->Error("Invalid `unit` for <storage>.");
			return PL_LOGIC_ERROR;
			break;
		}

		// bandwidth=""
		try {
			bandwidth = std::stoi(bw_attr->value());
		}
		catch (const std::invalid_argument &e) {
			logger->Error("Bandwidth for <storage> is not a valid integer.");
			return PL_LOGIC_ERROR;
		}
		catch (const std::out_of_range &e) {
			logger->Error("Bandwidth for <storage> is out-of-range, please increase your unit.");
			return PL_LOGIC_ERROR;
		}

		// bw_unit=""
		switch (ConstHashString(bw_unit_attr->value())) {
		case ConstHashString("B/s"):
			bw_exp = 0;
			break;
		case ConstHashString("KB/s"):
			bw_exp = 10;
			break;
		case ConstHashString("MB/s"):
			bw_exp = 20;
			break;
		case ConstHashString("GB/s"):
			bw_exp = 20;
			break;
		case ConstHashString("TB/s"):
			bw_exp = 40;
			break;
		default:
			bw_exp = 0;
			break;
		}

		// type=""
		if (type_attr != nullptr) {
			switch (ConstHashString(type_attr->value())) {
			case ConstHashString("hdd"):
				storage.SetStorageType(pp::PlatformDescription::HDD);
				break;
			case ConstHashString("ssd"):
				storage.SetStorageType(pp::PlatformDescription::SSD);
				break;
			case ConstHashString("sd"):
				storage.SetStorageType(pp::PlatformDescription::SD);
				;
				break;
			case ConstHashString("flash"):
				storage.SetStorageType(pp::PlatformDescription::FLASH);
				;
				break;
			default:
				logger->Error("'%s' is not a valid value for `type` attribute.",
					type_attr->value());
				return PL_LOGIC_ERROR;
				break;
			}
		}
		else {
			// If not specified the default is custom.
			storage.SetStorageType(pp::PlatformDescription::CUSTOM);
		}


		storage.SetPrefix(sys.GetPath());
		storage.SetId(id);
		storage.SetQuantity(((int64_t)quantity) << exp);
		storage.SetBandwidth((uint64_t)bandwidth << bw_exp);
		sys.AddStorage(std::make_shared<pp::PlatformDescription::Storage>(storage));

		storage_tag = storage_tag->next_sibling("storage");
	}

	return PL_SUCCESS;
}


}   // namespace plugins
}   // namespace bbque
