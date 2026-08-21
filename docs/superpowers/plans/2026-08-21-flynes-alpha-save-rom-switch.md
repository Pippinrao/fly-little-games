# FlyNES Alpha Per-ROM Saves and Safe ROM Switching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Isolate all persistent state by ROM identity, make writes crash-safe, migrate the legacy autosave, and switch ROMs without destroying the current session on failure.

**Architecture:** `RomIdentity` uses the core SHA-1 as the shared key. `SaveRepository` owns atomic app-private files; candidate-core loading validates a new ROM before the current core is unloaded, and cached battery data crosses the C ABI explicitly.

**Tech Stack:** Java 17, Android `AtomicFile`, C++17/JNI, FLYNST1 state header, JUnit, AndroidX instrumentation, host fault-injection tests.

---

## File map

- Create `data/RomIdentity.java`, `data/RomInfo.java`.
- Create `save/SaveStore.java`, `save/SaveRepository.java`, `save/SaveRecord.java`, `save/LegacySaveMigrator.java`.
- Modify C ABI/core/JNI for ROM info and cached battery import/export.
- Extend `CoreFacade` and `EmulationSession` with candidate-core switching.
- Remove global autosave/hash and static `NesCore.sPendingRom` handoff.

### Task 1: Expose stable ROM identity through JNI

**Files:**
- Modify: `app/src/main/cpp/nes_jni.cpp`
- Modify: `app/src/main/java/com/flynes/emu/NesCore.java`
- Create: `app/src/main/java/com/flynes/emu/data/RomIdentity.java`
- Create: `app/src/main/java/com/flynes/emu/data/RomInfo.java`
- Test: `app/src/androidTest/java/com/flynes/emu/RomIdentityTest.java`

- [ ] **Step 1: Write a failing built-in ROM identity test**

```java
@Test public void builtinRomUsesCoreSha1() throws Exception {
    NesCore core = new NesCore();
    assertTrue(core.create());
    Context context = ApplicationProvider.getApplicationContext();
    byte[] rom;
    try (InputStream in = context.getAssets().open("roms/from_below.nes")) {
        rom = in.readAllBytes();
    }
    assertTrue(core.loadRom(rom, null) >= 0);
    assertEquals("77C42676DB38D384C1D6B00090ADBC820BF70AB0",
            core.romInfo().identity().sha1());
    core.destroy();
}
```

- [ ] **Step 2: Run and observe missing romInfo**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.RomIdentityTest --console=plain`

Expected: compile FAIL for missing `romInfo()`.

- [ ] **Step 3: Add immutable identity and JNI metadata copy**

```java
public record RomIdentity(String sha1) {
    public RomIdentity {
        if (sha1 == null || !sha1.matches("[0-9A-Fa-f]{40}"))
            throw new IllegalArgumentException("sha1 must be 40 hex characters");
        sha1 = sha1.toUpperCase(java.util.Locale.ROOT);
    }
    public String directoryName() { return sha1; }
}
```

Add a JNI method that calls `nes_get_rom_info`, copies SHA-1, CRC32, mapper, PRG/CHR, region, title, and publisher into `RomInfo`. Never calculate Adler32 in Java.

- [ ] **Step 4: Run the identity test**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.RomIdentityTest --console=plain`

Expected: PASS with the known fixture SHA-1.

- [ ] **Step 5: Commit identity support**

```powershell
git add app/src/main/cpp/nes_jni.cpp app/src/main/java/com/flynes/emu/NesCore.java app/src/main/java/com/flynes/emu/data app/src/androidTest/java/com/flynes/emu/RomIdentityTest.java
git commit -m "feat: expose core-backed ROM identity"
```

### Task 2: Implement atomic per-ROM SaveRepository

**Files:**
- Create: `app/src/main/java/com/flynes/emu/save/SaveStore.java`
- Create: `app/src/main/java/com/flynes/emu/save/SaveRepository.java`
- Create: `app/src/main/java/com/flynes/emu/save/SaveRecord.java`
- Test: `app/src/androidTest/java/com/flynes/emu/save/SaveRepositoryTest.java`

- [ ] **Step 1: Write isolation and rollback tests**

