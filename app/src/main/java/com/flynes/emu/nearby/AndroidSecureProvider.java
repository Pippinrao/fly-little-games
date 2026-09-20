package com.flynes.emu.nearby;

import android.content.Context;
import android.os.Build;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.AlgorithmParameters;
import java.security.GeneralSecurityException;
import java.security.InvalidKeyException;
import java.security.KeyFactory;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.Signature;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.ECFieldFp;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPoint;
import java.security.spec.ECPublicKeySpec;
import java.security.spec.InvalidKeySpecException;
import java.security.interfaces.ECPublicKey;
import java.util.Arrays;
import java.util.Locale;
import java.util.Objects;
import java.util.Date;

import javax.security.auth.x500.X500Principal;

import javax.crypto.KeyAgreement;
import javax.crypto.Cipher;
import javax.crypto.Mac;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * AndroidKeyStore-backed nearby key boundary.
 *
 * <p>Private keys are represented only by purpose/scope-bound opaque handles.
 * There is no private-key import/export path and no software fallback. A caller
 * must distinguish an unsupported device from a missing key instead of silently
 * regenerating identity material.
 */
public final class AndroidSecureProvider {
    private static final String STORE = "AndroidKeyStore";
    private static final byte[] P256_ORDER = hex(
            "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551");
    private static final BigInteger ORDER = new BigInteger(1, P256_ORDER);
    private static final BigInteger HALF_ORDER = ORDER.shiftRight(1);

    public enum Purpose {
        DEVICE_IDENTITY,
        SESSION_SIGNING,
        PAIR_ECDH,
        TLS
    }

    public static final class KeyHandle {
        private final String alias;
        private final Purpose purpose;
        private final String scopeHash;
        private final PrivateKey privateKey;
        private final PublicKey publicKey;

        private KeyHandle(String alias, Purpose purpose, String scopeHash,
                          PrivateKey privateKey, PublicKey publicKey) {
            this.alias = alias;
            this.purpose = purpose;
            this.scopeHash = scopeHash;
            this.privateKey = privateKey;
            this.publicKey = publicKey;
        }

        /** Test-visible proof that the returned key is an opaque platform key. */
        public PrivateKey privateKey() { return privateKey; }
    }

    public static final class SecretHandle implements AutoCloseable {
        private byte[] secret;

        private SecretHandle(byte[] secret) { this.secret = secret; }

        boolean matchesForTest(SecretHandle other) {
            return secret != null && other != null && other.secret != null
                    && MessageDigest.isEqual(secret, other.secret);
        }

        @Override public void close() {
            if (secret != null) {
                Arrays.fill(secret, (byte) 0);
                secret = null;
            }
        }
    }

    public static final class TlsMaterial {
        public final byte[] certificateDer;
        public final byte[] derSpki;
        public final byte[] derSpkiHash;

        private TlsMaterial(byte[] certificateDer, byte[] derSpki,
                            byte[] derSpkiHash) {
            this.certificateDer = certificateDer;
            this.derSpki = derSpki;
            this.derSpkiHash = derSpkiHash;
        }
    }

    private final KeyStore keyStore;

    public AndroidSecureProvider(Context context) throws Exception {
        Objects.requireNonNull(context, "context");
        keyStore = KeyStore.getInstance(STORE);
        keyStore.load(null);
    }

