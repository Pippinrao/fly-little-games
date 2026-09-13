package com.flynes.emu;

import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

/**
 * The nearby status surface during play — the pause-drawer rows and the pinned banner (spec §8 A1a-4,
 * decisions D6, D7 and D10).
 *
 * <p><strong>D7 is the rule that shapes this class: in local single-player there is no nearby status
 * at all.</strong> Not a blocked row, not a greyed-out "not connected" line — nothing. A permanent
 * in-game chrome element describing a session that does not exist is noise on the one screen where
 * the user is playing a game, so {@link #hasSession()} gates every view this class can add, and
 * {@link #drawerRowIds(boolean)} / {@link #bannerRowIds(boolean)} return an empty set without one.
 * The gate is a single source rather than a per-row check so that adding a row later cannot
 * accidentally leak it into single-player.
 *
 * <p><strong>D6 splits the surface in two.</strong> The lightweight state rows live in the existing
 * pause drawer. The states that demand action — 冻结, the reconnect countdown, and the authority
 * timeout options 接管 / 继续单人 / 保存结束 — belong in a non-dismissible banner pinned to the top
 * of the run surface, because a frozen end must not have to open a drawer to find the way out. The
 * banner is not a modal and has no dismiss control: it disappears when the state does.
 *
 * <p><strong>D10 keeps one line that costs no ABI:</strong> the multi-branch visualisation is
 * deferred because no branch identity exists, but "branches are never merged automatically" is a
 * promise the design makes to the user, so it stays as an always-known statement rather than as a
 * blocked row.
 *
 * <p>Today {@link #hasSession()} is a constant false: plan slice A0 has not yet put {@code
 * fly_session} on the Android binding, so no session can exist and nothing here is attached. That
 * constant is the only thing standing between this file and live rows — when the session surface
 * lands, {@code hasSession()} reads it and the tables below become visible unchanged.
 */
public final class NearbyInGameStatus {

    /** Drawer rows: {@code {row id, label, value}} in §22.4 order. */
    private static final int[][] DRAWER_ROWS = {
            {R.id.nearby_ingame_row_mode, R.string.nearby_ingame_mode,
                    R.string.nearby_blocked_mode_gate},
            {R.id.nearby_ingame_row_connection_quality, R.string.nearby_ingame_connection_quality,
                    R.string.nearby_blocked_connection_quality},
            {R.id.nearby_ingame_row_seat, R.string.nearby_ingame_seat,
                    R.string.nearby_blocked_mode_gate},
            {R.id.nearby_ingame_row_pause_state, R.string.nearby_ingame_pause_state,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_ingame_row_branch_reunion, R.string.nearby_ingame_branch_reunion,
                    R.string.nearby_blocked_branch_merge},
            // Not a blocked row (D10): this is the always-known invariant, so its value is the
            // statement itself. It must survive any later edit that removes the branch rows.
            {R.id.nearby_ingame_row_branch_no_auto_merge, R.string.nearby_ingame_branch_no_auto_merge,
                    R.string.nearby_ingame_branch_no_auto_merge},
            {R.id.nearby_ingame_row_local_mute, R.string.nearby_ingame_local_mute,
                    R.string.nearby_blocked_local_mute},
    };

    /** Banner rows: the action-demanding states of D6, in the order a frozen end must read them. */
    private static final int[][] BANNER_ROWS = {
            {R.id.nearby_ingame_banner_row_frozen, R.string.nearby_ingame_frozen,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_ingame_banner_row_reconnect_countdown, R.string.nearby_ingame_reconnect_countdown,
                    R.string.nearby_blocked_session_read},
            {R.id.nearby_ingame_banner_row_authority_timeout, R.string.nearby_ingame_authority_timeout,
                    R.string.nearby_blocked_authority_recovery},
            {R.id.nearby_ingame_banner_row_takeover, R.string.nearby_ingame_takeover,
                    R.string.nearby_blocked_authority_recovery},
            {R.id.nearby_ingame_banner_row_continue_solo, R.string.nearby_ingame_continue_solo,
                    R.string.nearby_blocked_authority_recovery},
            {R.id.nearby_ingame_banner_row_save_and_end, R.string.nearby_ingame_save_and_end,
                    R.string.nearby_blocked_authority_recovery},
    };

