# FlyNES Alpha Frame Publishing and Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish every NTSC/PAL source frame safely, present it efficiently on 60/90/120 Hz displays, and make audio pacing resilient and low-latency.

**Architecture:** The core renders into a back buffer and atomically publishes a completed buffer plus sequence number. Android copies a sequence only once into a direct texture buffer; OpenGL ES performs aspect-correct presentation while `AudioEngine` advances exactly one emulated frame per audio batch.

**Tech Stack:** C++17, C ABI v2 additive structs/functions, JNI direct ByteBuffer, Java 17, OpenGL ES 2.0+, Surface frame-rate API, AudioTrack, JUnit, host core tests.

---

## File map

- Modify `core/include/nes/nes.h`: versioned snapshot struct and copy function.
- Modify `core/src/nes_core.cpp`: two framebuffers, publish mutex/sequence, fractional samples, transactional filter allocation.
- Modify `core/tests/test_core.cpp`: sequence, copy, cadence, and allocation-failure coverage.
- Modify `app/src/main/cpp/nes_jni.cpp`: snapshot JNI and returned filter errors.
- Modify `NesCore.java`: direct snapshot buffer and typed return values.
- Create `video/PublishedFrame.java`, `video/FramePublisher.java`, `video/GlFrameView.java`, `video/FrameRenderer.java`, `video/DisplayModeController.java`.
- Create `audio/AudioSink.java`, `audio/AndroidAudioSink.java`, `audio/AudioEngine.java`.
- Retire `AudioThread.java` and `EmuView.java` after integration.

### Task 1: Add a race-free frame snapshot ABI

**Files:**
- Modify: `core/include/nes/nes.h`
- Modify: `core/src/nes_core.cpp`
- Test: `core/tests/test_core.cpp`

- [ ] **Step 1: Add a failing sequence-and-copy host test**

```cpp
nes_video_snapshot snap{};
snap.struct_size = sizeof(snap);
std::vector<uint8_t> pixels(256u * 240u * 2u);
CHECK(nes_copy_video_frame(nes, pixels.data(), pixels.size(), &snap) >= 0);
const uint64_t before = snap.sequence;
uint32_t frames = 0, samples = 0;
CHECK(nes_run_frames(nes, 1, audio.data(), audio.size(), &frames, &samples) >= 0);
CHECK(nes_copy_video_frame(nes, pixels.data(), pixels.size(), &snap) >= 0);
CHECK(snap.sequence == before + 1);
CHECK(snap.bytes_written == 256u * 240u * 2u);
```

- [ ] **Step 2: Run the host test and confirm the ABI is absent**

Run:

```powershell
cmake --build core/build/host --config Release --target nes_core_test
.\core\build\host\Release\nes_core_test.exe
```

Expected: compile FAIL for undefined `nes_video_snapshot`/`nes_copy_video_frame`.

- [ ] **Step 3: Add the additive ABI and two-buffer publisher**

Append to `nes.h`:

```c
typedef struct nes_video_snapshot {
    uint32_t struct_size;
    uint32_t version;
    uint64_t sequence;
    uint32_t width;
    uint32_t height;
    nes_pixfmt format;
    int32_t pitch;
    size_t bytes_written;
} nes_video_snapshot;

NES_API int nes_copy_video_frame(const nes_t* nes, void* out, size_t cap,
                                 nes_video_snapshot* snapshot);
```

In `nes_ctx`, replace the single framebuffer with two buffers plus:

```cpp
uint8_t* framebuffers[2] = {nullptr, nullptr};
size_t framebuffer_size = 0;
uint32_t write_index = 0;
uint32_t published_index = 1;
uint64_t frame_sequence = 0;
mutable std::mutex frame_mutex;
```

For each emulated frame, render into `framebuffers[write_index]`, then lock, swap `published_index/write_index`, increment `frame_sequence`, and unlock. `nes_copy_video_frame` locks, validates capacity, copies only `published_index`, fills every snapshot field, and unlocks. Allocate both new buffers before changing filter/config; on either allocation failure free only the candidates and preserve the old state.

- [ ] **Step 4: Run host tests and an ASan configuration**

