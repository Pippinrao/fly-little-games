package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import org.junit.Test;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

public final class DisplayStatusRepositoryTest {
    @Test public void recordsExactRequestedActualAndStablePolicyReason() {
        MemoryStore store = new MemoryStore();
        DisplayStatusRepository repository = new DisplayStatusRepository(store);
        DisplayStatus status = new DisplayStatus(60f, 59.94f, "AUTO_BEST_SUPPORTED");
        repository.save(status);
        assertEquals(status, repository.load());
    }
    private static final class MemoryStore implements SettingsStore {
        final Map<String,Object> v=new LinkedHashMap<>();
        public Map<String,?> snapshot(){return Collections.unmodifiableMap(new LinkedHashMap<>(v));}
        public boolean commit(SettingsBatch batch){for(String k:batch.removals())v.remove(k);v.putAll(batch.strings());v.putAll(batch.integers());v.putAll(batch.booleans());return true;}
    }
}
