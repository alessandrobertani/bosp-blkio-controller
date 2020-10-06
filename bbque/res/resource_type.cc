
#include "bbque/res/resource_type.h"

namespace bbque { namespace res {

static const char * r_type_str[] = {
	"*"    ,
	"sys" ,
	"grp" ,
	"cpu" ,
	"gpu" ,
	"acc" ,
	"pe"  ,
	"mem" ,
	"net" ,
	"icn" ,
	"io"  ,
	"cst"
};

const char * GetResourceTypeString(ResourceType type) {
	return r_type_str[static_cast<int>(type)];
}

ResourceType GetResourceTypeFromString(std::string const & _str) {
	for (int i = 0; i < R_TYPE_COUNT; ++i) {
		ResourceType r_type = static_cast<ResourceType>(i);
		if (_str.compare(r_type_str[i]) == 0)
			return r_type;
	}
	return ResourceType::UNDEFINED;
}


ResourceType GetResourceTypeFromArchitecture(ArchType_t arch_type)
{
	switch (arch_type) {

	case ArchType::NONE:
		return ResourceType::UNDEFINED;

	case ArchType::CPU:
	case ArchType::X86:
	case ArchType::X86_64:
	case ArchType::ARM_V7:
	case ArchType::ARM_V8:
		return ResourceType::CPU;

	case ArchType::NVIDIA:
		return ResourceType::GPU;

	case ArchType::GN:
#ifdef CONFIG_MANGO_GN_EMULATION
		return ResourceType::ACCELERATOR;
#else
		return ResourceType::CPU;
#endif

	default:
		return ResourceType::ACCELERATOR;
	}

	return ResourceType::UNDEFINED;
}

} // namespace res

} // namespace bbque