Run:

```powershell
cmake -S core -B core/build/asan -DNES_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"
cmake --build core/build/asan --target nes_core_test
.\core\build\asan\nes_core_test.exe
```

Expected: snapshot test PASS and no sanitizer report.

- [ ] **Step 5: Commit the ABI publisher**

```powershell
git add core/include/nes/nes.h core/src/nes_core.cpp core/tests/test_core.cpp
git commit -m "feat: publish race-free video frame snapshots"
```

### Task 2: Produce exact long-term audio sample counts

**Files:**
- Modify: `core/src/nes_core.cpp`
- Test: `core/tests/test_core.cpp`

- [ ] **Step 1: Add a 30-minute cadence test**

```cpp
uint64_t total = 0;
constexpr uint32_t frame_count = 60u * 30u * 60u;
for (uint32_t i = 0; i < frame_count; ++i)
    total += samples_for_next_frame(48000, 60.0988, remainder);
const double expected = static_cast<double>(frame_count) * 48000.0 / 60.0988;
CHECK(std::abs(static_cast<double>(total) - expected) < 1.0);
```

Place the helper in an internal header compiled by the test so the test does not execute 108,000 full emulator frames.

- [ ] **Step 2: Verify the current floor calculation fails**

Run: `cmake --build core/build/host --config Release --target nes_core_test && .\core\build\host\Release\nes_core_test.exe`

Expected: FAIL with approximately 3 seconds/hour drift.

- [ ] **Step 3: Add a fractional accumulator**

```cpp
uint32_t samples_for_next_frame(uint32_t rate, double fps, double& remainder) {
    const double exact = static_cast<double>(rate) / fps + remainder;
    const uint32_t whole = static_cast<uint32_t>(std::floor(exact));
    remainder = exact - whole;
    return whole;
}
```

Store one remainder per `nes_ctx`, reset it on audio-format or machine-mode changes, and use it for each frame inside `nes_run_frames`.

- [ ] **Step 4: Run core tests**

Run: `.\core\build\host\Release\nes_core_test.exe`

Expected: `PASS (0 failures)` and theoretical error below one sample.

- [ ] **Step 5: Commit cadence accuracy**

```powershell
git add core/src/nes_core.cpp core/tests/test_core.cpp core/src/nes_audio_clock.hpp
git commit -m "fix: accumulate fractional audio samples per frame"
```

### Task 3: Expose snapshots through JNI and Java FramePublisher

**Files:**
- Modify: `app/src/main/cpp/nes_jni.cpp`
- Modify: `app/src/main/java/com/flynes/emu/NesCore.java`
- Create: `app/src/main/java/com/flynes/emu/video/PublishedFrame.java`
- Create: `app/src/main/java/com/flynes/emu/video/FramePublisher.java`
- Test: `app/src/test/java/com/flynes/emu/video/FramePublisherTest.java`

- [ ] **Step 1: Write a fake-source duplicate suppression test**

```java
@Test public void emitsOnlyWhenSequenceChanges() {
    FakeFrameSource source = new FakeFrameSource(7, 256, 240);
    FramePublisher publisher = new FramePublisher(source);
    assertTrue(publisher.poll().isPresent());
    assertTrue(publisher.poll().isEmpty());
    source.sequence = 8;
    assertEquals(8, publisher.poll().orElseThrow().sequence());
}

private static final class FakeFrameSource implements FramePublisher.Source {
    long sequence;
    final int width;
    final int height;
    FakeFrameSource(long sequence, int width, int height) {
        this.sequence = sequence;
        this.width = width;
        this.height = height;
    }
    @Override public PublishedFrame copyLatest() {
        return new PublishedFrame(sequence, width, height, width * 4,
                PublishedFrame.Format.RGBA8888,
                ByteBuffer.allocateDirect(width * height * 4).asReadOnlyBuffer());
    }
}
```

- [ ] **Step 2: Run the test and confirm missing classes**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*FramePublisherTest' --console=plain`

Expected: FAIL for missing video classes.

- [ ] **Step 3: Implement the JNI copy and Java contract**

Add JNI:

