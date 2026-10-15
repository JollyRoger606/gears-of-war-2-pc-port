// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors

#include "gow2_android_power.h"

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <optional>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(gow2_android_sustained_performance, false, "GameFixes",
                    "Android: ask for the window's sustained performance mode (lower, steadier "
                    "CPU and GPU clocks where the device supports it)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::glue {

namespace {

bool ClearJavaException(JNIEnv* env) {
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return true;
  }
  return false;
}

std::optional<bool> RequestSustainedPerformanceMode(JNIEnv* env, jobject activity) {
  jclass cls = env->GetObjectClass(activity);
  if (cls == nullptr) {
    ClearJavaException(env);
    return std::nullopt;
  }
  jmethodID method = env->GetMethodID(cls, "requestSustainedPerformanceMode", "(Z)Z");
  env->DeleteLocalRef(cls);
  if (method == nullptr) {
    ClearJavaException(env);
    return std::nullopt;
  }
  const jboolean supported = env->CallBooleanMethod(activity, method, JNI_TRUE);
  if (ClearJavaException(env)) return std::nullopt;
  return supported == JNI_TRUE;
}

}  // namespace

void ApplySustainedPerformanceMode() {
  if (!REXCVAR_GET(gow2_android_sustained_performance)) return;
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) {
    REXLOG_WARN("SUSTAINED performance mode request failed: JNI environment unavailable");
    return;
  }
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  const bool activity_error = ClearJavaException(env);
  if (activity_error || activity == nullptr) {
    if (activity != nullptr) env->DeleteLocalRef(activity);
    REXLOG_WARN("SUSTAINED performance mode request failed: activity unavailable");
    return;
  }
  const std::optional<bool> supported = RequestSustainedPerformanceMode(env, activity);
  env->DeleteLocalRef(activity);
  if (!supported.has_value()) {
    REXLOG_WARN("SUSTAINED performance mode request failed: Java method lookup or call failed");
    return;
  }
  REXLOG_INFO("SUSTAINED performance mode requested: {}",
              *supported ? "supported" : "not supported on this device");
}

}  // namespace rex::glue

#endif
