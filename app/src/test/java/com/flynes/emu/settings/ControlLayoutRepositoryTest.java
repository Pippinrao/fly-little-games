package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import com.flynes.emu.input.ControlLayoutV2;
import org.junit.Test;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

public final class ControlLayoutRepositoryTest {
    @Test public void persistsAndRecoversMalformedDataToRecommended() {
        MemoryStore store=new MemoryStore(); ControlLayoutRepository repository=new ControlLayoutRepository(store);
        ControlLayoutV2 expected=ControlLayoutV2.recommended().move(ControlLayoutV2.Element.A,.90f,.50f);
        repository.save(expected); assertEquals(expected,repository.load());
        repository.reset(); assertEquals(ControlLayoutV2.recommended(),repository.load());
        store.values.put(ControlLayoutRepository.KEY,"v2|broken");
        assertEquals(ControlLayoutV2.recommended(),repository.load());
    }

    @Test public void loadDecodesNativeStringAndRoundTripsRecommendedEncode() {
        // JNI libnescore is not loaded in :app:testDebugUnitTest; Task 5 host test covers
        // fly_control_layout_get/apply. Instrumented note: production Context constructor
        // uses FlyNesApp JNI when FlyNesApplication owns a native app.
        MemoryBackend backend = new MemoryBackend("");
        ControlLayoutRepository repository = new ControlLayoutRepository(backend);
        String expected = ControlLayoutV2.recommended().encode();
        repository.save(ControlLayoutV2.recommended());
        assertEquals(expected, backend.utf8);
        assertEquals(expected, repository.load().encode());
        backend.utf8 = "v2|broken";
        assertEquals(ControlLayoutV2.recommended(), repository.load());
        assertEquals(expected, repository.load().encode());
    }
    private static final class MemoryStore implements SettingsStore {
        private final Map<String,Object> values=new LinkedHashMap<>();
        public Map<String,?> snapshot(){return Collections.unmodifiableMap(new LinkedHashMap<>(values));}
        public boolean commit(SettingsBatch batch){for(String k:batch.removals())values.remove(k);values.putAll(batch.strings());values.putAll(batch.integers());values.putAll(batch.booleans());return true;}
    }

    /** Fake fly_control_layout_get/apply for JVM tests (JNI cannot load libnescore here). */
    private static final class MemoryBackend implements ControlLayoutRepository.Backend {
        String utf8;
        MemoryBackend(String utf8) { this.utf8 = utf8; }
        @Override public String controlLayoutGet() { return utf8; }
        @Override public boolean controlLayoutApply(String next) {
            utf8 = next;
            return true;
        }
    }
}
