package com.flynes.emu.nearby;

import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Locale;
import java.util.Objects;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Encrypted, revisioned nearby records rooted strictly in no-backup storage. */
public final class AndroidSecureRecordStore {
    private static final String STORE = "AndroidKeyStore";
    private static final String MASTER_ALIAS = "flynes.nearby.v2.secure-record-master";
    private static final byte[] MAGIC = new byte[]{'F','L','Y','N','S','R','2',0};
    private static final int MAX_RECORD = 1024 * 1024;

    public static final class Record {
        public final long revision;
        public final byte[] bytes;
        Record(long revision, byte[] bytes) {
            this.revision = revision;
            this.bytes = bytes;
        }
    }

    public static class StoreFailure extends Exception {
        StoreFailure(String message) { super(message); }
        StoreFailure(String message, Throwable cause) { super(message, cause); }
    }
    public static final class NotFound extends StoreFailure {
        NotFound() { super("secure record not found"); }
    }
    public static final class Conflict extends StoreFailure {
        Conflict() { super("secure record revision conflict"); }
    }
    public static final class Corrupt extends StoreFailure {
        Corrupt(Throwable cause) { super("secure record corrupt", cause); }
    }
    public static final class Unavailable extends StoreFailure {
        Unavailable(String message, Throwable cause) { super(message, cause); }
    }

    private final File directory;
    private final SecretKey master;

    public AndroidSecureRecordStore(Context context) throws StoreFailure {
        Objects.requireNonNull(context, "context");
        directory = new File(context.getNoBackupFilesDir(), "nearby-secure-v2");
        if (!directory.exists() && !directory.mkdirs()) {
            throw new Unavailable("cannot create no-backup secure directory", null);
        }
        try {
            KeyStore keyStore = KeyStore.getInstance(STORE);
            keyStore.load(null);
            if (!keyStore.containsAlias(MASTER_ALIAS)) {
                File[] history = directory.listFiles();
                if (history != null && history.length != 0) {
                    throw new Unavailable(
                            "secure master missing while durable history exists", null);
                }
                KeyGenerator generator = KeyGenerator.getInstance(
                        KeyProperties.KEY_ALGORITHM_AES, STORE);
                generator.init(new KeyGenParameterSpec.Builder(MASTER_ALIAS,
                        KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                        .setRandomizedEncryptionRequired(true)
                        .setUserAuthenticationRequired(false)
                        .build());
                generator.generateKey();
            }
            KeyStore.SecretKeyEntry entry = (KeyStore.SecretKeyEntry)
                    keyStore.getEntry(MASTER_ALIAS, null);
            if (entry == null || entry.getSecretKey().getEncoded() != null) {
                throw new Unavailable("non-exportable secure master unavailable", null);
            }
            master = entry.getSecretKey();
        } catch (StoreFailure failure) {
            throw failure;
        } catch (Exception failure) {
            throw new Unavailable("AndroidKeyStore unavailable", failure);
        }
    }

    public synchronized Record read(String namespace, String recordKey) throws StoreFailure {
        File file = file(namespace, recordKey);
        if (!file.isFile()) throw new NotFound();
        try {
            byte[] encoded = new AtomicFile(file).readFully();
            DataInputStream input = new DataInputStream(new ByteArrayInputStream(encoded));
            byte[] magic = new byte[MAGIC.length];
            input.readFully(magic);
            long revision = input.readLong();
            byte[] nonce = new byte[12];
            input.readFully(nonce);
            int ciphertextSize = input.readInt();
            if (!Arrays.equals(magic, MAGIC) || revision <= 0 || ciphertextSize < 16
                    || ciphertextSize > MAX_RECORD + 16
                    || ciphertextSize != input.available()) {
                throw new java.io.IOException("invalid secure record envelope");
            }
            byte[] ciphertext = new byte[ciphertextSize];
            input.readFully(ciphertext);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, master, new GCMParameterSpec(128, nonce));
            cipher.updateAAD(aad(namespace, recordKey, revision));
            byte[] plaintext = cipher.doFinal(ciphertext);
            if (plaintext.length > MAX_RECORD) throw new java.io.IOException("record too large");
            return new Record(revision, plaintext);
        } catch (Exception failure) {
            throw new Corrupt(failure);
        }
    }

