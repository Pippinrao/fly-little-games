package com.flynes.emu.nearby;

import static org.junit.Assert.*;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.security.Signature;
import java.security.MessageDigest;
import java.util.Arrays;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

@RunWith(AndroidJUnit4.class)
public final class AndroidSecureProviderTest {
    @Test public void prehashedSigningIsExactNonExportableAndPurposeBound() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        String scope = "instrumentation-" + System.nanoTime();
        AndroidSecureProvider.KeyHandle identity = provider.generate(
                AndroidSecureProvider.Purpose.DEVICE_IDENTITY, scope);
        try {
            byte[] digest = new byte[32];
            for (int i = 0; i < digest.length; i++) digest[i] = (byte) (i + 1);
            byte[] signature = provider.signPrehashed(
                    identity, "flynes-pair-signature-v1", digest);
            assertEquals(64, signature.length);
            assertTrue(AndroidSecureProvider.isCanonicalLowS(signature));
            Signature verifier = Signature.getInstance("NONEwithECDSA");
            verifier.initVerify(provider.publicKey(identity));
            verifier.update(digest);
            assertTrue(verifier.verify(AndroidSecureProvider.rawToDer(signature)));

            byte[] doubleHash = java.security.MessageDigest.getInstance("SHA-256")
                    .digest(digest);
            verifier.initVerify(provider.publicKey(identity));
            verifier.update(doubleHash);
            assertFalse(verifier.verify(AndroidSecureProvider.rawToDer(signature)));
            assertNull(identity.privateKey().getEncoded());

            AndroidSecureProvider.KeyHandle ecdh = provider.generate(
                    AndroidSecureProvider.Purpose.PAIR_ECDH, scope + "-ecdh");
            try {
                assertThrows(IllegalArgumentException.class,
                        () -> provider.signPrehashed(
                                ecdh, "flynes-pair-signature-v1", digest));
            } finally {
                provider.destroy(ecdh);
            }
        } finally {
            provider.destroy(identity);
        }
    }

    @Test public void independentEcdhHandlesAgreeAndWrongPurposeFails() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        String scope = "ecdh-" + System.nanoTime();
        AndroidSecureProvider.KeyHandle left = provider.generate(
                AndroidSecureProvider.Purpose.PAIR_ECDH, scope + "-left");
        AndroidSecureProvider.KeyHandle right = provider.generate(
                AndroidSecureProvider.Purpose.PAIR_ECDH, scope + "-right");
        AndroidSecureProvider.KeyHandle identity = provider.generate(
                AndroidSecureProvider.Purpose.DEVICE_IDENTITY, scope + "-identity");
        try {
            AndroidSecureProvider.SecretHandle leftSecret = provider.keyAgree(left,
                    provider.publicKeyX963(right));
            AndroidSecureProvider.SecretHandle rightSecret = provider.keyAgree(right,
                    provider.publicKeyX963(left));
            try {
                assertTrue(leftSecret.matchesForTest(rightSecret));
                assertThrows(IllegalArgumentException.class,
                        () -> provider.keyAgree(identity, provider.publicKeyX963(right)));
                byte[] invalid = provider.publicKeyX963(right);
                Arrays.fill(invalid, (byte) 0);
                assertThrows(Exception.class, () -> provider.keyAgree(left, invalid));
            } finally {
                leftSecret.close();
                rightSecret.close();
            }
        } finally {
            provider.destroy(left);
            provider.destroy(right);
            provider.destroy(identity);
        }
    }

    @Test public void hkdfAndAeadAreExactAndTamperFailsClosed() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        AndroidSecureProvider.SecretHandle input = provider.secretForTest(new byte[32]);
        AndroidSecureProvider.SecretHandle key = provider.hkdfSha256(
                input, "salt".getBytes(), "flynes-pair-aead-v1".getBytes(), 32);
        try {
            byte[] nonce = new byte[12];
            nonce[11] = 1;
            byte[] aad = "bound-transcript".getBytes();
            byte[] sealed = provider.aeadSeal(key, nonce, aad, "payload".getBytes());
            assertArrayEquals("payload".getBytes(), provider.aeadOpen(key, nonce, aad, sealed));
            sealed[0] ^= 1;
            assertThrows(Exception.class, () -> provider.aeadOpen(key, nonce, aad, sealed));
            sealed[0] ^= 1;
            byte[] wrongAad = aad.clone();
            wrongAad[0] ^= 1;
            assertThrows(Exception.class, () -> provider.aeadOpen(key, nonce, wrongAad, sealed));
            byte[] wrongNonce = nonce.clone();
            wrongNonce[0] ^= 1;
            assertThrows(Exception.class, () -> provider.aeadOpen(key, wrongNonce, aad, sealed));
        } finally {
            input.close();
            key.close();
        }
    }

    @Test public void hmacUsesOpaqueSecretAndExactInput() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        byte[] secretBytes = new byte[32];
        for (int i = 0; i < secretBytes.length; i++) secretBytes[i] = (byte) (i + 1);
        byte[] exactInput = "flynes-key-confirm-v1\0exact".getBytes(
                java.nio.charset.StandardCharsets.UTF_8);
        AndroidSecureProvider.SecretHandle secret = provider.secretForTest(secretBytes);
        try {
            Mac independent = Mac.getInstance("HmacSHA256");
            independent.init(new SecretKeySpec(secretBytes, "HmacSHA256"));
            assertArrayEquals(independent.doFinal(exactInput),
                    provider.hmacSha256(secret, exactInput));
            byte[] changed = exactInput.clone();
            changed[changed.length - 1] ^= 1;
            assertFalse(MessageDigest.isEqual(
                    provider.hmacSha256(secret, exactInput),
                    provider.hmacSha256(secret, changed)));
        } finally {
            secret.close();
        }
        assertThrows(IllegalArgumentException.class,
                () -> provider.hmacSha256(secret, exactInput));
    }

    @Test public void secureRecordsAreNoBackupRevisionedAndFailClosed() throws Exception {
        android.content.Context context = ApplicationProvider.getApplicationContext();
        AndroidSecureRecordStore store = new AndroidSecureRecordStore(context);
        String namespace = "test-" + System.nanoTime();
        String key = "identity-root";
        assertThrows(AndroidSecureRecordStore.NotFound.class,
                () -> store.read(namespace, key));
        AndroidSecureRecordStore.Record first = store.compareReplace(
                namespace, key, 0, "first".getBytes());
        assertEquals(1, first.revision);
        assertArrayEquals("first".getBytes(), store.read(namespace, key).bytes);
        assertThrows(AndroidSecureRecordStore.Conflict.class,
                () -> store.compareReplace(namespace, key, 0, "stale".getBytes()));
        AndroidSecureRecordStore.Record second = store.compareReplace(
                namespace, key, 1, "second".getBytes());
        assertEquals(2, second.revision);
        assertTrue(store.fileForTest(namespace, key).getCanonicalPath().startsWith(
                context.getNoBackupFilesDir().getCanonicalPath()
                        + java.io.File.separator));
        assertThrows(AndroidSecureRecordStore.Conflict.class,
                () -> store.remove(namespace, key, 1));
        store.remove(namespace, key, 2);
        assertThrows(AndroidSecureRecordStore.NotFound.class,
                () -> store.read(namespace, key));
    }

    @Test public void tlsMaterialRestoresExactCertificateAndSpki() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        String scope = "tls-" + System.nanoTime();
        AndroidSecureProvider.KeyHandle key = provider.generate(
                AndroidSecureProvider.Purpose.TLS, scope);
        try {
            AndroidSecureProvider.TlsMaterial first = provider.tlsMaterial(key);
            AndroidSecureProvider.KeyHandle restored = provider.open(
                    AndroidSecureProvider.Purpose.TLS, scope);
            AndroidSecureProvider.TlsMaterial second = provider.tlsMaterial(restored);
            assertArrayEquals(first.certificateDer, second.certificateDer);
            assertArrayEquals(first.derSpki, second.derSpki);
            assertArrayEquals(java.security.MessageDigest.getInstance("SHA-256")
                    .digest(first.derSpki), first.derSpkiHash);
            assertArrayEquals(provider.publicKey(restored).getEncoded(), first.derSpki);
            byte[] message = "tls13-certificate-verify".getBytes();
            Signature verifier = Signature.getInstance("SHA256withECDSA");
            verifier.initVerify(provider.publicKey(restored));
            verifier.update(message);
            assertTrue(verifier.verify(provider.signTlsMessage(restored, message)));
        } finally {
            provider.destroy(key);
        }
    }
}
