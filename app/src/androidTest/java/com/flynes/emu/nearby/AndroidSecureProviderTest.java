package com.flynes.emu.nearby;

import static org.junit.Assert.*;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.security.Signature;
import java.security.MessageDigest;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.interfaces.ECPublicKey;
import java.security.spec.ECGenParameterSpec;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

@RunWith(AndroidJUnit4.class)
public final class AndroidSecureProviderTest {
    private static final BigInteger P256_ORDER = new BigInteger(
            "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551", 16);

    private static byte[] unsigned32(BigInteger value) {
        byte[] encoded = value.toByteArray();
        byte[] result = new byte[32];
        int count = Math.min(encoded.length, result.length);
        System.arraycopy(encoded, encoded.length - count, result, result.length - count, count);
        return result;
    }

    private static byte[] peerX963(KeyPair pair) {
        ECPublicKey key = (ECPublicKey) pair.getPublic();
        byte[] result = new byte[65];
        result[0] = 4;
        System.arraycopy(unsigned32(key.getW().getAffineX()), 0, result, 1, 32);
        System.arraycopy(unsigned32(key.getW().getAffineY()), 0, result, 33, 32);
        return result;
    }

    private static final class PeerSignature {
        final byte[] domain = "flynes-pair-signature-v1".getBytes(StandardCharsets.US_ASCII);
        final byte[] preimage = "flynes-pair-signature-v1\0independent-peer".getBytes(
                StandardCharsets.US_ASCII);
        final byte[] publicKey;
        final byte[] digest;
        final byte[] der;
        final byte[] raw;

        PeerSignature() throws Exception {
            KeyPairGenerator generator = KeyPairGenerator.getInstance("EC");
            generator.initialize(new ECGenParameterSpec("secp256r1"));
            KeyPair pair = generator.generateKeyPair();
            publicKey = peerX963(pair);
            digest = MessageDigest.getInstance("SHA-256").digest(preimage);
            // Independent JCA signer hashes the preimage once. No private-key export.
            Signature signer = Signature.getInstance("SHA256withECDSA");
            signer.initSign(pair.getPrivate());
            signer.update(preimage);
            der = signer.sign();
            raw = AndroidSecureProvider.canonicalRawSignature(der);
        }
    }

    @Test public void importedPeerSignatureVerifiesExactPrehashedDigest() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        PeerSignature peer = new PeerSignature();
        assertTrue("independent peer X9.63 signature must verify", provider.verifyPrehashed(
                peer.publicKey, peer.domain, peer.digest, peer.raw));
        byte[] changedDigest = peer.digest.clone();
        changedDigest[0] ^= 1;
        assertFalse(provider.verifyPrehashed(peer.publicKey, peer.domain, changedDigest, peer.raw));
        assertFalse(provider.verifyPrehashed(peer.publicKey, peer.domain,
                MessageDigest.getInstance("SHA-256").digest(peer.digest), peer.raw));
        byte[] otherDomainPreimage = peer.preimage.clone();
        otherDomainPreimage[0] ^= 1;
        assertFalse(provider.verifyPrehashed(peer.publicKey, peer.domain,
                MessageDigest.getInstance("SHA-256").digest(otherDomainPreimage), peer.raw));
        assertFalse(provider.verifyPrehashed(new PeerSignature().publicKey,
                peer.domain, peer.digest, peer.raw));
        byte[] tampered = peer.raw.clone();
        tampered[31] ^= 1;
        assertFalse(provider.verifyPrehashed(peer.publicKey, peer.domain, peer.digest, tampered));
        // Domain is opaque metadata; the already-separated digest carries its binding.
        // Changing metadata alone cannot be distinguished cryptographically here.
        byte[] binaryDomain = new byte[129];
        Arrays.fill(binaryDomain, (byte) 0xff);
        binaryDomain[0] = 0;
        assertTrue(provider.verifyPrehashed(peer.publicKey, binaryDomain, peer.digest, peer.raw));
    }

    @Test public void importedPeerSignatureRejectsNoncanonicalScalarsAndPoints() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        PeerSignature peer = new PeerSignature();
        BigInteger lowS = new BigInteger(1, Arrays.copyOfRange(peer.raw, 32, 64));
        byte[] highS = peer.raw.clone();
        System.arraycopy(unsigned32(P256_ORDER.subtract(lowS)), 0, highS, 32, 32);
        assertFalse(provider.verifyPrehashed(peer.publicKey, peer.domain, peer.digest, highS));
        for (int offset : new int[]{0, 32}) {
            for (BigInteger invalid : new BigInteger[]{BigInteger.ZERO, P256_ORDER,
                    BigInteger.ONE.shiftLeft(256).subtract(BigInteger.ONE)}) {
                byte[] raw = peer.raw.clone();
                System.arraycopy(unsigned32(invalid), 0, raw, offset, 32);
                assertFalse(provider.verifyPrehashed(peer.publicKey, peer.domain, peer.digest, raw));
            }
        }
        byte[] offCurve = new byte[65];
        offCurve[0] = 4;
        assertFalse(provider.verifyPrehashed(offCurve, peer.domain, peer.digest, peer.raw));
        byte[] outOfField = peer.publicKey.clone();
        Arrays.fill(outOfField, 1, 33, (byte) 0xff);
        assertFalse(provider.verifyPrehashed(outOfField, peer.domain, peer.digest, peer.raw));
    }

    @Test public void importedPeerSignatureRejectsMalformedArguments() throws Exception {
        AndroidSecureProvider provider = new AndroidSecureProvider(
                ApplicationProvider.getApplicationContext());
        PeerSignature peer = new PeerSignature();
        byte[] wrongPrefix = peer.publicKey.clone();
        wrongPrefix[0] = 2;
        for (byte[] publicKey : new byte[][]{null, new byte[0], new byte[33],
                Arrays.copyOf(peer.publicKey, 64), Arrays.copyOf(peer.publicKey, 66), wrongPrefix}) {
            assertThrows(IllegalArgumentException.class, () -> provider.verifyPrehashed(
                    publicKey, peer.domain, peer.digest, peer.raw));
        }
        for (byte[] domain : new byte[][]{null, new byte[0]}) {
            assertThrows(IllegalArgumentException.class, () -> provider.verifyPrehashed(
                    peer.publicKey, domain, peer.digest, peer.raw));
        }
        for (byte[] digest : new byte[][]{null, new byte[0], new byte[31], new byte[33]}) {
            assertThrows(IllegalArgumentException.class, () -> provider.verifyPrehashed(
                    peer.publicKey, peer.domain, digest, peer.raw));
        }
        for (byte[] signature : new byte[][]{null, new byte[0], peer.der,
                Arrays.copyOf(peer.raw, 63), Arrays.copyOf(peer.raw, 65)}) {
            assertThrows(IllegalArgumentException.class, () -> provider.verifyPrehashed(
                    peer.publicKey, peer.domain, peer.digest, signature));
        }
    }

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
            assertTrue(provider.verifyPrehashed(provider.publicKeyX963(identity),
                    "flynes-pair-signature-v1".getBytes(StandardCharsets.US_ASCII),
                    digest, signature));
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
