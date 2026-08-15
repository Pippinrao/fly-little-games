// Placeholder JNI stub so the native build pipeline compiles from Task 0.
// Replaced with the real JNI bridge in Task 7.
#include <jni.h>

extern "C" JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_stub(JNIEnv*, jclass) {
    return 0;
}
