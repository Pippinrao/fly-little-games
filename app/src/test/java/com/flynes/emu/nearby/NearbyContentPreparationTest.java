package com.flynes.emu.nearby;

import static org.junit.Assert.*;

import com.flynes.emu.catalog.*;
import com.flynes.emu.launch.ExactRomLoader;
import org.junit.Test;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.util.zip.*;

public final class NearbyContentPreparationTest {
    @Test public void exactSecondSourceLoadsOffCallerAndDuplicateSharesOperation() throws Exception {
        try (Fixture f = new Fixture()) {
            Gate gate = f.gate("two");
            Thread caller = Thread.currentThread();
            CompletionStage<Integer> first = f.preparation.prepareAndSelect(f.ref("two"));
            gate.awaitEntered();
            CompletionStage<Integer> duplicate = f.preparation.prepareAndSelect(f.ref("two"));
            assertSame(first, duplicate);
            assertFalse(first.toCompletableFuture().isDone());
            assertNotSame(caller, f.readThread.get());
            assertEquals(List.of("two"), f.opened);
            gate.release.countDown();
            assertEquals(0, result(first));
            assertEquals(List.of("two"), f.bridge.installedSources(f));
            assertArrayEquals(Fixture.PAYLOAD, f.bridge.installedBytes);
            assertTrue(f.catalog.recentEntries().isEmpty());
            assertEquals(0, f.catalog.canonicalEntries().get(0).playCount());
            CompletionStage<Integer> next = f.preparation.prepareAndSelect(f.ref("two"));
            assertNotSame("a terminal result cannot be reused in a later round", first, next);
            assertEquals(0, result(next));
            assertEquals(List.of("two", "two"), f.opened);
        }
    }

    @Test public void oneRunningOneReplaceablePendingAndLateReadCannotInstall() throws Exception {
        try (Fixture f = new Fixture()) {
            Gate gate = f.gate("one");
            CompletionStage<Integer> a = f.preparation.prepareAndSelect(f.ref("one"));
            gate.awaitEntered();
            CompletionStage<Integer> b = f.preparation.prepareAndSelect(f.ref("two"));
            CompletionStage<Integer> c = f.preparation.prepareAndSelect(f.ref("three"));
            assertTrue(a.toCompletableFuture().isCancelled());
            assertTrue(b.toCompletableFuture().isCancelled());
            assertFalse(c.toCompletableFuture().isDone());
            assertEquals(List.of("one"), f.opened);
            assertEquals(1, f.active.get());
            assertEquals(List.of(1L, 2L), f.bridge.cancelled);
            gate.release.countDown();
            assertEquals(0, result(c));
            assertEquals(List.of("one", "three"), f.opened);
            assertEquals(List.of("three"), f.bridge.installedSources(f));
            assertEquals(1, f.maximumActive.get());
        }
    }

    @Test public void closeDoesNotWaitForUninterruptibleReadAndCannotInstallLate() throws Exception {
        Fixture f = new Fixture();
        try {
            Gate gate = f.gate("one");
            CompletionStage<Integer> operation = f.preparation.prepareAndSelect(f.ref("one"));
            gate.awaitEntered();
            CompletableFuture<Void> closed = CompletableFuture.runAsync(f.preparation::close);
            closed.get(3, TimeUnit.SECONDS);
            assertTrue(operation.toCompletableFuture().isCancelled());
            assertEquals(1, f.active.get());
            gate.release.countDown();
            assertTrue(gate.closed.await(3, TimeUnit.SECONDS));
            f.readThread.get().join(3000);
            assertFalse("closed worker exits after blocked storage returns", f.readThread.get().isAlive());
            assertTrue(f.bridge.installed.isEmpty());
            assertEquals(-18, result(f.preparation.prepareAndSelect(f.ref("two"))));
        } finally { f.close(); }
    }

    @Test public void cancellationContinuationMayCloseFromAnotherThread() throws Exception {
        try (Fixture f = new Fixture()) {
            Gate gate = f.gate("one");
            CompletionStage<Integer> operation = f.preparation.prepareAndSelect(f.ref("one"));
            gate.awaitEntered();
            CompletableFuture<Void> continuation = operation.handle((value, failure) -> {
                try { CompletableFuture.runAsync(f.preparation::close).get(3, TimeUnit.SECONDS); }
                catch (Exception e) { throw new CompletionException(e); }
                return (Void) null;
            }).toCompletableFuture();
            f.preparation.cancel();
            continuation.get(3, TimeUnit.SECONDS);
            assertTrue(operation.toCompletableFuture().isCancelled());
            assertTrue(f.bridge.installed.isEmpty());
        }
    }

