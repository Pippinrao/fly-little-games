package com.flynes.emu.nearby;

import static org.junit.Assert.*;

import android.content.Context;
import android.content.ContextWrapper;
import android.system.ErrnoException;
import android.system.OsConstants;
import android.util.AtomicFile;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import javax.crypto.SecretKey;

/** Real AndroidKeyStore tests; all records and aliases are isolated from product data. */
@RunWith(AndroidJUnit4.class)
public class AndroidSecureRecordStoreTest {
    private static final String NAMESPACE = "records";
    private static final String KEY = "identity";

    @Test public void concurrentInstancesHaveExactlyOneCasWinner() throws Exception {
        Fixture fixture = new Fixture();
        Gate gate = new Gate();
        AtomicBoolean pause = new AtomicBoolean(false);
        AndroidSecureRecordStore.Dependencies dependencies =
                new AndroidSecureRecordStore.Dependencies() {
            @Override AtomicFile atomicFile(File file) {
                return new AtomicFile(file) {
                    @Override public byte[] readFully() throws IOException {
                        byte[] bytes = super.readFully();
                        if (pause.compareAndSet(true, false)) gate.pauseIo();
                        return bytes;
                    }
                };
            }
        };
        AndroidSecureRecordStore first = fixture.store(dependencies);
        AndroidSecureRecordStore second = fixture.store(new AndroidSecureRecordStore.Dependencies());
        first.compareReplace(NAMESPACE, KEY, 0, bytes("seed"));
        pause.set(true);
        Worker<Boolean> a = new Worker<>(() -> replace(first, "first"));
        Worker<Boolean> b = new Worker<>(() -> replace(second, "second"));
        try {
            a.start();
            assertTrue("first reader reached gate", gate.entered.await(10, TimeUnit.SECONDS));
            b.start();
            b.awaitDoneOrStoreMonitor();
        } finally {
            gate.release.countDown();
            a.join();
            b.join();
        }
        boolean firstWon = a.result();
        boolean secondWon = b.result();
        assertEquals("one successful CAS for an expected revision", 1,
                (firstWon ? 1 : 0) + (secondWon ? 1 : 0));
        AndroidSecureRecordStore.Record record = second.read(NAMESPACE, KEY);
        assertEquals(2, record.revision);
        assertArrayEquals(bytes(firstWon ? "first" : "second"), record.bytes);
    }

    @Test public void concurrentConstructorsCreateMasterOnlyOnce() throws Exception {
        Fixture fixture = new Fixture();
        Gate gate = new Gate();
        AtomicBoolean pause = new AtomicBoolean(true);
        AtomicInteger creates = new AtomicInteger();
        AndroidSecureRecordStore.Dependencies dependencies =
                new AndroidSecureRecordStore.Dependencies() {
            @Override SecretKey loadMaster(String alias) throws Exception {
                SecretKey key = super.loadMaster(alias);
                if (key == null && pause.compareAndSet(true, false)) gate.pauseIo();
                return key;
            }
            @Override void createMaster(String alias) throws Exception {
                creates.incrementAndGet();
                super.createMaster(alias);
            }
        };
        Worker<AndroidSecureRecordStore> a = new Worker<>(() -> fixture.store(dependencies));
        Worker<AndroidSecureRecordStore> b = new Worker<>(() -> fixture.store(dependencies));
        try {
            a.start();
            assertTrue("first missing master reached gate", gate.entered.await(10, TimeUnit.SECONDS));
            b.start();
            b.awaitDoneOrStoreMonitor();
        } finally {
            gate.release.countDown();
            a.join();
            b.join();
        }
        AndroidSecureRecordStore first = a.result();
        AndroidSecureRecordStore second = b.result();
        assertEquals("master creation must be serialized", 1, creates.get());
        first.compareReplace(NAMESPACE, KEY, 0, bytes("shared"));
        assertArrayEquals(bytes("shared"), second.read(NAMESPACE, KEY).bytes);
    }

