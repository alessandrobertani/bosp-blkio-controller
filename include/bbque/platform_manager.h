#ifndef BBQUE_PLATFORM_MANAGER_H
#define BBQUE_PLATFORM_MANAGER_H

#include "bbque/pp/local_platform_proxy.h"
#include "bbque/pp/remote_platform_proxy.h"
#include "bbque/utils/worker.h"

#include <memory>

#define PLATFORM_MANAGER_NAMESPACE "bq.plm"

#define PLATFORM_MANAGER_EV_REFRESH  0
#define PLATFORM_MANAGER_EV_COUNT    1

namespace bbque
{

class PlatformManager : public PlatformProxy, public utils::Worker, public CommandHandler
{
public:
	/**
	 * @brief Get a reference to the paltform proxy
	 */
	static PlatformManager & GetInstance();

	/**
	 * @brief Return the Platform specific string identifier
	 * @param system_id It specifies from which system take the
	 *       platform identifier. If not specified or equal
	 *       to "-1", the platorm id of the local system is returned.
	 */
	const char* GetPlatformID(int16_t system_id = -1) const override;

	/**
	 * @brief Return the Hardware identifier string
	 * @param system_id It specifies from which system take the
	 *       platform idenfier. If not specified or equal
	 *       to "-1", the hw id of the local system is returned.
	 */
	const char* GetHardwareID(int16_t system_id = -1) const override;

	/**
	 * @brief Platform specific resource setup interface.
	 * @warning Not implemented in PlatformManager!
	 */
	ExitCode_t Setup(SchedPtr_t papp) override;

	/**
	 * @brief Platform specific resources enumeration
	 *
	 * The default implementation of this method loads the TPD, is such a
	 * function has been enabled
	 */
	ExitCode_t LoadPlatformData() override;

	/**
	 * @brief Platform specific resources refresh
	 */
	ExitCode_t Refresh() override;

	/**
	 * @brief Platform specific resources release interface.
	 */
	ExitCode_t Release(SchedPtr_t papp) override;

	/**
	 * @brief Platform specific resource claiming interface.
	 */
	ExitCode_t ReclaimResources(SchedPtr_t papp) override;

	/**
	 * @brief Bind the specified resources to the specified application.
	 *
	 * @param papp The application which resources are assigned
	 * @param pres The resources to be assigned
	 * @param excl If true the specified resources are assigned for exclusive
	 * usage to the application
	 */
	ExitCode_t MapResources(
	        SchedPtr_t papp, ResourceAssignmentMapPtr_t pres, bool excl = true) override;

	/**
	 * @brief Set the power management configuration set by the scheduling
	 * policy
	 *
	 * @return PLATFORM_OK for success
	 */
	ExitCode_t ActuatePowerManagement() override;

	/**
	 * @brief Set the power management configuration set by the scheduling
	 * policy, for a specific resource
	 *
	 * @return PLATFORM_OK for success
	 */
	ExitCode_t ActuatePowerManagement(bbque::res::ResourcePtr_t resource) override;

	/**
	 * @brief Check if the resource is a "high-performance" is single-ISA
	 * heterogeneous platforms
	 *
	 * @return true if so, false otherwise
	 */
	bool IsHighPerformance(bbque::res::ResourcePathPtr_t const & path) const override;

	/**
	 * @brief Platform specific termination.
	 */
	void Exit() override;

	/******************************************************************
	 * CheckpointRestoreIF inherithed member functions                *
	 ******************************************************************/

	CheckpointRestoreIF::ExitCode_t Dump(app::SchedPtr_t psched) override;

	CheckpointRestoreIF::ExitCode_t Restore(app::SchedPtr_t psched) override;

	CheckpointRestoreIF::ExitCode_t Freeze(app::SchedPtr_t psched) override;

	CheckpointRestoreIF::ExitCode_t Thaw(app::SchedPtr_t papp) override;


	/**
	 * @brief Load the configuration via the corresponding plugin
	 *        It encapsulate exceptions coming from plugins.
	 * @return PL_SUCCESS in case of successful load, an error
	 *         otherwise.
	 */
	ExitCode_t LoadPlatformConfig();

	/**
	 * @brief Get a reference to the local platform proxy
	 * @return A PlatformProxy reference
	 */
	inline PlatformProxy const & GetLocalPlatformProxy() {
		return *lpp;
	}

#ifdef CONFIG_BBQUE_DIST_MODE
	/**
	 * @brief Get a reference to the remote platform proxy
	 * @return A PlatformProxy reference
	 */
	inline PlatformProxy const & GetRemotePlatformProxy() {
		return *rpp;
	}
#endif

	/**
	 * @brief Retrieve the local system ID
	 * @return the id number -1 if not set)
	 */
	int16_t GetLocalSystemId() const noexcept {
		return local_system_id;
	}

private:

	/**
	 * @brief True if remote and local platform has been initialized
	 */
	bool platforms_initialized = false;

	/**
	 * @brief the local system id number
	 */
	int16_t local_system_id = -1;

	/**
	 * @brief The logger used by the worker thread
	 */
	std::unique_ptr<bu::Logger> logger;

	/**
	 * @brief Pointer to local platform proxy
	 */
	std::unique_ptr<pp::LocalPlatformProxy> lpp;

#ifdef CONFIG_BBQUE_DIST_MODE
	/**
	 * @brief Pointer to remote platform proxy
	 */
	std::unique_ptr<pp::RemotePlatformProxy> rpp;

#endif // CONFIG_BBQUE_DIST_MODE
	/**
	 * @brief The set of flags related to pending platform events to handle
	 */
	std::bitset<PLATFORM_MANAGER_EV_COUNT> platformEvents;

	/**
	 * @brief The thread main code
	 */
	void Task() override final;

	/**
	 * @brief The constructor. It is private due to singleton pattern
	 */
	PlatformManager();

	/**
	 * @brief Release all the platform proxy resources
	 */
	~PlatformManager();

	/**
	 * @brief Copy operator not allowed in singletons
	 */
	PlatformManager(PlatformManager const&) = delete;

	/**
	 * @brief Assigment-Copy operator not allowed in singletons
	 */
	void operator=(PlatformManager const&)  = delete;

	/**
	 * @brief Set/update of the local system id number
	 */
	void UpdateLocalSystemId();

	/**
	 * @brief The command handler callback function
	 */
	int CommandsCb(int argc, char *argv[]);

};

} // namespace bbque
#endif // PLATFORMMANAGER_H
