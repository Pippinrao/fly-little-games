package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import com.flynes.emu.input.ControlLayoutV2;
import org.junit.Test;
import java.util.HashMap;
import java.util.Map;

public final class ControlLayoutRepositoryTest {
    @Test public void persistsAndRecoversMalformedDataToRecommended() {
        MemoryStore store=new MemoryStore(); ControlLayoutRepository repository=new ControlLayoutRepository(store);
        ControlLayoutV2 expected=ControlLayoutV2.recommended().move(ControlLayoutV2.Element.A,.90f,.50f);
        repository.save(expected); assertEquals(expected,repository.load());
        store.putString(ControlLayoutRepository.KEY,"v2|broken");
        assertEquals(ControlLayoutV2.recommended(),repository.load());
    }
    private static final class MemoryStore implements SettingsStore {
        private final Map<String,Object> values=new HashMap<>();
        public String getString(String k,String f){Object v=values.get(k);return v instanceof String?(String)v:f;}
        public int getInt(String k,int f){return f;} public boolean getBoolean(String k,boolean f){return f;}
        public void putString(String k,String v){values.put(k,v);} public void putInt(String k,int v){} public void putBoolean(String k,boolean v){}
    }
}