    @Test public void callerFutureCancellationRevokesTicketAndRejectsLateRead() throws Exception {
        try (Fixture f = new Fixture()) {
            Gate gate = f.gate("one");
            CompletionStage<Integer> operation = f.preparation.prepareAndSelect(f.ref("one"));
            gate.awaitEntered();
            assertTrue(operation.toCompletableFuture().cancel(false));
            assertTrue(operation.toCompletableFuture().isCancelled());
            assertEquals("caller cancellation invalidates native authority immediately",
                    List.of(1L), f.bridge.cancelled);
            CompletionStage<Integer> next = f.preparation.prepareAndSelect(f.ref("two"));
            assertEquals(List.of("one"), f.opened);
            gate.release.countDown();
            assertEquals(0, result(next));
            assertEquals("drain the cancelled read before another source may install",
                    List.of("one", "two"), f.opened);
            assertEquals(List.of("two"), f.bridge.installedSources(f));
            assertEquals(1, f.maximumActive.get());
        }
    }

    @Test public void successContinuationRunsOutsideAdmissionAndBridgeCall() throws Exception {
        try (Fixture f = new Fixture()) {
            Gate gate = f.gate("one");
            CompletionStage<Integer> operation = f.preparation.prepareAndSelect(f.ref("one"));
            gate.awaitEntered();
            CompletableFuture<Integer> continuation = operation.thenApply(value -> {
                assertFalse(f.bridge.inCall.get());
                try { CompletableFuture.runAsync(f.preparation::close).get(3, TimeUnit.SECONDS); }
                catch (Exception e) { throw new CompletionException(e); }
                return value;
            }).toCompletableFuture();
            gate.release.countDown();
            assertEquals(Integer.valueOf(0), continuation.get(3, TimeUnit.SECONDS));
        }
    }

    @Test public void permissionRevocationAndNewBatchPreventInstallation() throws Exception {
        for (boolean revoke : new boolean[]{true, false}) {
            try (Fixture f = new Fixture()) {
                Gate gate = f.gate("one");
                CompletionStage<Integer> operation = f.preparation.prepareAndSelect(f.ref("one"));
                gate.awaitEntered();
                if (revoke) f.allowed = false;
                else f.provider.query(0);
                gate.release.countDown();
                assertTrue("no success after authority loss", result(operation) < 0);
                assertTrue(f.bridge.installed.isEmpty());
                assertEquals(List.of(1L), f.bridge.cancelled);
            }
        }
    }

    @Test public void changedSameVariantIdSourceCannotInstallOrFallBackByHash() throws Exception {
        try (Fixture f = new Fixture()) {
            Gate gate = f.gate("one");
            CompletionStage<Integer> operation = f.preparation.prepareAndSelect(f.ref("one"));
            gate.awaitEntered();
            f.replacementSource = "replacement";
            f.publish();
            gate.release.countDown();
            assertTrue(result(operation) < 0);
            assertTrue(f.bridge.installed.isEmpty());
            assertEquals(List.of("one"), f.opened);
        }
    }

    @Test public void invalidRefAndRejectedNativeAdmissionNeverRead() throws Exception {
        try (Fixture f = new Fixture()) {
            assertEquals(-1, result(f.preparation.prepareAndSelect(new byte[15])));
            assertEquals(-1, result(f.preparation.prepareAndSelect(null)));
            assertEquals(-3, result(f.preparation.prepareAndSelect(new byte[16])));
            f.bridge.beginResult = -6;
            assertEquals(-6, result(f.preparation.prepareAndSelect(f.ref("one"))));
            assertTrue(f.opened.isEmpty());
        }
    }

    @Test public void queuedAcceptanceIsNotTerminalSelectSuccess() throws Exception {
        for (int nativeResult : new int[]{1, -3, -4}) {
            try (Fixture f = new Fixture()) {
                f.bridge.installResult = nativeResult;
                int result = result(f.preparation.prepareAndSelect(f.ref("one")));
                assertEquals(nativeResult == 1 ? -4 : nativeResult, result);
                assertEquals(List.of(1L), f.bridge.cancelled);
            }
        }
    }

    // Added by the native/owner integrator after the queue-saturation revocation RED.
    @Test public void invalidReplacementRevokesPreviouslySuccessfulTicket() throws Exception {
        for (byte[] invalid : new byte[][] { new byte[15], new byte[16] }) {
            try (Fixture f = new Fixture()) {
                assertEquals(0, result(f.preparation.prepareAndSelect(f.ref("one"))));
                assertEquals(invalid.length == 15 ? -1 : -3,
                        result(f.preparation.prepareAndSelect(invalid)));
                assertEquals(List.of(1L), f.bridge.cancelled);
                assertEquals(List.of("one"), f.opened);
                assertEquals("no replacement install after invalid source", List.of("one"), f.bridge.installedSources(f));
            }
        }
    }