```java
private static final RomIdentity ID_A =
        new RomIdentity("1111111111111111111111111111111111111111");
private static final RomIdentity ID_B =
        new RomIdentity("2222222222222222222222222222222222222222");

@Test public void savesAreIsolatedBySha1() throws Exception {
    Context context = ApplicationProvider.getApplicationContext();
    File tempDir = new File(context.getCacheDir(), "save-isolation-" + System.nanoTime());
    SaveRepository repo = SaveRepository.forTest(context, tempDir);
    repo.writeAutosave(ID_A, new byte[]{1,2,3}, 100L);
    repo.writeAutosave(ID_B, new byte[]{9,8}, 200L);
    assertArrayEquals(new byte[]{1,2,3}, repo.readAutosave(ID_A).state());
    assertArrayEquals(new byte[]{9,8}, repo.readAutosave(ID_B).state());
}

@Test public void failedWritePreservesPreviousState() throws Exception {
    Context context = ApplicationProvider.getApplicationContext();
    File tempDir = new File(context.getCacheDir(), "save-rollback-" + System.nanoTime());
    SaveRepository repo = SaveRepository.forTest(context, tempDir);
    repo.writeAutosave(ID_A, new byte[]{1,2,3}, 100L);
    repo.setFaultInjector(bytes -> { throw new IOException("disk full"); });
    assertThrows(IOException.class, () -> repo.writeAutosave(ID_A, new byte[]{4}, 200L));
    assertArrayEquals(new byte[]{1,2,3}, repo.readAutosave(ID_A).state());
}
```

- [ ] **Step 2: Run and observe missing repository**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.save.SaveRepositoryTest --console=plain`

Expected: compile FAIL.

- [ ] **Step 3: Implement AtomicFile transactions**

Use this session-facing seam and make `SaveRepository` its production implementation:

```java
public interface SaveStore {
    void writeAutosave(RomIdentity id, byte[] state, long savedAt) throws IOException;
    java.util.Optional<SaveRecord> readAutosave(RomIdentity id) throws IOException;
    void writeBattery(RomIdentity id, byte[] battery) throws IOException;
    byte[] readBattery(RomIdentity id) throws IOException;
}
```

Use `files/saves/<SHA1>/autosave.nst`, `.bak`, `battery.sav`, and `metadata.json`. `writeAutosave` must call `AtomicFile.startWrite()`, write and `FileDescriptor.sync()`, then `finishWrite()`; any exception calls `failWrite()`. `readAutosave` validates the FLYNST1 header through `StateHeaderReader` before returning `SaveRecord`.

- [ ] **Step 4: Run repository tests**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.package=com.flynes.emu.save --console=plain`

Expected: isolation and rollback PASS.

- [ ] **Step 5: Commit atomic storage**

```powershell
git add app/src/main/java/com/flynes/emu/save app/src/androidTest/java/com/flynes/emu/save
git commit -m "feat: add atomic per-ROM save repository"
```

### Task 3: Migrate the legacy global autosave safely

**Files:**
- Create: `app/src/main/java/com/flynes/emu/save/StateHeaderReader.java`
- Create: `app/src/main/java/com/flynes/emu/save/LegacySaveMigrator.java`
- Test: `app/src/test/java/com/flynes/emu/save/StateHeaderReaderTest.java`
- Test: `app/src/androidTest/java/com/flynes/emu/save/LegacySaveMigratorTest.java`

- [ ] **Step 1: Test SHA-1 extraction from the fixed FLYNST1 layout**

```java
@Test public void extractsSha1AtOffset28() {
    byte[] state = new byte[81];
    System.arraycopy("FLYNST1\0".getBytes(UTF_8), 0, state, 0, 8);
    state[8] = 1;
    byte[] sha = "77C42676DB38D384C1D6B00090ADBC820BF70AB0".getBytes(US_ASCII);
    System.arraycopy(sha, 0, state, 28, sha.length);
    assertEquals(new RomIdentity(new String(sha, US_ASCII)), StateHeaderReader.identity(state));
}
```

- [ ] **Step 2: Run and observe missing reader**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*StateHeaderReaderTest' --console=plain`

Expected: compile FAIL.

- [ ] **Step 3: Implement one-time copy migration**

`StateHeaderReader` validates magic, version, length, SHA-1 syntax, and payload CRC. `LegacySaveMigrator.migrate()` copies—not moves—valid `files/autosave.nst` into the identified SaveRepository directory, writes preference `legacy_autosave_migration=success`, and leaves invalid/unknown files untouched with status `needs_manual_recovery`.

- [ ] **Step 4: Run reader and migration tests**

Run: `./gradlew.bat :app:testDebugUnitTest :app:connectedDebugAndroidTest --console=plain`

Expected: valid file copied once; invalid file retained; second migration is idempotent.

- [ ] **Step 5: Commit migration**

```powershell
git add app/src/main/java/com/flynes/emu/save app/src/test/java/com/flynes/emu/save app/src/androidTest/java/com/flynes/emu/save
git commit -m "feat: migrate legacy autosave without data loss"
```

