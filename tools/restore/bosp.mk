
# ifdef CONFIG_BBQUE_MPI

# Targets provided by this project
.PHONY: bbque-restore-exc clean_bbque-restore-exc

# Add this to the "contrib_testing" target
testing: bbque-restore-exc
clean_testing: clean_bbque-restore-exc

MODULE_BBQUE_RESTORE_EXC=barbeque/tools/restore/

bbque-restore-exc: external
	@echo
	@echo "==== Building bbque-restore-exc ($(BUILD_TYPE)) ===="
	@echo " Using GCC    : $(CC)"
	@echo " Target flags : $(TARGET_FLAGS)"
	@echo " Sysroot      : $(PLATFORM_SYSROOT)"
	@echo " BOSP Options : $(CMAKE_COMMON_OPTIONS)"
	@[ -d $(MODULE_BBQUE_RESTORE_EXC)/build/$(BUILD_TYPE) ] || \
		mkdir -p $(MODULE_BBQUE_RESTORE_EXC)/build/$(BUILD_TYPE) || \
		exit 1
	@cd $(MODULE_BBQUE_RESTORE_EXC)/build/$(BUILD_TYPE) && \
		CC=$(CC) CFLAGS=$(TARGET_FLAGS) \
		CXX=$(CXX) CXXFLAGS=$(TARGET_FLAGS) \
		cmake $(CMAKE_COMMON_OPTIONS) ../.. || \
		exit 1
	@cd $(MODULE_BBQUE_RESTORE_EXC)/build/$(BUILD_TYPE) && \
		make -j$(CPUS) install || \
		exit 1

clean_bbque-restore-exc:
	@echo
	@echo "==== Clean-up BbqueRestoreEXC Application ===="
	@[ ! -f $(BUILD_DIR)/usr/bin/bbque-restore-exc ] || \
		rm -f $(BUILD_DIR)/usr/bin/bbque-restore-exc*
	@rm -rf $(MODULE_BBQUE_RESTORE_EXC)/build
	@echo

# endif # CONFIG_BBQUE_MPI

