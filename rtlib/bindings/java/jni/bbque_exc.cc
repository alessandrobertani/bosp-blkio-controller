#include <jni.h>
#include <bbque/bbque_exc.h>
#include "bbque_exc.h"
#include "bbque_rtlib_commons.h"
#include "bbque_rtlib_enums.h"

jlong Java_bbque_rtlib_model_BbqueEXC_initNative(JNIEnv *env, jobject obj, jstring java_name, jstring java_recipe, jobject java_services) {
	const char *native_name = env->GetStringUTFChars(java_name, JNI_FALSE);
	const char *native_recipe = env->GetStringUTFChars(java_recipe, JNI_FALSE);
	jlong rtlib_native_pointer = getObjectNativePointer(env, java_services);
	RTLIB_Services_t *rtlib = reinterpret_cast<RTLIB_Services_t *>(rtlib_native_pointer);
	JNIBbqueEXC *jni_exc = new JNIBbqueEXC(native_name, native_recipe, rtlib, env, obj);
	jlong native_pointer;
	if (!jni_exc) {
		native_pointer = 0;
		jclass exception_class = env->FindClass("bbque/rtlib/exception/RTLibRegistrationException");
		env->ThrowNew(exception_class, "Execution context registration failed");
	} else {
		native_pointer = reinterpret_cast<jlong>(jni_exc);
	}
	env->ReleaseStringUTFChars(java_name, native_name);
	env->ReleaseStringUTFChars(java_recipe, native_recipe);
	return native_pointer;
}

jboolean Java_bbque_rtlib_model_BbqueEXC_isRegistered(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->isRegistered();
}