```cpp
JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NesCore_nativeCopyVideoFrame(JNIEnv* env, jclass, jlong handle,
                                                  jobject dst, jintArray meta) {
    void* pixels = env->GetDirectBufferAddress(dst);
    const jlong cap = env->GetDirectBufferCapacity(dst);
    nes_video_snapshot snap{sizeof(snap), 1};
    const int rc = nes_copy_video_frame(reinterpret_cast<nes_t*>(handle), pixels,
                                        static_cast<size_t>(cap), &snap);
    if (rc < 0) return rc;
    jint values[5] = {static_cast<jint>(snap.width), static_cast<jint>(snap.height),
                      snap.pitch, static_cast<jint>(snap.format),
                      static_cast<jint>(snap.bytes_written)};
    env->SetIntArrayRegion(meta, 0, 5, values);
    return static_cast<jlong>(snap.sequence);
}
```

Define `PublishedFrame` as an immutable value containing sequence, dimensions, pitch, format, and a read-only `ByteBuffer`. `FramePublisher.poll()` returns empty when the native sequence equals `lastSequence`.

- [ ] **Step 4: Run unit and Android build tests**

Run: `./gradlew.bat :app:testDebugUnitTest :app:assembleDebug --console=plain`

Expected: duplicate suppression PASS and both ABIs link `nes_copy_video_frame`.

- [ ] **Step 5: Commit the Android frame boundary**

```powershell
git add app/src/main/cpp/nes_jni.cpp app/src/main/java/com/flynes/emu/NesCore.java app/src/main/java/com/flynes/emu/video app/src/test/java/com/flynes/emu/video
git commit -m "feat: expose sequenced video snapshots to Android"
```

### Task 4: Present frames with OpenGL ES and correct aspect modes

**Files:**
- Create: `app/src/main/java/com/flynes/emu/video/AspectMode.java`
- Create: `app/src/main/java/com/flynes/emu/video/Viewport.java`
- Create: `app/src/main/java/com/flynes/emu/video/ViewportCalculator.java`
- Create: `app/src/main/java/com/flynes/emu/video/FrameRenderer.java`
- Create: `app/src/main/java/com/flynes/emu/video/GlFrameView.java`
- Test: `app/src/test/java/com/flynes/emu/video/ViewportCalculatorTest.java`

- [ ] **Step 1: Write viewport tests**

```java
@Test public void fourByThreeIn2340x1080Is1440x1080() {
    Viewport viewport = ViewportCalculator.fit(2340, 1080, AspectMode.FOUR_BY_THREE, 256, 240);
    assertEquals(1440, viewport.width());
    assertEquals(1080, viewport.height());
}

@Test public void squarePixelsUseSixteenByFifteen() {
    Viewport viewport = ViewportCalculator.fit(2340, 1080, AspectMode.SQUARE_PIXELS, 256, 240);
    assertEquals(1152, viewport.width());
    assertEquals(1080, viewport.height());
}
```

- [ ] **Step 2: Run and observe missing calculator**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*ViewportCalculatorTest' --console=plain`

Expected: FAIL for missing types.

- [ ] **Step 3: Implement render-on-demand texture presentation**

Use `GLSurfaceView.RENDERMODE_WHEN_DIRTY`. The vertex shader passes a full-screen quad; the fragment shader is:

```glsl
precision mediump float;
uniform sampler2D uTexture;
varying vec2 vTexCoord;
void main() { gl_FragColor = texture2D(uTexture, vTexCoord); }
```

On a new `PublishedFrame`, call `glTexSubImage2D` once and `requestRender()`. `ViewportCalculator` computes FOUR_BY_THREE, SQUARE_PIXELS (16:15), and INTEGER_SCALE viewports inside the safe content rect. Use nearest filtering by default and linear filtering only when selected.

- [ ] **Step 4: Run tests and an emulator screenshot smoke**