### Task 4: Add cached battery import/export to the C ABI

**Files:**
- Modify: `core/include/nes/nes.h`
- Modify: `core/src/nes_core.cpp`
- Modify: `core/tests/test_core.cpp`
- Modify: `app/src/main/cpp/nes_jni.cpp`
- Modify: `app/src/main/java/com/flynes/emu/NesCore.java`

- [ ] **Step 1: Add failing battery cache tests**

```cpp
const uint8_t battery[] = {0x11,0x22,0x33,0x44};
CHECK(nes_battery_set_cached(nes, battery, sizeof(battery)) == NES_OK);
uint8_t out[8]{}; size_t written=0, needed=0;
CHECK(nes_battery_get_cached(nes, out, sizeof(out), &written, &needed) == NES_OK);
CHECK(written == sizeof(battery));
CHECK(std::memcmp(out, battery, sizeof(battery)) == 0);
```

- [ ] **Step 2: Run and confirm missing ABI**

Run: `.\core\build\host\Release\nes_core_test.exe`

Expected: compile FAIL for the two functions.

- [ ] **Step 3: Add explicit cached battery functions**

```c
NES_API int nes_battery_set_cached(nes_t* nes, const uint8_t* data, size_t len);
NES_API int nes_battery_get_cached(const nes_t* nes, uint8_t* out, size_t cap,
                                   size_t* written, size_t* needed);
```

Set before ROM load so `LOAD_BATTERY` consumes the correct game bytes. After `nes_unload`, export the SAVE_BATTERY callback cache before destroy. Add matching JNI `setCachedBattery(byte[])`, `unload()`, and `cachedBattery()` methods with size-query allocation.

- [ ] **Step 4: Run host and Android tests**

Run:

```powershell
.\core\build\host\Release\nes_core_test.exe
.\gradlew.bat :app:assembleDebug --console=plain
```

Expected: host battery round trip PASS; JNI links on arm64-v8a/x86_64.

- [ ] **Step 5: Commit battery persistence ABI**

```powershell
git add core/include/nes/nes.h core/src/nes_core.cpp core/tests/test_core.cpp app/src/main/cpp/nes_jni.cpp app/src/main/java/com/flynes/emu/NesCore.java
git commit -m "feat: import and export cached battery data"
```

### Task 5: Switch ROMs with a candidate core transaction

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/session/CoreFacade.java`
- Modify: `app/src/main/java/com/flynes/emu/session/EmulationSession.java`
- Create: `app/src/main/java/com/flynes/emu/session/CoreFactory.java`
- Test: `app/src/test/java/com/flynes/emu/session/RomSwitchTransactionTest.java`

- [ ] **Step 1: Write success and rollback tests**

```java
private static final RomIdentity ID_A =
        new RomIdentity("1111111111111111111111111111111111111111");
private static final RomIdentity ID_B =
        new RomIdentity("2222222222222222222222222222222222222222");
private static final byte[] ROM_B = {'N','E','S',0x1A,0,0,0,0};
private final RecordingSaveStore saveStore = new RecordingSaveStore();

private EmulationSession testSession(CoreFacade current, CoreFactory factory) {
    return EmulationSession.forSwitchTest(current, factory, saveStore, Runnable::run);
}

@Test public void failedCandidateKeepsOldCoreRunning() throws Exception {
    FakeCore oldCore = FakeCore.loaded(ID_A);
    FakeCore badCore = FakeCore.failingLoad(-200);
    EmulationSession session = testSession(oldCore, () -> badCore);
    SessionResult result = session.switchRom(ROM_B).get();
    assertFalse(result.isSuccess());
    assertSame(oldCore, session.coreForTest());
    assertEquals(SessionState.RUNNING, session.state());
    assertFalse(oldCore.destroyed);
    assertTrue(badCore.destroyed);
}

@Test public void successfulCandidateSavesOldBeforeSwap() throws Exception {
    FakeCore oldCore = FakeCore.loaded(ID_A);
    EmulationSession session = testSession(oldCore, () -> FakeCore.loaded(ID_B));
    assertTrue(session.switchRom(ROM_B).get().isSuccess());
    assertEquals(ID_B, session.currentRom());
    assertEquals(java.util.List.of(ID_A), saveStore.autosaved);
    assertTrue(oldCore.destroyed);
}

