#pragma once
#include <jni.h>
#include "content_port.hpp"
namespace flynes::android::nearby {
bool java_content_callbacks(JNIEnv*, jobject, ContentPort::Callbacks*);
}
