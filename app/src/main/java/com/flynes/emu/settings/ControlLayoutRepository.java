package com.flynes.emu.settings;

import android.content.Context;
import com.flynes.emu.input.ControlLayoutV2;

public final class ControlLayoutRepository {
    public static final String KEY="controls.layout_v2";
    private final SettingsStore store;
    public ControlLayoutRepository(SettingsStore store){this.store=store;}
    public ControlLayoutRepository(Context context){this(new SharedPreferencesSettingsStore(context));}
    public ControlLayoutV2 load(){return ControlLayoutV2.decodeOrRecommended(store.getString(KEY,""));}
    public void save(ControlLayoutV2 layout){store.putString(KEY,layout.encode());}
    public void reset(){save(ControlLayoutV2.recommended());}
}
