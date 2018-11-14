find_package(Java REQUIRED)
if (JAVA_FOUND)
        include(UseJava)
else ()
        message (FATAL_ERROR "Unable to find JAVA.")
endif()

INCLUDE (${CONFIGPATH})
#message("Config path: ${CONFIGPATH}")

set(GRADLE_SETUP "gradle_setup.sh")
set(GRADLE_SETUP_ARGS "${CMAKE_CURRENT_SOURCE_DIR} ${CONFIG_DEPLOYMENT_ANDROID_SDK_PATH}")
set(GRADLE "gradlew")
set(GRADLE_FLAGS "-p")
set(GRADLE_OPTS "${CMAKE_CURRENT_SOURCE_DIR}")
set(GRADLE_ARGS "assemble")

message("==== Building bbque_service AAR ====")

message("SDK Path: ${CONFIG_DEPLOYMENT_ANDROID_SDK_PATH}")
message("Setting Gradle local.properties:${CMAKE_CURRENT_SOURCE_DIR}/${GRADLE_SETUP} ${GRADLE_SETUP_ARGS}")
execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/${GRADLE_SETUP} ${CMAKE_CURRENT_SOURCE_DIR} ${CONFIG_DEPLOYMENT_ANDROID_SDK_PATH})

message("Executing Gradle assemble: ${CMAKE_CURRENT_SOURCE_DIR}/${GRADLE} ${GRADLE_FLAGS} ${GRADLE_OPTS} ${GRADLE_ARGS}")
execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/${GRADLE} ${GRADLE_FLAGS} ${GRADLE_OPTS} ${GRADLE_ARGS})