    @Test public void queueRejectedReplacementStillRevokesPreviouslySuccessfulTicket() throws Exception {
        for (int rejected : new int[] {-7, -11}) {
            try (Fixture f = new Fixture()) {
                assertEquals(0, result(f.preparation.prepareAndSelect(f.ref("one"))));
                f.bridge.beginResult = rejected;
                assertEquals(rejected, result(f.preparation.prepareAndSelect(f.ref("two"))));
                assertEquals("revocation precedes new begin and cannot be skipped on rejection", List.of(1L), f.bridge.cancelled);
                assertEquals(List.of("one"), f.opened);
                assertEquals(List.of("one"), f.bridge.installedSources(f));
            }
        }
    }

    @Test public void realHashAndExactZipFailuresNeverInstall() throws Exception {
        for (boolean zip : new boolean[]{false, true}) {
            try (Fixture f = new Fixture(zip)) {
                if (zip) {
                    f.entryName = "missing.nes";
                    f.publish();
                    f.captureRefs();
                } else f.physical = new byte[]{1, 2, 3};
                assertEquals(-12, result(f.preparation.prepareAndSelect(f.ref("one"))));
                assertTrue(f.bridge.installed.isEmpty());
            }
        }
    }

    private static int result(CompletionStage<Integer> result) throws Exception {
        return result.toCompletableFuture().get(5, TimeUnit.SECONDS);
    }

