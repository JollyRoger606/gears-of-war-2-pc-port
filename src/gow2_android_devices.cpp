// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#include "gow2_android_devices.h"

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <vector>

namespace rex::glue {

namespace {

constexpr jint kConfigKeyboardQwerty = 2;
constexpr jint kConfigHardKeyboardHiddenYes = 2;
constexpr jint kConfigTouchscreenFinger = 3;
constexpr jint kSourceMouse = 0x00002002;

bool ClearJavaException(JNIEnv* env) {
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return true;
  }
  return false;
}

jobject CallObjectGetter(JNIEnv* env, jobject object, const char* method, const char* signature) {
  if (object == nullptr) return nullptr;
  jclass cls = env->GetObjectClass(object);
  if (cls == nullptr) {
    ClearJavaException(env);
    return nullptr;
  }
  jmethodID id = env->GetMethodID(cls, method, signature);
  jobject result = nullptr;
  if (id != nullptr) {
    result = env->CallObjectMethod(object, id);
    if (ClearJavaException(env)) {
      if (result != nullptr) env->DeleteLocalRef(result);
      result = nullptr;
    }
  } else {
    ClearJavaException(env);
  }
  env->DeleteLocalRef(cls);
  return result;
}

jint ReadIntField(JNIEnv* env, jobject object, const char* field, jint fallback) {
  if (object == nullptr) return fallback;
  jclass cls = env->GetObjectClass(object);
  if (cls == nullptr) {
    ClearJavaException(env);
    return fallback;
  }
  jfieldID id = env->GetFieldID(cls, field, "I");
  jint value = fallback;
  if (id != nullptr) {
    value = env->GetIntField(object, id);
    if (ClearJavaException(env)) value = fallback;
  } else {
    ClearJavaException(env);
  }
  env->DeleteLocalRef(cls);
  return value;
}

jobject GetConfiguration(JNIEnv* env) {
  if (env == nullptr) return nullptr;
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (activity == nullptr) return nullptr;
  jobject resources =
      CallObjectGetter(env, activity, "getResources", "()Landroid/content/res/Resources;");
  env->DeleteLocalRef(activity);
  if (resources == nullptr) return nullptr;
  jobject configuration =
      CallObjectGetter(env, resources, "getConfiguration",
                       "()Landroid/content/res/Configuration;");
  env->DeleteLocalRef(resources);
  return configuration;
}

std::vector<jint> GetDeviceIds(JNIEnv* env, jclass deviceClass) {
  jmethodID method = env->GetStaticMethodID(deviceClass, "getDeviceIds", "()[I");
  if (method == nullptr) {
    ClearJavaException(env);
    return {};
  }
  jintArray array =
      static_cast<jintArray>(env->CallStaticObjectMethod(deviceClass, method));
  if (ClearJavaException(env) || array == nullptr) {
    if (array != nullptr) env->DeleteLocalRef(array);
    return {};
  }
  const jsize length = env->GetArrayLength(array);
  if (length <= 0) {
    env->DeleteLocalRef(array);
    return {};
  }
  std::vector<jint> ids(static_cast<size_t>(length));
  env->GetIntArrayRegion(array, 0, length, ids.data());
  const bool error = ClearJavaException(env);
  env->DeleteLocalRef(array);
  if (error) return {};
  return ids;
}

bool IsExternalRealMouse(JNIEnv* env, jclass deviceClass, jint id) {
  jmethodID getDevice =
      env->GetStaticMethodID(deviceClass, "getDevice", "(I)Landroid/view/InputDevice;");
  if (getDevice == nullptr) {
    ClearJavaException(env);
    return false;
  }
  jobject device = env->CallStaticObjectMethod(deviceClass, getDevice, id);
  if (ClearJavaException(env) || device == nullptr) {
    if (device != nullptr) env->DeleteLocalRef(device);
    return false;
  }
  jmethodID isExternal = env->GetMethodID(deviceClass, "isExternal", "()Z");
  jmethodID isVirtual = env->GetMethodID(deviceClass, "isVirtual", "()Z");
  jmethodID getSources = env->GetMethodID(deviceClass, "getSources", "()I");
  if (isExternal == nullptr || isVirtual == nullptr || getSources == nullptr) {
    ClearJavaException(env);
    env->DeleteLocalRef(device);
    return false;
  }
  // The S25 Ultra's built-in "sec_touchpad" reports SOURCE_MOUSE too.
  const jboolean external = env->CallBooleanMethod(device, isExternal);
  if (ClearJavaException(env) || !external) {
    env->DeleteLocalRef(device);
    return false;
  }
  const jboolean virtualDev = env->CallBooleanMethod(device, isVirtual);
  if (ClearJavaException(env) || virtualDev) {
    env->DeleteLocalRef(device);
    return false;
  }
  const jint sources = env->CallIntMethod(device, getSources);
  if (ClearJavaException(env)) {
    env->DeleteLocalRef(device);
    return false;
  }
  env->DeleteLocalRef(device);
  return (sources & kSourceMouse) == kSourceMouse;
}

}  // namespace

bool PhysicalKeyboardConnected() {
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return false;
  jobject configuration = GetConfiguration(env);
  if (configuration == nullptr) return false;
  jint keyboard = ReadIntField(env, configuration, "keyboard", 0);
  jint hidden = ReadIntField(env, configuration, "hardKeyboardHidden", 0);
  env->DeleteLocalRef(configuration);
  // Configuration.keyboard is QWERTY only while an alphabetic, non-virtual keyboard is attached;
  // SDL keeps no keyboard list on Android, so SDL_HasKeyboard() is always false there.
  return keyboard == kConfigKeyboardQwerty && hidden != kConfigHardKeyboardHiddenYes;
}

bool ExternalMouseConnected() {
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return false;
  jclass deviceClass = env->FindClass("android/view/InputDevice");
  if (deviceClass == nullptr) {
    ClearJavaException(env);
    return false;
  }
  const std::vector<jint> ids = GetDeviceIds(env, deviceClass);
  bool found = false;
  for (jint id : ids) {
    if (IsExternalRealMouse(env, deviceClass, id)) {
      found = true;
      break;
    }
  }
  env->DeleteLocalRef(deviceClass);
  return found;
}

bool TouchscreenPresent() {
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return false;
  jobject configuration = GetConfiguration(env);
  if (configuration == nullptr) return false;
  jint touchscreen = ReadIntField(env, configuration, "touchscreen", 0);
  env->DeleteLocalRef(configuration);
  return touchscreen == kConfigTouchscreenFinger;
}

}  // namespace rex::glue

#endif