void Java_bbque_rtlib_model_BbqueEXC_start(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->Start();
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_waitCompletion(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->WaitCompletion();
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_terminate(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->Terminate();
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_enable(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->Enable();
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_disable(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->Disable();
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_setAWMConstraints(JNIEnv *env, jobject obj, jobjectArray java_constraints) {
	int array_size = env->GetArrayLength(java_constraints);
	RTLIB_Constraint_t native_array[array_size];
	for (int i = 0; i < array_size; i++) {
		jobject java_constraint = env->GetObjectArrayElement(java_constraints, i);
		jclass java_class = env->GetObjectClass(java_constraint);
		jfieldID jni_awm_id = env->GetFieldID(java_class, "mAwm", "I");
		jfieldID jni_operation_id = env->GetFieldID(java_class, "mOperation", "Lbbque/rtlib/enumeration/RTLibConstraintOperation;");
		jfieldID jni_type_id = env->GetFieldID(java_class, "mType", "Lbbque/rtlib/enumeration/RTLibConstraintType;");
		jint jni_awm = env->GetIntField(java_constraint, jni_awm_id);
		jobject jni_operation_obj = env->GetObjectField(java_constraint, jni_operation_id);
		jclass java_operation_class = env->GetObjectClass(jni_operation_obj);
		jfieldID java_operation_value_id = env->GetFieldID(java_operation_class, "mJNIValue", "I");
		jint jni_operation_value = env->GetIntField(jni_operation_obj, java_operation_value_id);
		jobject jni_type_obj = env->GetObjectField(java_constraint, jni_type_id);
		jclass java_type_class = env->GetObjectClass(jni_type_obj);
		jfieldID java_type_value_id = env->GetFieldID(java_type_class, "mJNIValue", "I");
		jint jni_type_value = env->GetIntField(jni_type_obj, java_type_value_id);
		native_array[i].awm = (uint8_t) jni_awm;
		native_array[i].operation = getNativeConstraintOperation(jni_operation_value);
		native_array[i].type = getNativeConstraintType(jni_type_value);
	}
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->SetAWMConstraints(native_array, array_size);
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_clearAWMConstraints(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->ClearAWMConstraints();
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_setGoalGap(JNIEnv *env, jobject obj, jint goal_gap_percent) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->SetGoalGap(goal_gap_percent);
	throwRTLibExceptionIfNecessary(env, code);
}

jstring Java_bbque_rtlib_model_BbqueEXC_getUniqueID_1String(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return env->NewStringUTF(jni_exc->GetUniqueID_String());
}

jint Java_bbque_rtlib_model_BbqueEXC_getUniqueID(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->GetUniqueID();
}

jint Java_bbque_rtlib_model_BbqueEXC_getAssignedResources__Lbbque_rtlib_enumeration_RTLibResourceType_2(JNIEnv *env, jobject obj, jobject java_resource_type) {
	jclass java_resource_type_class = env->GetObjectClass(java_resource_type);
	jfieldID java_resource_type_value_id = env->GetFieldID(java_resource_type_class, "mJNIValue", "I");
	jint jni_resource_type_value = env->GetIntField(java_resource_type, java_resource_type_value_id);
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ResourceType_t resource_type = getNativeResourceType(jni_resource_type_value);	
	int value;
	RTLIB_ExitCode_t code = jni_exc->GetAssignedResources(resource_type, value);
	throwRTLibExceptionIfNecessary(env, code);
	return value;
}

jintArray Java_bbque_rtlib_model_BbqueEXC_getAffinityMask(JNIEnv *env, jobject obj, jintArray java_ids_vector) {
	int array_size = env->GetArrayLength(java_ids_vector);
	jint *native_ids_vector = env->GetIntArrayElements(java_ids_vector, 0);
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->GetAffinityMask(native_ids_vector, array_size);
	env->ReleaseIntArrayElements(java_ids_vector, native_ids_vector, 0);
	throwRTLibExceptionIfNecessary(env, code);
	return java_ids_vector;
}

jintArray Java_bbque_rtlib_model_BbqueEXC_getAssignedResources__Lbbque_rtlib_enumeration_RTLibResourceType_2_3I
(JNIEnv *env, jobject obj, jobject java_resource_type, jintArray java_systems) {
	jclass java_resource_type_class = env->GetObjectClass(java_resource_type);
	jfieldID java_resource_type_value_id = env->GetFieldID(java_resource_type_class, "mJNIValue", "I");
	jint jni_resource_type_value = env->GetIntField(java_resource_type, java_resource_type_value_id);
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ResourceType_t resource_type = getNativeResourceType(jni_resource_type_value);	
	int array_size = env->GetArrayLength(java_systems);
	jint *native_systems = env->GetIntArrayElements(java_systems, 0);
	RTLIB_ExitCode_t code = jni_exc->GetAssignedResources(resource_type, native_systems, array_size);
	env->ReleaseIntArrayElements(java_systems, native_systems, 0);
	throwRTLibExceptionIfNecessary(env, code);
	return java_systems;
}

void Java_bbque_rtlib_model_BbqueEXC_setCPS(JNIEnv *env, jobject obj, jfloat java_cps) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->SetCPS(java_cps);
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_setJPSGoal(JNIEnv *env, jobject obj, jfloat java_jps_min, jfloat java_jps_max, jint java_jpc) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->SetJPSGoal(java_jps_min, java_jps_max, java_jpc);
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_updateJPC(JNIEnv *env, jobject obj, jint java_jpc) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->UpdateJPC(java_jpc);
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_setCPSGoal__FF(JNIEnv *env, jobject obj, jfloat java_cps_min, jfloat java_cps_max) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->SetCPSGoal(java_cps_min, java_cps_max);
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_setCPSGoal__F(JNIEnv *env, jobject obj, jfloat java_cps) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->SetCPSGoal(java_cps);
	throwRTLibExceptionIfNecessary(env, code);
}

void Java_bbque_rtlib_model_BbqueEXC_setMinimumCycleTimeUs(JNIEnv *env, jobject obj, jint java_min_cycle_time_us) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_ExitCode_t code = jni_exc->SetMinimumCycleTimeUs(java_min_cycle_time_us);
	throwRTLibExceptionIfNecessary(env, code);
}

jfloat Java_bbque_rtlib_model_BbqueEXC_getCPS(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->GetCPS();
}

jfloat Java_bbque_rtlib_model_BbqueEXC_getJPS(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->GetJPS();
}

jint Java_bbque_rtlib_model_BbqueEXC_getCTimeUs(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->GetCTimeUs();
}

jint Java_bbque_rtlib_model_BbqueEXC_getCTimeMs(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->GetCTimeMs();
}

jint Java_bbque_rtlib_model_BbqueEXC_cycles(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->Cycles();
}

jobject Java_bbque_rtlib_model_BbqueEXC_workingModeParams(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_WorkingModeParams_t wmp = jni_exc->WorkingModeParams();
	jlong native_services_pointer = reinterpret_cast<jlong>(wmp.services);
	jclass java_services_class = env->FindClass("bbque/rtlib/model/RTLibServices");
	jmethodID java_services_method_id = env->GetMethodID(java_services_class, "<init>", "(J)V");
	jobject java_services = env->NewObject(java_services_class, java_services_method_id, native_services_pointer);
	jclass java_system_resources_class = env->FindClass("bbque/rtlib/model/RTLibSystemResources");
	jmethodID java_system_resources_method_id = env->GetMethodID(java_system_resources_class, "<init>", "(IIIIIIII)V");
	jobjectArray java_system_resources = env->NewObjectArray(wmp.nr_sys, java_system_resources_class, NULL);
	for (int i = 0; i < wmp.nr_sys; i++) {
		RTLIB_SystemResources_t system = wmp.systems[i];
		jobject java_system = env->NewObject(java_system_resources_class, java_system_resources_method_id, system.sys_id,
				system.number_cpus, system.number_proc_elements, system.cpu_bandwidth, system.mem_bandwidth,
#ifdef CONFIG_TARGET_OPENCL
				system.gpu_bandwidth, system.accelerator_bandwidth, system.ocl_device_id
#else
				0, 0, 0
#endif
				);
		env->SetObjectArrayElement(java_system_resources, i, java_system);
	}
	jclass java_working_mode_params_class = env->FindClass("bbque/rtlib/model/RTLibWorkingModeParams");
	jmethodID java_working_mode_params_method_id = env->GetMethodID(java_working_mode_params_class, "<init>", "(I"
									"Lbbque/rtlib/model/RTLibServices;"
									"[Lbbque/rtlib/model/RTLibSystemResources;)V");
	return env->NewObject(java_working_mode_params_class, java_working_mode_params_method_id, wmp.awm_id, java_services, java_system_resources);
}

jboolean Java_bbque_rtlib_model_BbqueEXC_done(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->Done();
}

jint Java_bbque_rtlib_model_BbqueEXC_currentAWM(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	return jni_exc->CurrentAWM();
}

jobject Java_bbque_rtlib_model_BbqueEXC_configuration(JNIEnv *env, jobject obj) {
	jlong native_pointer = getObjectNativePointer(env, obj);
	JNIBbqueEXC *jni_exc = reinterpret_cast<JNIBbqueEXC *>(native_pointer);
	RTLIB_Conf_t config = jni_exc->Configuration();
	return createRTLibConfigObjFromNativeObj(env, config);
}

RTLIB_ExitCode_t JNIBbqueEXC::onGenericIntCallback(const char *method, const char *signature, ...) {
	va_list arguments;
	va_start(arguments, signature);
	JNIEnv *env;
	jvm->AttachCurrentThread((void **)&env, NULL);
	jmethodID callback_method = env->GetMethodID(callback_class, method, signature);
	jobject java_exit_code = env->CallObjectMethodV(callback_obj, callback_method, arguments);
	jclass java_exit_code_class = env->GetObjectClass(java_exit_code);
	jfieldID jni_exit_code_value_id = env->GetFieldID(java_exit_code_class, "mJNIValue", "I");
	jint jni_exit_code_value = env->GetIntField(java_exit_code, jni_exit_code_value_id);
	va_end(arguments);
	jvm->DetachCurrentThread();
	return getNativeExitCode(jni_exit_code_value);
}
