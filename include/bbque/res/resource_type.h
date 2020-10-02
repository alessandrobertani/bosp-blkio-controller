#ifndef BBQUE_RESOURCE_TYPE_H_
#define BBQUE_RESOURCE_TYPE_H_

#include <string>

#include <tg/hw.h>

#define MAX_R_NAME_LEN       6

#define R_TYPE_SYSTEM        1
#define R_TYPE_GROUP         2
#define R_TYPE_CPU           4
#define R_TYPE_PROC_ELEMENT  8
#define R_TYPE_MEMORY        16
#define R_TYPE_ACCELERATOR   32
#define R_TYPE_INTERCONNECT  64
#define R_TYPE_IO            128
#define R_TYPE_NETWORK_IF    256

#define R_ID_ANY            -1
#define R_ID_NONE           -2

#define R_TYPE_COUNT        12

/** Data-type for the Resource IDs */
typedef int16_t BBQUE_RID_TYPE;

namespace bbque
{
namespace res
{

/**
 * @class ResourceType
 *
 * @brief All the manageable resources expected
 */
enum class ResourceType
{
	UNDEFINED = 0,
	SYSTEM       ,
	GROUP        ,
	CPU          ,
	GPU          ,
	ACCELERATOR  ,
	PROC_ELEMENT ,
	MEMORY       ,
	NETWORK_IF   ,
	INTERCONNECT ,
	IO           ,
	CUSTOM       ,
};

/**
 * @brief Return a char string form resource type
 *
 * @param type The resource type value
 * @return A raw char string
 */
const char * GetResourceTypeString(ResourceType type);

/**
 * @brief Return the resource type enumerated value
 *
 * @param str The string form of the resource type
 * @return A resource type value
 */
ResourceType GetResourceTypeFromString(std::string const & str);

/**
 * @brief Return the resource type enumerated value
 *
 * @param arch_type The architecture type of the processing unit
 * @return A resource type
 */
ResourceType GetResourceTypeFromArchitecture(ArchType_t arch_type);

}  // namespace res

}  // namespace bbque

#endif // BBQUE_RESOURCE_TYPE_H_
