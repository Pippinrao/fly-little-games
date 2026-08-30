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
    private static final class MemoryStore implements SettingsStore {
        private final Map<String,Object> values=new LinkedHashMap<>();
        public Map<String,?> snapshot(){return Collections.unmodifiableMap(new LinkedHashMap<>(values));}
        public boolean commit(SettingsBatch batch){for(String k:batch.removals())values.remove(k);values.putAll(batch.strings());values.putAll(batch.integers());values.putAll(batch.booleans());return true;}
    }
}
