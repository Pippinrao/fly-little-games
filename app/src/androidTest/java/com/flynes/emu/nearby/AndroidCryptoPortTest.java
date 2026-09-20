package com.flynes.emu.nearby;

import android.content.Context;
import android.os.Build;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import java.util.Arrays;
import java.math.BigInteger;
import static org.junit.Assert.assertEquals;
import static org.junit.Assume.assumeTrue;

/** Same production native sources in a test-only library; not owner registration. */
@RunWith(AndroidJUnit4.class)
public final class AndroidCryptoPortTest {
    static { System.loadLibrary("nearby_crypto_test"); }
    private static native int exercise(Context context, Class<?> helpers);

    /** Invoked only on the adapter worker; maximum three retained UUID aliases. */
    public static Object[] createMaterial(Context context) throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(context);
        String scope = "crypto-port-test-" + UUID.randomUUID();
        AndroidSecureProvider.KeyHandle left = provider.generate(
                AndroidSecureProvider.Purpose.PAIR_ECDH, scope + "-left");
        AndroidSecureProvider.KeyHandle right = provider.generate(
                AndroidSecureProvider.Purpose.PAIR_ECDH, scope + "-right");
        AndroidSecureProvider.KeyHandle signer = provider.generate(
                AndroidSecureProvider.Purpose.SESSION_SIGNING, scope + "-sign");
        byte[] domain = "crypto-port-peer-test-v1".getBytes(StandardCharsets.US_ASCII);
        byte[] digest = provider.random(32);
        byte[] signature = provider.signPrehashed(signer, "crypto-port-peer-test-v1", digest);
        byte[] highS = signature.clone();
        BigInteger order = new BigInteger("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551", 16);
        byte[] oppositeS = order.subtract(new BigInteger(1, Arrays.copyOfRange(signature,32,64))).toByteArray();
        Arrays.fill(highS,32,64,(byte)0);
        int copied = Math.min(32, oppositeS.length);
        System.arraycopy(oppositeS,oppositeS.length-copied,highS,64-copied,copied);
        return new Object[] {
            provider.keyAgree(left, provider.publicKeyX963(right)),
            provider.keyAgree(right, provider.publicKeyX963(left)),
            provider.publicKeyX963(signer), domain, digest,
            signature, highS
        };
    }

    @Test public void realProviderPrimitivesCrossTheCAbiWithoutExportingSecrets() {
        assumeTrue("AndroidKeyStore ECDH requires API 31; no substitute key", Build.VERSION.SDK_INT >= 31);
        assertEquals("C ABI + JNI + genuine KeyStore/JCA crypto cases", 0,
                exercise(ApplicationProvider.getApplicationContext(), AndroidCryptoPortTest.class));
    }
}