private static final class RecordingSaveStore implements SaveStore {
    final java.util.List<RomIdentity> autosaved = new java.util.ArrayList<>();
    @Override public void writeAutosave(RomIdentity id, byte[] state, long savedAt) {
        autosaved.add(id);
    }
    @Override public java.util.Optional<SaveRecord> readAutosave(RomIdentity id) {
        return java.util.Optional.empty();
    }
    @Override public void writeBattery(RomIdentity id, byte[] battery) { }
    @Override public byte[] readBattery(RomIdentity id) { return new byte[0]; }
}

private static final class FakeCore implements CoreFacade {
    private final RomIdentity identity;
    private final int loadResult;
    boolean destroyed;
    private FakeCore(RomIdentity identity, int loadResult) {
        this.identity = identity;
        this.loadResult = loadResult;
    }
    static FakeCore loaded(RomIdentity identity) { return new FakeCore(identity, 0); }
    static FakeCore failingLoad(int error) { return new FakeCore(ID_B, error); }
    @Override public int create() { return 0; }
    @Override public int loadRom(byte[] rom) { return loadResult; }
    @Override public RomIdentity romIdentity() { return identity; }
    @Override public void setInput(int mask) { }
    @Override public int runOneFrame() { return 800; }
    @Override public java.nio.ByteBuffer audioBuffer() {
        return java.nio.ByteBuffer.allocateDirect(4096);
    }
    @Override public byte[] saveState() { return new byte[]{1}; }
    @Override public int loadState(byte[] state) { return 0; }
    @Override public void unload() { }
    @Override public void destroy() { destroyed = true; }
}
```

- [ ] **Step 2: Run and observe destructive current behavior**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*RomSwitchTransactionTest' --console=plain`

Expected: FAIL because candidate switching is not implemented.

- [ ] **Step 3: Implement the six-step transaction**

Extend `CoreFacade` with `RomIdentity romIdentity()`, `void unload()`, battery import/export, and the audio buffer contract introduced by the frame/audio plan. Add package-private `forSwitchTest` and `coreForTest` seams in the same package. `switchRom` transitions RUNNING/PAUSED→SWITCHING, snapshots old state, creates/configures candidate, injects candidate battery, loads candidate, reads RomIdentity, restores candidate autosave, starts candidate, atomically swaps, unloads/exports old battery, and destroys old. Any candidate failure destroys only candidate and resumes old with its previous state.

- [ ] **Step 4: Run switch and session tests**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*session*' --console=plain`

Expected: success and rollback tests PASS; no illegal state transition.

- [ ] **Step 5: Commit safe switching**

```powershell
git add app/src/main/java/com/flynes/emu/session app/src/test/java/com/flynes/emu/session
git commit -m "fix: validate ROMs in a candidate core before switching"
```

### Task 6: Remove global handoffs and run fault-injection acceptance

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/MainActivity.java`
- Modify: `app/src/main/java/com/flynes/emu/NesCore.java`
- Modify: `app/src/main/java/com/flynes/emu/GameLibraryActivity.java`
- Create: `app/src/androidTest/java/com/flynes/emu/save/MultiRomSaveFlowTest.java`
- Create: `docs/acceptance/alpha/save-rom-switch.md`

- [ ] **Step 1: Replace static ROM bytes with a ROM entry ID result**

Return `EXTRA_GAME_ID` from the library. `MainActivity` asks `GameRepository` to load bytes, then calls `session.switchRom(bytes)`; delete `NesCore.sPendingRom`, `AUTOSAVE_NAME`, `KEY_AUTOSAVE_ROM_HASH`, Java Adler32, and direct file methods.

- [ ] **Step 2: Add A→B→A and process-restart instrumentation**

The test loads fixture A, saves marker A, switches to fixture B and saves marker B, switches back to A, force-stops/relaunches through `UiAutomation`, and asserts each repository directory retains its own bytes.

- [ ] **Step 3: Inject failure at every AtomicFile phase**

Run the test with failure before write, mid-write, before sync, and before finish. Each restart must restore a complete old or complete new state; truncated data is a failure.

- [ ] **Step 4: Record evidence**

`save-rom-switch.md` records build SHA, fixture IDs, 100 A/B loops, force-stop result, every fault point, legacy migration status, and cached SRAM hash.

- [ ] **Step 5: Commit integration evidence**

```powershell
git add app/src/main/java/com/flynes/emu/MainActivity.java app/src/main/java/com/flynes/emu/NesCore.java app/src/main/java/com/flynes/emu/GameLibraryActivity.java app/src/androidTest/java/com/flynes/emu/save docs/acceptance/alpha/save-rom-switch.md
git commit -m "test: verify per-ROM state survives switching and failures"
```
