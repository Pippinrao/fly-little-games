#pragma once
#include "crypto_port.hpp"
#include <jni.h>

namespace flynes::android::nearby {
// Resolve app classes on a Java-originated call; all actual JCA work is deferred
// to CryptoPort's worker. No owner pointer or Java callback into an owner.
std::shared_ptr<CryptoPort::Backend> java_crypto_backend(JNIEnv*, jobject application_context);
// Internal opaque future-KEY boundary. Caller must be on the controlled worker.
// A valid backend + SecretHandle transfers close responsibility on entry, even
// if wrapping fails/throws. Invalid backend/type leaves caller ownership intact.
std::unique_ptr<CryptoPort::Secret> java_crypto_secret(
    const std::shared_ptr<CryptoPort::Backend>&, JNIEnv*, jobject secret_handle);
#ifdef FLYNES_CRYPTO_JNI_TEST
// Test-library-only worker fault probes; never compiled into nescore.
void crypto_jni_test_fail_next_wrap(int point);
void crypto_jni_test_fail_next_verify();
int crypto_jni_test_close_count();
#endif
}
