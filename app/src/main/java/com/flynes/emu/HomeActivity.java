package com.flynes.emu;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;

import androidx.appcompat.app.AppCompatActivity;

/** Branded launcher that makes play, library, sources, and settings discoverable. */
public final class HomeActivity extends AppCompatActivity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_home);

        View root = findViewById(R.id.home_root);
        int base = getResources().getDimensionPixelSize(R.dimen.fly_space_4);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(base + insets.getSystemWindowInsetLeft(),
                    base + insets.getSystemWindowInsetTop(),
                    base + insets.getSystemWindowInsetRight(),
                    base + insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();

        findViewById(R.id.builtin_game).setOnClickListener(
                view -> startActivity(new Intent(this, MainActivity.class)));
        findViewById(R.id.add_source).setOnClickListener(
                view -> startActivity(new Intent(this, GameLibraryActivity.class)));
        findViewById(R.id.open_library).setOnClickListener(
                view -> startActivity(new Intent(this, GameLibraryActivity.class)));
        findViewById(R.id.open_settings).setOnClickListener(
                view -> startActivity(new Intent(this, SettingsActivity.class)));
    }
}