Run:

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug --console=plain
```

Expected: viewport tests PASS; 4:3 mode reports 1440×1080 on the audit emulator.

- [ ] **Step 5: Commit the presenter**

```powershell
git add app/src/main/java/com/flynes/emu/video app/src/test/java/com/flynes/emu/video
git commit -m "feat: add aspect-correct OpenGL frame presenter"
```

### Task 5: Select and request 60/90/120 Hz modes

**Files:**
- Create: `app/src/main/java/com/flynes/emu/video/RefreshMode.java`
- Create: `app/src/main/java/com/flynes/emu/video/DisplayModeController.java`
- Test: `app/src/test/java/com/flynes/emu/video/DisplayModeSelectorTest.java`

- [ ] **Step 1: Write deterministic mode selection tests**

```java
@Test public void request120ChoosesMatchingNativeResolution() {
    DisplayCandidate[] modes = {new DisplayCandidate(1,2340,1080,60f),
                                new DisplayCandidate(2,2340,1080,120f)};
    assertEquals(2, DisplayModeSelector.select(modes, 2340, 1080, RefreshMode.HZ_120));
}

@Test public void unsupported90FallsBackToAuto() {
    DisplayCandidate[] modes = {new DisplayCandidate(1,2340,1080,60f)};
    assertEquals(0, DisplayModeSelector.select(modes, 2340, 1080, RefreshMode.HZ_90));
}
```

- [ ] **Step 2: Run the selection tests**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*DisplayModeSelectorTest' --console=plain`

Expected: FAIL for missing selector.

- [ ] **Step 3: Implement platform requests with safe fallback**

`DisplayModeController.apply(Activity, Surface, RefreshMode, float sourceFps)` selects only native-resolution modes, sets `preferredDisplayModeId` when a requested mode exists, and on API 30+ calls `surface.setFrameRate(sourceFps, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE)`. Unsupported requests return `ApplyResult.FALLBACK_AUTO` and never crash.

- [ ] **Step 4: Run tests and API 24/36 compilation**

Run: `./gradlew.bat :app:testDebugUnitTest :app:lintDebug --console=plain`

Expected: selection tests PASS; Lint confirms API 30 calls are guarded.

- [ ] **Step 5: Commit display-mode control**

```powershell
git add app/src/main/java/com/flynes/emu/video app/src/test/java/com/flynes/emu/video
git commit -m "feat: add adaptive display refresh control"
```

### Task 6: Replace AudioThread with a recoverable one-frame AudioEngine

**Files:**
- Create: `app/src/main/java/com/flynes/emu/audio/AudioSink.java`
- Create: `app/src/main/java/com/flynes/emu/audio/AndroidAudioSink.java`
- Create: `app/src/main/java/com/flynes/emu/audio/AudioEngine.java`
- Modify: `app/src/main/java/com/flynes/emu/session/EmulationSession.java`
- Delete: `app/src/main/java/com/flynes/emu/AudioThread.java`
- Test: `app/src/test/java/com/flynes/emu/audio/AudioEngineTest.java`

- [ ] **Step 1: Write partial-write and dead-object tests**

```java
@Test public void drainsPartialWritesBeforeRunningNextFrame() {
    FakeSink sink = new FakeSink(200, 300, 500);
    FakeCore core = new FakeCore(1000);
    new AudioEngine(core, sink, error -> {}).runOneIteration();
    assertEquals(3, sink.writeCalls);
    assertEquals(1, core.runCalls);
}

@Test public void deadObjectRequestsRebuildWithoutRunningAhead() {
    FakeSink sink = FakeSink.deadObject();
    FakeCore core = new FakeCore(800);
    AtomicReference<AudioError> error = new AtomicReference<>();
    new AudioEngine(core, sink, error::set).runOneIteration();
    assertEquals(AudioError.DEAD_OBJECT, error.get());
    assertEquals(1, core.runCalls);
}

private static final class FakeCore implements CoreFacade {
    final int samples;
    final ByteBuffer audio = ByteBuffer.allocateDirect(4096);
    int runCalls;
    FakeCore(int samples) { this.samples = samples; }
    @Override public int create() { return 0; }
    @Override public int loadRom(byte[] rom) { return 0; }
    @Override public void setInput(int mask) { }
    @Override public int runOneFrame() { runCalls++; return samples; }
    @Override public ByteBuffer audioBuffer() { return audio; }
    @Override public byte[] saveState() { return new byte[0]; }
    @Override public int loadState(byte[] state) { return 0; }
    @Override public void destroy() { }
}

private static final class FakeSink implements AudioSink {
    private final java.util.ArrayDeque<Integer> writes = new java.util.ArrayDeque<>();
    private final boolean dead;
    int writeCalls;
    FakeSink(int... writes) {
        for (int write : writes) this.writes.add(write);
        this.dead = false;
    }
    private FakeSink(boolean dead) { this.dead = dead; }
    static FakeSink deadObject() { return new FakeSink(true); }
    @Override public int write(ByteBuffer pcm, int sampleOffset, int sampleCount) {
        writeCalls++;
        return dead ? AudioSink.ERROR_DEAD_OBJECT : writes.removeFirst();
    }
}
```