    private static final int[] NONE = new int[0];

    private NearbyInGameStatus() {}

    /**
     * Whether this device has a nearby session at all. The Android binding exposes no {@code
     * fly_session} surface yet (plan slice A0 owns it), so there is never a session and the whole
     * nearby status surface stays absent during local single-player (D7). When A0 lands, this reads
     * the session handle instead of returning a constant; nothing else in this class changes.
     */
    static boolean hasSession() {
        return false;
    }

    /** Drawer row ids for the current state: the §22.4 rows, or nothing without a session (D7). */
    public static int[] drawerRowIds(boolean session) {
        return session ? column(DRAWER_ROWS, 0) : NONE;
    }

    /** Banner row ids for the current state: the D6 action rows, or nothing without a session. */
    public static int[] bannerRowIds(boolean session) {
        return session ? column(BANNER_ROWS, 0) : NONE;
    }

    /**
     * Every row id this surface can ever attach, drawer and banner together. Instrumentation uses it
     * to assert the D7 absence directly: during local single-player none of these ids may exist
     * anywhere on the run surface. Asserting a hand-written list instead would let a newly added row
     * escape the check.
     */
    public static int[] allRowIds() {
        int[] drawer = column(DRAWER_ROWS, 0);
        int[] banner = column(BANNER_ROWS, 0);
        int[] all = new int[drawer.length + banner.length];
        System.arraycopy(drawer, 0, all, 0, drawer.length);
        System.arraycopy(banner, 0, all, drawer.length, banner.length);
        return all;
    }

    /** Labels of the drawer rows, parallel to {@link #drawerRowIds}. */
    public static int[] drawerLabelIds() {
        return column(DRAWER_ROWS, 1);
    }

    /** Values of the drawer rows, parallel to {@link #drawerRowIds}. */
    public static int[] drawerValueIds() {
        return column(DRAWER_ROWS, 2);
    }

    public static int[] bannerLabelIds() {
        return column(BANNER_ROWS, 1);
    }

    public static int[] bannerValueIds() {
        return column(BANNER_ROWS, 2);
    }

    /** Adds the §22.4 status rows to the pause drawer. No-op without a session (D7). */
    static void installDrawerRows(AppCompatActivity host, LinearLayout drawer) {
        if (!hasSession()) return;
        addRows(host, drawer, DRAWER_ROWS);
    }

    /**
     * Pins the D6 banner to the top of the run surface. No-op without a session (D7), and it is the
     * only place the banner is created — deliberately not a modal and deliberately without a dismiss
     * control, because the state it reports clears on its own.
     */
    static void installBanner(AppCompatActivity host, FrameLayout root) {
        if (!hasSession()) return;
        LinearLayout banner = new LinearLayout(host);
        banner.setId(R.id.nearby_ingame_banner);
        banner.setOrientation(LinearLayout.VERTICAL);
        banner.setBackgroundColor(0xE6201416);
        banner.setPadding(dp(host, 16), dp(host, 8), dp(host, 16), dp(host, 8));
        addRows(host, banner, BANNER_ROWS);

        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                android.view.Gravity.TOP);
        root.addView(banner, params);
    }

    private static void addRows(AppCompatActivity host, LinearLayout container, int[][] rows) {
        for (int[] row : rows) {
            LinearLayout line = new LinearLayout(host);
            line.setId(row[0]);
            line.setOrientation(LinearLayout.VERTICAL);

            TextView label = new TextView(host);
            label.setText(row[1]);
            label.setTextColor(0xFFF4EFE6);
            TextView value = new TextView(host);
            value.setText(row[2]);
            value.setTextColor(0xFFFF6B5E);

            line.addView(label);
            line.addView(value);
            // One accessibility stop per row, label first, so the value is never announced alone.
            line.setContentDescription(label.getText() + ", " + value.getText());
            container.addView(line);
        }
    }

    private static int[] column(int[][] rows, int index) {
        int[] values = new int[rows.length];
        for (int i = 0; i < rows.length; i++) values[i] = rows[i][index];
        return values;
    }

    private static int dp(AppCompatActivity host, int value) {
        return Math.round(value * host.getResources().getDisplayMetrics().density);
    }
}