    @Test public void failedDeleteReportsUnavailableAndPreservesRecord() throws Exception {
        Fixture fixture = new Fixture();
        AndroidSecureRecordStore store = fixture.store(new AndroidSecureRecordStore.Dependencies() {
            @Override AtomicFile atomicFile(File file) {
                return new AtomicFile(file) {
                    @Override public void delete() { /* Simulate a refused filesystem deletion. */ }
                };
            }
        });
        store.compareReplace(NAMESPACE, KEY, 0, bytes("retained"));
        assertThrows(AndroidSecureRecordStore.Unavailable.class,
                () -> store.remove(NAMESPACE, KEY, 1));
        assertEquals(1, store.read(NAMESPACE, KEY).revision);
        assertArrayEquals(bytes("retained"), store.read(NAMESPACE, KEY).bytes);
    }

    @Test public void inaccessibleDeleteVerificationReportsUnavailable() throws Exception {
        assertDeleteStatFailure(OsConstants.EACCES);
    }

    @Test public void ioErrorDuringDeleteVerificationReportsUnavailable() throws Exception {
        assertDeleteStatFailure(OsConstants.EIO);
    }

    private static void assertDeleteStatFailure(int errno) throws Exception {
        Fixture fixture = new Fixture();
        AndroidSecureRecordStore store = fixture.store(new AndroidSecureRecordStore.Dependencies() {
            @Override AtomicFile atomicFile(File file) {
                return new AtomicFile(file) {
                    @Override public void delete() { /* The record remains on disk. */ }
                };
            }
            @Override void stat(File file) throws ErrnoException {
                throw new ErrnoException("lstat", errno);
            }
        });
        store.compareReplace(NAMESPACE, KEY, 0, bytes("retained"));
        AndroidSecureRecordStore.Unavailable failure = assertThrows(
                AndroidSecureRecordStore.Unavailable.class,
                () -> store.remove(NAMESPACE, KEY, 1));
        assertTrue(failure.getCause() instanceof ErrnoException);
        assertEquals(errno, ((ErrnoException) failure.getCause()).errno);
        assertEquals(1, store.read(NAMESPACE, KEY).revision);
        assertArrayEquals(bytes("retained"), store.read(NAMESPACE, KEY).bytes);
    }

    @Test public void twoInstancesPreserveRevisionNoBackupAndCorruptionRules() throws Exception {
        Fixture fixture = new Fixture();
        AndroidSecureRecordStore first = fixture.store(new AndroidSecureRecordStore.Dependencies());
        AndroidSecureRecordStore second = fixture.store(new AndroidSecureRecordStore.Dependencies());
        assertThrows(AndroidSecureRecordStore.NotFound.class, () -> first.read(NAMESPACE, KEY));
        assertEquals(1, first.compareReplace(NAMESPACE, KEY, 0, bytes("one")).revision);
        assertArrayEquals(bytes("one"), second.read(NAMESPACE, KEY).bytes);
        assertThrows(AndroidSecureRecordStore.Conflict.class,
                () -> second.compareReplace(NAMESPACE, KEY, 0, bytes("stale")));
        assertEquals(2, second.compareReplace(NAMESPACE, KEY, 1, bytes("two")).revision);
        assertThrows(AndroidSecureRecordStore.Conflict.class,
                () -> first.remove(NAMESPACE, KEY, 1));
        File record = first.fileForTest(NAMESPACE, KEY);
        assertTrue(record.getCanonicalPath().startsWith(
                fixture.directory.getCanonicalPath() + File.separator));
        first.remove(NAMESPACE, KEY, 2);
        assertThrows(AndroidSecureRecordStore.NotFound.class, () -> second.read(NAMESPACE, KEY));
        first.compareReplace(NAMESPACE, KEY, 0, bytes("tamper"));
        byte[] encoded = new AtomicFile(record).readFully();
        encoded[encoded.length - 1] ^= 1;
        try (FileOutputStream output = new FileOutputStream(record)) { output.write(encoded); }
        assertThrows(AndroidSecureRecordStore.Corrupt.class, () -> second.read(NAMESPACE, KEY));
        assertThrows(AndroidSecureRecordStore.Corrupt.class,
                () -> second.compareReplace(NAMESPACE, KEY, 1, bytes("replacement")));
    }

