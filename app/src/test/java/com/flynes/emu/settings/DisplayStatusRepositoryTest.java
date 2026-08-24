package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import org.junit.Test;
import java.util.HashMap;
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
        final Map<String,Object> v=new HashMap<>();
        public String getString(String k,String f){Object x=v.get(k);return x instanceof String?(String)x:f;}
        public int getInt(String k,int f){Object x=v.get(k);return x instanceof Integer?(Integer)x:f;}
        public boolean getBoolean(String k,boolean f){return f;}
        public void putString(String k,String x){v.put(k,x);} public void putInt(String k,int x){v.put(k,x);} public void putBoolean(String k,boolean x){}
    }
}
