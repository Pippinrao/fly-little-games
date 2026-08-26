package com.flynes.emu.settings;

import android.content.Context;
import com.flynes.emu.input.ControlLayoutV2;

import java.util.Map;

public final class ControlLayoutRepository {
    public static final String KEY="controls.layout_v2";
    private final SettingsStore store;
    public ControlLayoutRepository(SettingsStore store){this.store=store;}
    public ControlLayoutRepository(Context context){this(new SharedPreferencesSettingsStore(context));}
    public ControlLayoutV2 load(){Map<String,?> values=store.snapshot();Object value=values.get(KEY);return ControlLayoutV2.decodeOrRecommended(value instanceof String?(String)value:"");}
    public void save(ControlLayoutV2 layout){store.commit(new SettingsBatch.Builder().putString(KEY,layout.encode()).build());}
    public void reset(){save(ControlLayoutV2.recommended());}
}