    public synchronized KeyHandle generate(Purpose purpose, String scope) throws Exception {
        Objects.requireNonNull(purpose, "purpose");
        String scopeHash = scopeHash(scope);
        String alias = alias(purpose, scopeHash);
        if (keyStore.containsAlias(alias)) {
            throw new IllegalStateException("key already exists for exact purpose and scope");
        }
        int purposes;
        if (purpose == Purpose.PAIR_ECDH) {
            if (Build.VERSION.SDK_INT < 31) {
                throw new UnsupportedOperationException(
                        "AndroidKeyStore ECDH requires API 31; software fallback is forbidden");
            }
            purposes = KeyProperties.PURPOSE_AGREE_KEY;
        } else {
            purposes = KeyProperties.PURPOSE_SIGN | KeyProperties.PURPOSE_VERIFY;
        }
        KeyGenParameterSpec.Builder spec = new KeyGenParameterSpec.Builder(alias, purposes)
                .setAlgorithmParameterSpec(new ECGenParameterSpec("secp256r1"))
                .setUserAuthenticationRequired(false);
        if (purpose != Purpose.PAIR_ECDH) {
            spec.setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256);
        }
        if (purpose == Purpose.TLS) {
            long now = System.currentTimeMillis();
            spec.setCertificateSubject(new X500Principal("CN=FlyNES Nearby"))
                    .setCertificateSerialNumber(new BigInteger(160,
                            new SecureRandom()).abs().add(BigInteger.ONE))
                    .setCertificateNotBefore(new Date(now - 60_000L))
                    .setCertificateNotAfter(new Date(now + 31_536_000_000L));
        }
        KeyPairGenerator generator = KeyPairGenerator.getInstance(
                KeyProperties.KEY_ALGORITHM_EC, STORE);
        generator.initialize(spec.build());
        generator.generateKeyPair();
        return open(purpose, scope);
    }

    public synchronized KeyHandle open(Purpose purpose, String scope) throws Exception {
        Objects.requireNonNull(purpose, "purpose");
        String scopeHash = scopeHash(scope);
        String alias = alias(purpose, scopeHash);
        KeyStore.Entry entry = keyStore.getEntry(alias, null);
        if (!(entry instanceof KeyStore.PrivateKeyEntry)) {
            throw new java.io.FileNotFoundException("key not found for exact purpose and scope");
        }
        KeyStore.PrivateKeyEntry privateEntry = (KeyStore.PrivateKeyEntry) entry;
        return new KeyHandle(alias, purpose, scopeHash, privateEntry.getPrivateKey(),
                privateEntry.getCertificate().getPublicKey());
    }

    public PublicKey publicKey(KeyHandle handle) {
        requireLive(handle);
        return handle.publicKey;
    }

    public byte[] publicKeyX963(KeyHandle handle) {
        requireLive(handle);
        if (!(handle.publicKey instanceof ECPublicKey)) {
            throw new IllegalArgumentException("handle is not a P-256 public key");
        }
        ECPublicKey key = (ECPublicKey) handle.publicKey;
        byte[] output = new byte[65];
        output[0] = 4;
        copyUnsigned32(key.getW().getAffineX(), output, 1);
        copyUnsigned32(key.getW().getAffineY(), output, 33);
        return output;
    }

    public byte[] signPrehashed(KeyHandle handle, String domain, byte[] digest)
            throws Exception {
        requireLive(handle);
        if (handle.purpose != Purpose.DEVICE_IDENTITY
                && handle.purpose != Purpose.SESSION_SIGNING) {
            throw new IllegalArgumentException("key purpose cannot sign pair/session digests");
        }
        if (domain == null || domain.isEmpty() || domain.length() > 128
                || digest == null || digest.length != 32) {
            throw new IllegalArgumentException("exact domain and SHA-256 digest are required");
        }
        Signature signer = Signature.getInstance("NONEwithECDSA");
        signer.initSign(handle.privateKey);
        signer.update(digest);
        return canonicalRawSignature(signer.sign());
    }

    /**
     * Verifies an imported P-256 peer signature over the exact shared digest.
     * Domain is opaque metadata: shared already included it in the digest.
     * Malformed argument shapes throw; invalid peer keys/signatures return false.
     * Provider failures propagate, including SignatureException: after importing
     * the key and constructing canonical DER locally, it is not a parse verdict
     * for untrusted signature bytes and must not be relabeled AUTH_FAILED.
     */
    public boolean verifyPrehashed(byte[] publicKeyX963, byte[] domain,
                                   byte[] digest, byte[] signature)
            throws GeneralSecurityException {
        if (publicKeyX963 == null || publicKeyX963.length != 65 || publicKeyX963[0] != 4
                || domain == null || domain.length == 0
                || digest == null || digest.length != 32
                || signature == null || signature.length != 64) {
            throw new IllegalArgumentException("X9.63 key, domain, SHA-256 digest and raw64 required");
        }
        if (!isCanonicalLowS(signature)) return false;

        AlgorithmParameters parameters = AlgorithmParameters.getInstance("EC");
        parameters.init(new ECGenParameterSpec("secp256r1"));
        ECParameterSpec p256 = parameters.getParameterSpec(ECParameterSpec.class);
        BigInteger x = new BigInteger(1, Arrays.copyOfRange(publicKeyX963, 1, 33));
        BigInteger y = new BigInteger(1, Arrays.copyOfRange(publicKeyX963, 33, 65));
        BigInteger prime = ((ECFieldFp) p256.getCurve().getField()).getP();
        // Do not let a provider reduce noncanonical coordinates modulo the field.
        if (x.compareTo(prime) >= 0 || y.compareTo(prime) >= 0) return false;
        KeyFactory factory = KeyFactory.getInstance("EC");
        Signature verifier = Signature.getInstance("NONEwithECDSA");
        try {
            PublicKey peer = factory.generatePublic(new ECPublicKeySpec(new ECPoint(x, y), p256));
            verifier.initVerify(peer);
        } catch (InvalidKeySpecException | InvalidKeyException invalidPeerKey) {
            return false;
        }
        verifier.update(digest);
        return verifier.verify(rawToDer(signature));
    }

    public byte[] signTlsMessage(KeyHandle handle, byte[] message) throws Exception {
        requireLive(handle);
        if (handle.purpose != Purpose.TLS || message == null || message.length == 0
                || message.length > 64 * 1024) {
            throw new IllegalArgumentException("TLS handle and bounded message required");
        }
        Signature signer = Signature.getInstance("SHA256withECDSA");
        signer.initSign(handle.privateKey);
        signer.update(message);
        return signer.sign();
    }

    public synchronized TlsMaterial tlsMaterial(KeyHandle handle) throws Exception {
        requireLive(handle);
        if (handle.purpose != Purpose.TLS) {
            throw new IllegalArgumentException("TLS key handle required");
        }
        byte[] certificate = keyStore.getCertificate(handle.alias).getEncoded();
        byte[] spki = handle.publicKey.getEncoded();
        if (certificate.length == 0 || spki.length != 91) {
            throw new IllegalStateException("canonical P-256 TLS material unavailable");
        }
        byte[] hash = MessageDigest.getInstance("SHA-256").digest(spki);
        return new TlsMaterial(certificate, spki, hash);
    }

    public SecretHandle keyAgree(KeyHandle handle, byte[] peerX963) throws Exception {
        requireLive(handle);
        if (handle.purpose != Purpose.PAIR_ECDH) {
            throw new IllegalArgumentException("only PAIR_ECDH handles may agree keys");
        }
        if (peerX963 == null || peerX963.length != 65 || peerX963[0] != 4) {
            throw new IllegalArgumentException("canonical P-256 X9.63 point required");
        }
        AlgorithmParameters parameters = AlgorithmParameters.getInstance("EC");
        parameters.init(new ECGenParameterSpec("secp256r1"));
        ECParameterSpec p256 = parameters.getParameterSpec(ECParameterSpec.class);
        ECPoint point = new ECPoint(
                new BigInteger(1, Arrays.copyOfRange(peerX963, 1, 33)),
                new BigInteger(1, Arrays.copyOfRange(peerX963, 33, 65)));
        PublicKey peer = KeyFactory.getInstance("EC").generatePublic(
                new ECPublicKeySpec(point, p256));
        KeyAgreement agreement = KeyAgreement.getInstance("ECDH", STORE);
        agreement.init(handle.privateKey);
        agreement.doPhase(peer, true);
        byte[] secret = agreement.generateSecret();
        if (secret.length == 0) throw new IllegalStateException("empty ECDH secret");
        return new SecretHandle(secret);
    }

    public byte[] random(int byteCount) {
        if (byteCount <= 0 || byteCount > 4096) {
            throw new IllegalArgumentException("bounded random request required");
        }
        byte[] output = new byte[byteCount];
        new SecureRandom().nextBytes(output);
        return output;
    }

    public SecretHandle hkdfSha256(SecretHandle input, byte[] salt, byte[] info,
                                   int outputSize) throws Exception {
        requireSecret(input);
        if (salt == null || salt.length > 1024 || info == null || info.length == 0
                || info.length > 1024 || outputSize <= 0 || outputSize > 255 * 32) {
            throw new IllegalArgumentException("bounded HKDF inputs required");
        }
        Mac mac = Mac.getInstance("HmacSHA256");
        byte[] actualSalt = salt.length == 0 ? new byte[32] : salt;
        mac.init(new SecretKeySpec(actualSalt, "HmacSHA256"));
        byte[] prk = mac.doFinal(input.secret);
        byte[] output = new byte[outputSize];
        byte[] previous = new byte[0];
        int offset = 0;
        for (int counter = 1; offset < output.length; counter++) {
            mac.init(new SecretKeySpec(prk, "HmacSHA256"));
            mac.update(previous);
            mac.update(info);
            mac.update((byte) counter);
            previous = mac.doFinal();
            int count = Math.min(previous.length, output.length - offset);
            System.arraycopy(previous, 0, output, offset, count);
            offset += count;
        }
        Arrays.fill(prk, (byte) 0);
        Arrays.fill(previous, (byte) 0);
        return new SecretHandle(output);
    }

    /** Computes one exact HMAC without exporting or exposing the secret bytes. */
    public byte[] hmacSha256(SecretHandle key, byte[] exactInput) throws Exception {
        requireSecret(key);
        if (exactInput == null || exactInput.length == 0 || exactInput.length > 4096) {
            throw new IllegalArgumentException("bounded exact HMAC input required");
        }
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(key.secret, "HmacSHA256"));
        return mac.doFinal(exactInput);
    }

    public byte[] aeadSeal(SecretHandle key, byte[] nonce, byte[] aad, byte[] plaintext)
            throws Exception {
        return aead(Cipher.ENCRYPT_MODE, key, nonce, aad, plaintext);
    }

    public byte[] aeadOpen(SecretHandle key, byte[] nonce, byte[] aad, byte[] ciphertext)
            throws Exception {
        return aead(Cipher.DECRYPT_MODE, key, nonce, aad, ciphertext);
    }

    private byte[] aead(int mode, SecretHandle key, byte[] nonce, byte[] aad, byte[] input)
            throws Exception {
        requireSecret(key);
        if (key.secret.length != 32 || nonce == null || nonce.length != 12
                || aad == null || aad.length > 4096 || input == null
                || input.length > 1024 * 1024) {
            throw new IllegalArgumentException("AES-256-GCM requires exact bounded inputs");
        }
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(mode, new SecretKeySpec(key.secret, "AES"),
                new GCMParameterSpec(128, nonce));
        cipher.updateAAD(aad);
        return cipher.doFinal(input);
    }

    SecretHandle secretForTest(byte[] value) {
        if (value == null || value.length == 0) {
            throw new IllegalArgumentException("secret required");
        }
        return new SecretHandle(value.clone());
    }

    public synchronized void destroy(KeyHandle handle) throws Exception {
        requireLive(handle);
        keyStore.deleteEntry(handle.alias);
    }

    private void requireLive(KeyHandle handle) {
        if (handle == null || handle.alias == null || handle.scopeHash == null) {
            throw new IllegalArgumentException("live key handle required");
        }
    }

    private static void requireSecret(SecretHandle handle) {
        if (handle == null || handle.secret == null || handle.secret.length == 0) {
            throw new IllegalArgumentException("live secret handle required");
        }
    }

    private static String alias(Purpose purpose, String scopeHash) {
        return "flynes.nearby.v2." + purpose.name().toLowerCase(Locale.ROOT)
                + "." + scopeHash;
    }

    private static String scopeHash(String scope) throws Exception {
        if (scope == null || scope.isEmpty() || scope.length() > 512) {
            throw new IllegalArgumentException("bounded nonempty scope required");
        }
        byte[] hash = MessageDigest.getInstance("SHA-256")
                .digest(scope.getBytes(StandardCharsets.UTF_8));
        StringBuilder output = new StringBuilder(hash.length * 2);
        for (byte value : hash) output.append(String.format(Locale.ROOT, "%02x", value));
        return output.toString();
    }

    static boolean isCanonicalLowS(byte[] raw) {
        if (raw == null || raw.length != 64) return false;
        BigInteger r = new BigInteger(1, Arrays.copyOfRange(raw, 0, 32));
        BigInteger s = new BigInteger(1, Arrays.copyOfRange(raw, 32, 64));
        return r.signum() > 0 && r.compareTo(ORDER) < 0
                && s.signum() > 0 && s.compareTo(HALF_ORDER) <= 0;
    }

    static byte[] canonicalRawSignature(byte[] der) {
        BigInteger[] values = decodeDer(der);
        BigInteger s = values[1].compareTo(HALF_ORDER) > 0
                ? ORDER.subtract(values[1]) : values[1];
        byte[] raw = new byte[64];
        copyUnsigned32(values[0], raw, 0);
        copyUnsigned32(s, raw, 32);
        if (!isCanonicalLowS(raw)) throw new IllegalArgumentException("invalid ECDSA signature");
        return raw;
    }

    static byte[] rawToDer(byte[] raw) {
        if (!isCanonicalLowS(raw)) throw new IllegalArgumentException("canonical signature required");
        byte[] r = derInteger(Arrays.copyOfRange(raw, 0, 32));
        byte[] s = derInteger(Arrays.copyOfRange(raw, 32, 64));
        byte[] output = new byte[2 + r.length + s.length];
        output[0] = 0x30;
        output[1] = (byte) (r.length + s.length);
        System.arraycopy(r, 0, output, 2, r.length);
        System.arraycopy(s, 0, output, 2 + r.length, s.length);
        return output;
    }

    private static BigInteger[] decodeDer(byte[] der) {
        if (der == null || der.length < 8 || der.length > 72 || der[0] != 0x30
                || (der[1] & 0xff) != der.length - 2 || der[2] != 0x02) {
            throw new IllegalArgumentException("canonical DER ECDSA signature required");
        }
        int rLength = der[3] & 0xff;
        int sTag = 4 + rLength;
        if (rLength == 0 || sTag + 2 > der.length || der[sTag] != 0x02) {
            throw new IllegalArgumentException("malformed ECDSA r");
        }
        int sLength = der[sTag + 1] & 0xff;
        if (sLength == 0 || sTag + 2 + sLength != der.length) {
            throw new IllegalArgumentException("malformed ECDSA s");
        }
        BigInteger r = new BigInteger(Arrays.copyOfRange(der, 4, sTag));
        BigInteger s = new BigInteger(Arrays.copyOfRange(der, sTag + 2, der.length));
        if (r.signum() <= 0 || s.signum() <= 0 || r.compareTo(ORDER) >= 0
                || s.compareTo(ORDER) >= 0) {
            throw new IllegalArgumentException("ECDSA scalar out of range");
        }
        return new BigInteger[]{r, s};
    }

    private static byte[] derInteger(byte[] scalar) {
        int first = 0;
        while (first < scalar.length - 1 && scalar[first] == 0) first++;
        boolean prefix = (scalar[first] & 0x80) != 0;
        int valueLength = scalar.length - first;
        byte[] output = new byte[2 + valueLength + (prefix ? 1 : 0)];
        output[0] = 0x02;
        output[1] = (byte) (output.length - 2);
        System.arraycopy(scalar, first, output, prefix ? 3 : 2, valueLength);
        return output;
    }

    private static void copyUnsigned32(BigInteger value, byte[] output, int offset) {
        byte[] source = value.toByteArray();
        int start = source.length == 33 && source[0] == 0 ? 1 : 0;
        int length = source.length - start;
        if (length > 32) throw new IllegalArgumentException("P-256 scalar overflow");
        Arrays.fill(output, offset, offset + 32, (byte) 0);
        System.arraycopy(source, start, output, offset + 32 - length, length);
    }

    private static byte[] hex(String value) {
        byte[] output = new byte[value.length() / 2];
        for (int i = 0; i < output.length; i++) {
            output[i] = (byte) Integer.parseInt(value.substring(i * 2, i * 2 + 2), 16);
        }
        return output;
    }
}