    @Test public void durableHistoryWithoutMasterFailsClosedWithoutCreatingKey() throws Exception {
        Fixture fixture = new Fixture();
        File history = new File(fixture.directory, "nearby-secure-v2");
        assertTrue(history.mkdirs());
        assertTrue(new File(history, "existing.record").createNewFile());
        AtomicInteger creates = new AtomicInteger();
        AndroidSecureRecordStore.Dependencies dependencies =
                new AndroidSecureRecordStore.Dependencies() {
            @Override void createMaster(String alias) throws Exception {
                creates.incrementAndGet();
                super.createMaster(alias);
            }
        };
        assertThrows(AndroidSecureRecordStore.Unavailable.class, () -> fixture.store(dependencies));
        assertEquals(0, creates.get());
    }

    @Test public void unreadableHistoryWithoutMasterFailsClosedWithoutCreatingKey() throws Exception {
        Fixture fixture = new Fixture();
        AtomicInteger creates = new AtomicInteger();
        AndroidSecureRecordStore.Dependencies dependencies =
                new AndroidSecureRecordStore.Dependencies() {
            @Override File[] listHistory(File directory) { return null; }
            @Override SecretKey loadMaster(String alias) { return null; }
            @Override void createMaster(String alias) { creates.incrementAndGet(); }
        };
        assertThrows(AndroidSecureRecordStore.Unavailable.class, () -> fixture.store(dependencies));
        assertEquals("unreadable history must not create a master", 0, creates.get());
    }

    private static boolean replace(AndroidSecureRecordStore store, String value) throws Exception {
        try {
            store.compareReplace(NAMESPACE, KEY, 1, bytes(value));
            return true;
        } catch (AndroidSecureRecordStore.Conflict expected) {
            return false;
        }
    }

    private static byte[] bytes(String value) { return value.getBytes(StandardCharsets.UTF_8); }

    private static final class Fixture {
        final String id = UUID.randomUUID().toString();
        final String alias = "flynes.test.secure-record." + id;
        final File directory;
        final Context context;
        Fixture() {
            Context base = ApplicationProvider.getApplicationContext();
            directory = new File(base.getNoBackupFilesDir(), "secure-record-test-" + id);
            context = new ContextWrapper(base) {
                @Override public File getNoBackupFilesDir() { return directory; }
            };
        }
        AndroidSecureRecordStore store(AndroidSecureRecordStore.Dependencies dependencies)
                throws AndroidSecureRecordStore.StoreFailure {
            return new AndroidSecureRecordStore(context, alias, dependencies);
        }
        // No production paths or aliases are touched. At most six test aliases per suite run
        // are retained, with their tiny UUID-scoped file fixtures, for non-destructive testing.
    }

    private static final class Gate {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);
        void pauseIo() throws IOException {
            entered.countDown();
            try {
                if (!release.await(15, TimeUnit.SECONDS)) throw new IOException("gate timeout");
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new IOException(interrupted);
            }
        }
    }

    private static final class Worker<T> {
        final FutureTask<T> task;
        final Thread thread;
        Worker(Callable<T> callable) {
            task = new FutureTask<>(callable);
            thread = new Thread(task, "secure-store-test");
        }
        void start() { thread.start(); }
        T result() throws Exception { return task.get(10, TimeUnit.SECONDS); }
        void join() throws InterruptedException {
            thread.join(12000);
            if (thread.isAlive()) {
                thread.interrupt();
                thread.join(2000);
            }
            assertFalse("worker must terminate", thread.isAlive());
        }
        void awaitDoneOrStoreMonitor() throws InterruptedException {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
            while (System.nanoTime() < deadline) {
                if (task.isDone()) return;
                if (thread.getState() == Thread.State.BLOCKED) {
                    StackTraceElement[] stack = thread.getStackTrace();
                    if (stack.length > 0 && stack[0].getClassName()
                            .equals(AndroidSecureRecordStore.class.getName())) return;
                }
                Thread.sleep(1);
            }
            fail("second worker neither completed nor blocked at store monitor");
        }
    }
}
