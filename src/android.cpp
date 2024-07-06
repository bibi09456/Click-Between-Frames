// copied from https://github.com/qimiko/click-on-steps/blob/main/src/android.cpp

#include <Geode/cocos/platform/android/jni/JniHelper.h>
#include <time.h>

#include "platform.hpp"

using namespace geode::prelude;

cbf::TimestampType cbf::getCurrentTime() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec * 1000) + (now.tv_nsec / 1'000'000);
}

void clearJNIExceptions() {
	auto vm = cocos2d::JniHelper::getJavaVM();

	JNIEnv* env;
	if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
		env->ExceptionClear();
	}
}

bool reportPlatformCapability(std::string id) {
	cocos2d::JniMethodInfo t;
	if (cocos2d::JniHelper::getStaticMethodInfo(t, "com/geode/launcher/utils/GeodeUtils", "reportPlatformCapability", "(Ljava/lang/String;)Z")) {
		jstring stringArg1 = t.env->NewStringUTF(id.c_str());

		auto r = t.env->CallStaticBooleanMethod(t.classID, t.methodID, stringArg1);

		t.env->DeleteLocalRef(stringArg1);
		t.env->DeleteLocalRef(t.classID);

		return r;
	} else {
		clearJNIExceptions();
	}

	return false;
}

cbf::TimestampType g_lastTimestamp;

void JNICALL JNI_setNextInputTimestamp(JNIEnv* env, jobject, jlong timestamp) {
	auto timestampMs = timestamp / 1'000'000;
	g_lastTimestamp = timestampMs;
}

#include <Geode/modify/CCTouchDispatcher.hpp>
class $modify(CCTouchDispatcher) {
	void touches(cocos2d::CCSet* touches, cocos2d::CCEvent* event, unsigned int index) {
		CCTouchDispatcher::touches(touches, event, index);

		if (index == CCTOUCHBEGAN || index == CCTOUCHENDED) {
			auto& manager = cbf::Manager::get();
			auto state = index == CCTOUCHBEGAN ? cbf::InputState::Press : cbf::InputState::Release;
			// log::debug("input timestamp is {}, state {}", g_lastTimestamp, int(state));
			manager.addInput(cbf::Input { .time = g_lastTimestamp, .state = state });
			g_lastTimestamp = 0;
		}
	}
};

static JNINativeMethod methods[] = {
	{
		"setNextInputTimestamp",
		"(J)V",
		reinterpret_cast<void*>(&JNI_setNextInputTimestamp)
	},
};

$on_mod(Loaded) {
	auto vm = cocos2d::JniHelper::getJavaVM();

	JNIEnv* env;
	if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
		auto clazz = env->FindClass("com/geode/launcher/utils/GeodeUtils");
		if (env->RegisterNatives(clazz, methods, 1) != 0) {
			// method was not found
			clearJNIExceptions();
			geode::log::warn("the launcher doesn't support input timestamp api!");
		} else {
			reportPlatformCapability("timestamp_inputs");
		}
	}
}