    private static final class Gate {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);
        final CountDownLatch closed = new CountDownLatch(1);
        void awaitEntered() throws Exception {
            assertTrue("real exact-loader stream must reach read gate", entered.await(3, TimeUnit.SECONDS));
        }
        void awaitRead() throws IOException {
            entered.countDown();
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15);
            // Deliberately model SAF that does not cooperate with interruption.
            while (release.getCount() != 0) {
                try {
                    if (!release.await(Math.max(1, deadline - System.nanoTime()), TimeUnit.NANOSECONDS))
                        throw new IOException("test gate timeout");
                } catch (InterruptedException ignored) { /* cancellation must fence late data */ }
            }
        }
    }

    private static final class RecordingBridge implements NearbyContentPreparation.Bridge {
        final List<Long> cancelled = Collections.synchronizedList(new ArrayList<>());
        final List<byte[]> installed = Collections.synchronizedList(new ArrayList<>());
        final AtomicBoolean inCall = new AtomicBoolean();
        int beginResult;
        int installResult;
        long nextTicket;
        byte[] installedBytes;
        @Override public NearbyContentPreparation.Admission begin(byte[] ref) {
            return new NearbyContentPreparation.Admission(beginResult, beginResult == 0 ? ++nextTicket : 0);
        }
        @Override public int install(long ticket, byte[] ref, byte[] hash, byte[] bytes) {
            inCall.set(true);
            try { installed.add(ref.clone()); installedBytes = bytes.clone(); return installResult; }
            finally { inCall.set(false); }
        }
        @Override public void cancel(long ticket) { cancelled.add(ticket); }
        List<String> installedSources(Fixture f) {
            List<String> sources = new ArrayList<>();
            for (byte[] ref : installed) for (Map.Entry<String, byte[]> entry : f.refs.entrySet())
                if (Arrays.equals(ref, entry.getValue())) sources.add(entry.getKey());
            return sources;
        }
    }

    private static final class Fixture implements AutoCloseable {
        static final byte[] PAYLOAD = "exact-preparation-payload".getBytes(StandardCharsets.UTF_8);
        final GameCatalog catalog = new GameCatalog();
        final Map<String, byte[]> refs = new LinkedHashMap<>();
        final Map<String, Gate> gates = new ConcurrentHashMap<>();
        final List<String> opened = Collections.synchronizedList(new ArrayList<>());
        final AtomicInteger active = new AtomicInteger();
        final AtomicInteger maximumActive = new AtomicInteger();
        final AtomicReference<Thread> readThread = new AtomicReference<>();
        final RecordingBridge bridge = new RecordingBridge();
        final NearbyContentProvider provider;
        final NearbyContentPreparation preparation;
        final boolean zipped;
        volatile boolean allowed = true;
        volatile byte[] physical;
        String entryName = "target.nes";
        String replacementSource;
        Fixture() throws Exception { this(false); }
        Fixture(boolean zipped) throws Exception {
            this.zipped = zipped;
            physical = zipped ? zip(entryName, PAYLOAD) : PAYLOAD.clone();
            publish();
            NearbyExactContentLoader.ReadAccessValidator access = (source, uri) -> {
                if (!allowed) throw new SecurityException("revoked");
            };
            provider = new NearbyContentProvider(catalog, access);
            captureRefs();
            NearbyExactContentLoader loader = new NearbyExactContentLoader(catalog,
                    new ExactRomLoader((source, uri) -> {
                        opened.add(source);
                        readThread.set(Thread.currentThread());
                        maximumActive.accumulateAndGet(active.incrementAndGet(), Math::max);
                        Gate gate = gates.get(source);
                        return new ByteArrayInputStream(physical) {
                            boolean firstRead = true;
                            @Override public synchronized int read(byte[] bytes, int offset, int length) {
                                if (firstRead) {
                                    firstRead = false;
                                    if (gate != null) try { gate.awaitRead(); }
                                    catch (IOException failure) { throw new IllegalStateException(failure); }
                                }
                                return super.read(bytes, offset, length);
                            }
                            @Override public void close() {
                                active.decrementAndGet();
                                if (gate != null) gate.closed.countDown();
                            }
                        };
                    }), access);
            preparation = new NearbyContentPreparation(provider, loader, bridge);
        }
        Gate gate(String source) { Gate gate = new Gate(); gates.put(source, gate); return gate; }
        byte[] ref(String source) { return refs.get(source).clone(); }
        void captureRefs() {
            refs.clear();
            for (int i = 0;; ++i) {
                byte[] record = provider.query(i);
                if (record == null) break;
                byte[] ref = Arrays.copyOfRange(record, 4, 20);
                // Match the provider's actual catalog order, never assume source sorting.
                int cursor = 0;
                for (GameCatalogEntry entry : catalog.canonicalEntries()) for (GameVariant variant : entry.variants()) {
                    if (variant.isLaunchable() && cursor++ == i) refs.put(variant.sourceId(), ref);
                }
            }
        }
        void publish() {
            List<PhysicalPackage> packages = new ArrayList<>();
            for (String id : List.of("one", "two", "three")) {
                String sourceId = id.equals("one") && replacementSource != null ? replacementSource : id;
                RomSource source = new RomSource(sourceId, RomSource.Type.SAF_TREE,
                        "content://fixture/tree/" + sourceId, RomSource.PermissionState.GRANTED);
                RomHashes hashes = hashes(PAYLOAD, physical);
                RomVariant variant = new RomVariant("variant-" + id,
                        new CanonicalGame("game", "Fixture", null, List.of()),
                        zipped ? entryName : null, RomFormat.INES, CompatibilityDecision.playableNes(),
                        hashes, RomAnalysis.basic(0),
                        zipped ? ZipEntryIdentity.fromRawName(entryName.getBytes(StandardCharsets.UTF_8), 0) : null,
                        zipped ? ZipNameEncoding.UTF8_EFS : null);
                packages.add(new PhysicalPackage("package-" + id, source,
                        "content://fixture/document/" + sourceId, zipped ? "fixture.zip" : "fixture.nes",
                        zipped ? PackageFormat.ZIP : PackageFormat.RAW, hashes.physicalPackageSha256(), List.of(variant)));
            }
            catalog.applyScanResult(ScanResult.success(packages, List.of()));
        }
        @Override public void close() throws Exception {
            preparation.close();
            for (Gate gate : gates.values()) gate.release.countDown();
            for (Gate gate : gates.values()) if (gate.entered.getCount() == 0)
                assertTrue("running test stream closes", gate.closed.await(3, TimeUnit.SECONDS));
            Thread reader = readThread.get();
            if (reader != null) {
                reader.join(3000);
                assertFalse("test releases the one IO worker", reader.isAlive());
            }
            provider.close();
        }
    }

    private static RomHashes hashes(byte[] payload, byte[] physical) {
        CRC32 crc = new CRC32(); crc.update(payload);
        return new RomHashes(digest("SHA-1", payload), digest("SHA-256", payload),
                digest("SHA-256", physical), String.format(Locale.ROOT, "%08X", crc.getValue()));
    }
    private static String digest(String algorithm, byte[] bytes) {
        try {
            StringBuilder hex = new StringBuilder();
            for (byte value : MessageDigest.getInstance(algorithm).digest(bytes))
                hex.append(String.format(Locale.ROOT, "%02X", value & 255));
            return hex.toString();
        } catch (Exception failure) { throw new AssertionError(failure); }
    }
    private static byte[] zip(String name, byte[] payload) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry(name)); zip.write(payload); zip.closeEntry();
        }
        return bytes.toByteArray();
    }
}
