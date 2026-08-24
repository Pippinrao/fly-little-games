package com.flynes.emu;

import android.app.AlertDialog;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import androidx.activity.OnBackPressedCallback;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.ViewCompat;

import com.flynes.emu.input.ControlLayoutV2;
import com.flynes.emu.input.ControlLayoutWarnings;
import com.flynes.emu.settings.ControlLayoutRepository;

/** Settings-only, high fidelity editor over a deterministic NES-style test frame. */
public final class ControlLayoutActivity extends AppCompatActivity {
    private ControlLayoutRepository repository;
    private ControlLayoutEditorView preview;
    private TextView warning;
    private SeekBar scale;
    private boolean syncing;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        repository=new ControlLayoutRepository(this);
        LinearLayout root=new LinearLayout(this);
        root.setId(R.id.control_layout_root); root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackgroundColor(0xFF121316);
        root.setOnApplyWindowInsetsListener((v,insets)->{
            v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom()); return insets;});

        FrameLayout stage=new FrameLayout(this); stage.setBackgroundColor(0xFF0D0E11);
        preview=new ControlLayoutEditorView(this); preview.setId(R.id.control_layout_preview);
        preview.setLayout(repository.load());
        stage.addView(preview,new FrameLayout.LayoutParams(-1,-1));
        root.addView(stage,new LinearLayout.LayoutParams(0,-1,1f));
        root.addView(buildPanel(),new LinearLayout.LayoutParams(dp(328),-1));
        setContentView(root); root.requestApplyInsets();
        preview.setListener(this::renderState); renderState();
        getOnBackPressedDispatcher().addCallback(this,new OnBackPressedCallback(true){
            @Override public void handleOnBackPressed(){finish();}
        });
    }

    private View buildPanel() {
        LinearLayout panel=new LinearLayout(this); panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(20),dp(16),dp(20),dp(16)); panel.setBackgroundColor(0xFF1B1D22);
        LinearLayout fields=new LinearLayout(this); fields.setOrientation(LinearLayout.VERTICAL);
        TextView title=text(R.string.control_layout_title,24,0xFFF4EFE6); title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        ViewCompat.setAccessibilityHeading(title,true); fields.addView(title,wrap(dp(4)));
        fields.addView(text(R.string.control_layout_hint,14,0xFFBEB8AE),wrap(dp(8)));
        warning=text(0,13,0xFFFFB35E); warning.setId(R.id.control_layout_warning);
        warning.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE); fields.addView(warning,wrap(dp(6)));
        fields.addView(text(R.string.control_layout_scale,13,0xFFF4EFE6),wrap(0));
        scale=new SeekBar(this); scale.setId(R.id.control_layout_scale); scale.setMax(130); scale.setProgress(50);
        scale.setContentDescription(getString(R.string.control_layout_scale));
        scale.setOnSeekBarChangeListener(seekListener(value->{ if(!syncing) preview.resizeSelected((value+50)/100f); }));
        fields.addView(scale,new LinearLayout.LayoutParams(-1,dp(48)));
        fields.addView(text(R.string.control_layout_opacity_label,13,0xFFF4EFE6),wrap(0));
        SeekBar opacity=new SeekBar(this); opacity.setId(R.id.control_layout_opacity); opacity.setMax(60);
        opacity.setProgress(Math.round((preview==null?.78f:preview.layout().opacity())*100f)-40);
        opacity.setContentDescription(getString(R.string.control_layout_opacity_label));
        opacity.setOnSeekBarChangeListener(seekListener(v->preview.setOpacity((v+40)/100f)));
        fields.addView(opacity,new LinearLayout.LayoutParams(-1,dp(48)));
        Switch test=new Switch(this); test.setId(R.id.control_layout_test); test.setText(R.string.control_layout_test);
        test.setTextColor(0xFFF4EFE6); test.setMinHeight(dp(48)); test.setOnCheckedChangeListener((b,c)->preview.setTryMode(c));
        fields.addView(test,new LinearLayout.LayoutParams(-1,dp(48)));
        ScrollView scroll=new ScrollView(this); scroll.setFillViewport(false); scroll.addView(fields,new ScrollView.LayoutParams(-1,-2));
        panel.addView(scroll,new LinearLayout.LayoutParams(-1,0,1f));
        LinearLayout actions=new LinearLayout(this); actions.setOrientation(LinearLayout.HORIZONTAL);
        Button undo=button(R.id.control_layout_undo,R.string.control_layout_undo,false); undo.setOnClickListener(v->preview.undo());
        Button defaults=button(R.id.control_layout_default,R.string.control_layout_recommended,false); defaults.setOnClickListener(v->preview.resetRecommended());
        actions.addView(undo,new LinearLayout.LayoutParams(0,dp(48),1f)); actions.addView(defaults,new LinearLayout.LayoutParams(0,dp(48),1.4f));
        panel.addView(actions,new LinearLayout.LayoutParams(-1,dp(48)));
        Button save=button(R.id.control_layout_save,R.string.control_layout_save,true); save.setOnClickListener(v->save());
        panel.addView(save,new LinearLayout.LayoutParams(-1,dp(52)));
        Button discard=button(R.id.control_layout_discard,R.string.control_layout_discard,false); discard.setOnClickListener(v->finish());
        LinearLayout.LayoutParams discardParams=new LinearLayout.LayoutParams(-1,dp(48)); discardParams.topMargin=dp(8); panel.addView(discard,discardParams);
        return panel;
    }

    private void save(){
        if(ControlLayoutWarnings.inspect(preview.layout()).isEmpty()){commit();return;}
        new AlertDialog.Builder(this).setMessage(R.string.control_layout_warning_save)
                .setNegativeButton(R.string.control_layout_keep_editing,null)
                .setPositiveButton(R.string.control_layout_save,(d,w)->commit()).show();
    }
    private void commit(){repository.save(preview.layout());setResult(RESULT_OK);finish();}
    private void renderState(){
        if(warning==null)return; ControlLayoutWarnings w=ControlLayoutWarnings.inspect(preview.layout());
        int text=0;
        if(w.contains(ControlLayoutWarnings.Code.TOO_SMALL))text=R.string.control_layout_warning_small;
        else if(w.contains(ControlLayoutWarnings.Code.OVERLAP))text=R.string.control_layout_warning_overlap;
        else if(w.contains(ControlLayoutWarnings.Code.GESTURE_ZONE))text=R.string.control_layout_warning_gesture;
        else if(w.contains(ControlLayoutWarnings.Code.CENTRAL_PROTECTION))text=R.string.control_layout_warning_center;
        warning.setText(text==0?"":getString(text));
        syncing=true; scale.setProgress(Math.round(preview.selectedScale()*100f)-50); syncing=false;
    }
    private TextView text(int res,int size,int color){TextView v=new TextView(this);if(res!=0)v.setText(res);v.setTextSize(size);v.setTextColor(color);v.setGravity(Gravity.CENTER_VERTICAL);return v;}
    private Button button(int id,int text,boolean primary){Button b=new Button(this);b.setId(id);b.setText(text);b.setAllCaps(false);b.setTextColor(primary?0xFF121316:0xFFF4EFE6);b.setBackgroundColor(primary?0xFFFF6B5E:0xFF292C33);b.setMinHeight(dp(48));return b;}
    private LinearLayout.LayoutParams wrap(int bottom){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.bottomMargin=bottom;return p;}
    private SeekBar.OnSeekBarChangeListener seekListener(IntChange change){return new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int p,boolean user){if(user)change.accept(p);}public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}};}
    private int dp(int v){return Math.round(v*getResources().getDisplayMetrics().density);}
    private interface IntChange{void accept(int value);}
}