    public synchronized Record compareReplace(String namespace, String recordKey,
                                               long expectedRevision, byte[] value)
            throws StoreFailure {
        validateName(namespace);
        validateName(recordKey);
        if (expectedRevision < 0 || value == null || value.length > MAX_RECORD) {
            throw new IllegalArgumentException("bounded value and revision required");
        }
        long current = 0;
        File target = file(namespace, recordKey);
        if (target.isFile()) current = read(namespace, recordKey).revision;
        if (current != expectedRevision) throw new Conflict();
        if (current == Long.MAX_VALUE) {
            throw new IllegalArgumentException("revision exhausted");
        }
        long revision = current + 1;
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            // AndroidKeyStore owns nonce generation when randomized encryption
            // is required; caller-chosen IVs are deliberately rejected.
            cipher.init(Cipher.ENCRYPT_MODE, master);
            byte[] nonce = cipher.getIV();
            if (nonce == null || nonce.length != 12) {
                throw new IllegalStateException("AndroidKeyStore returned an invalid GCM nonce");
            }
            cipher.updateAAD(aad(namespace, recordKey, revision));
            byte[] ciphertext = cipher.doFinal(value);
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            output.write(MAGIC);
            output.writeLong(revision);
            output.write(nonce);
            output.writeInt(ciphertext.length);
            output.write(ciphertext);
            output.flush();
            AtomicFile atomic = new AtomicFile(target);
            FileOutputStream stream = atomic.startWrite();
            try {
                stream.write(bytes.toByteArray());
                stream.getFD().sync();
                atomic.finishWrite(stream);
            } catch (Exception failure) {
                atomic.failWrite(stream);
                throw failure;
            }
            return new Record(revision, value.clone());
        } catch (Exception failure) {
            throw new Unavailable("secure record write failed", failure);
        }
    }

    public synchronized void remove(String namespace, String recordKey,
                                    long expectedRevision) throws StoreFailure {
        Record current = read(namespace, recordKey);
        if (current.revision != expectedRevision) throw new Conflict();
        AtomicFile atomic = new AtomicFile(file(namespace, recordKey));
        atomic.delete();
    }

    File fileForTest(String namespace, String recordKey) throws Exception {
        return file(namespace, recordKey);
    }

    private File file(String namespace, String recordKey) throws StoreFailure {
        validateName(namespace);
        validateName(recordKey);
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            digest.update(namespace.getBytes(StandardCharsets.UTF_8));
            digest.update((byte) 0);
            digest.update(recordKey.getBytes(StandardCharsets.UTF_8));
            StringBuilder name = new StringBuilder(64);
            for (byte value : digest.digest()) {
                name.append(String.format(Locale.ROOT, "%02x", value));
            }
            return new File(directory, name + ".record");
        } catch (Exception failure) {
            throw new Unavailable("SHA-256 unavailable", failure);
        }
    }

    private static byte[] aad(String namespace, String recordKey, long revision)
            throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream output = new DataOutputStream(bytes);
        byte[] namespaceBytes = namespace.getBytes(StandardCharsets.UTF_8);
        byte[] keyBytes = recordKey.getBytes(StandardCharsets.UTF_8);
        output.writeInt(namespaceBytes.length);
        output.write(namespaceBytes);
        output.writeInt(keyBytes.length);
        output.write(keyBytes);
        output.writeLong(revision);
        output.flush();
        return bytes.toByteArray();
    }

    private static void validateName(String value) {
        if (value == null || value.isEmpty()
                || value.getBytes(StandardCharsets.UTF_8).length > 256) {
            throw new IllegalArgumentException("bounded namespace/key required");
        }
    }
}
