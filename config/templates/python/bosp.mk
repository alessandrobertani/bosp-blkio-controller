
ifdef CONFIG_SAMPLES_APPTYPE_MYAPP

# Targets provided by this project
.PHONY: samples_apptype_myapp clean_samples_apptype_myapp

# Add this to the "samples" targets
samples_apptype: samples_apptype_myapp
clean_samples_apptype: clean_samples_apptype_myapp

MODULE_SAMPLES_APPTYPE_MYAPP=samples/apptype/MyApp

samples_apptype_myapp: external
	@echo
	@echo "==== Building MyApp ($(BUILD_TYPE)) ===="
	@echo " Using GCC    : $(CC)"
	@echo " Target flags : $(TARGET_FLAGS)"
	@echo " Sysroot      : $(PLATFORM_SYSROOT)"
	@echo " BOSP Options : $(CMAKE_COMMON_OPTIONS)"
	@[ -d $(MODULE_SAMPLES_APPTYPE_MYAPP)/build/$(BUILD_TYPE) ] || \
		mkdir -p $(MODULE_SAMPLES_APPTYPE_MYAPP)/build/$(BUILD_TYPE) || \
		exit 1
	@cd $(MODULE_SAMPLES_APPTYPE_MYAPP)/build/$(BUILD_TYPE) && \
		CC=$(CC) CFLAGS="$(TARGET_FLAGS)" \
		CXX=$(CXX) CXXFLAGS="$(TARGET_FLAGS)" \
		cmake $(CMAKE_COMMON_OPTIONS) ../.. || \
		exit 1
	@cd $(MODULE_SAMPLES_APPTYPE_MYAPP)/build/$(BUILD_TYPE) && \
		make -j$(CPUS) install || \
		exit 1

clean_samples_apptype_myapp:
	@echo
	@echo "==== Clean-up MyApp Application ===="
	@[ ! -f $(BUILD_DIR)/usr/bin/myapp ] || \
		rm -f $(BUILD_DIR)/etc/bbque/recipes/MyApp*; \
		rm -f $(BUILD_DIR)/usr/bin/myapp*
	@rm -rf $(MODULE_SAMPLES_APPTYPE_MYAPP)/build
	@echo

else # CONFIG_SAMPLES_APPTYPE_MYAPP

samples_apptype_myapp:
	$(warning MyApp module disabled by BOSP configuration)
	$(error BOSP compilation failed)

endif # CONFIG_SAMPLES_APPTYPE_MYAPP