- [ ] **Step 2: Run and observe missing audio contracts**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*AudioEngineTest' --console=plain`

Expected: FAIL for missing audio classes.

- [ ] **Step 3: Implement one-frame pacing and focus handling**

Add `ByteBuffer audioBuffer()` to `CoreFacade`. Define `AudioSink.write(ByteBuffer pcm, int sampleOffset, int sampleCount)` to return consumed samples and `ERROR_DEAD_OBJECT = -6`. `AudioEngine.runOneIteration()` calls `core.runOneFrame()`, then loops until every produced sample is written or an error occurs. `AndroidAudioSink` uses at most two NES frames above HAL minimum, translates partial/negative writes, and rebuilds on dead object. Add `AudioFocusRequest`, focus-loss pause/duck policy, and a receiver for `ACTION_AUDIO_BECOMING_NOISY`.

- [ ] **Step 4: Run unit and instrumentation audio smoke**

Run: `./gradlew.bat :app:testDebugUnitTest :app:connectedDebugAndroidTest --console=plain`

Expected: audio tests PASS; route-change test pauses or rebuilds within 500 ms.

- [ ] **Step 5: Commit the audio engine**

```powershell
git add app/src/main/java/com/flynes/emu/audio app/src/main/java/com/flynes/emu/session/EmulationSession.java app/src/test/java/com/flynes/emu/audio app/src/main/java/com/flynes/emu/AudioThread.java
git commit -m "refactor: pace one emulated frame per audio iteration"
```

### Task 7: Integrate and capture frame-pacing evidence

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/MainActivity.java`
- Delete: `app/src/main/java/com/flynes/emu/EmuView.java`
- Create: `scripts/measure-frame-pacing.ps1`
- Create: `docs/acceptance/alpha/frame-audio.md`

- [ ] **Step 1: Replace Choreographer/nativeBlit with GlFrameView**

Wire `EmulationSession` frame callbacks to `FramePublisher.poll()` and `GlFrameView.submit(frame)`. Remove unconditional per-vsync `nativeBlit`, fixed 1024×960 surface sizing, and forced HQ4X.

- [ ] **Step 2: Build and install**

Run: `./gradlew.bat :app:assembleDebug --console=plain`

Expected: `BUILD SUCCESSFUL`; no references to `EmuView` or `nativeBlit` remain.

- [ ] **Step 3: Measure 60 and 120 Hz**

`scripts/measure-frame-pacing.ps1` must record display period, Surface timestamps, source sequence deltas, process threads, and AudioTrack underruns for 120 seconds into CSV files under `docs/acceptance/alpha/data/`.

- [ ] **Step 4: Record the acceptance result**

In `frame-audio.md`, record device, build SHA, source NTSC/PAL rate, display mode, p50/p95 interval, missed-source-frame rate, CPU, underruns, and thermal status. Pass only at NTSC 59.9–60.1, PAL 49.9–50.1, and <0.5% source-frame misses.

- [ ] **Step 5: Commit integration evidence**

```powershell
git add app/src/main/java/com/flynes/emu/MainActivity.java app/src/main/java/com/flynes/emu/EmuView.java scripts/measure-frame-pacing.ps1 docs/acceptance/alpha/frame-audio.md
git commit -m "perf: integrate sequenced rendering and verify frame pacing"
```
