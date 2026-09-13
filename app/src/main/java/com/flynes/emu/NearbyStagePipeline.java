package com.flynes.emu;

import android.content.res.ColorStateList;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

/**
 * Renders the §2.1 pairing pipeline — the one element that explains why pairing cannot proceed —
 * for every page that includes {@code R.layout.view_nearby_stage_pipeline}.
 *
 * <p>It exists as one class rather than one method per page because the pipeline is the same list on
 * 好友/附近设备 and on 配对, and the rule that governs it is a single number: the index of the first
 * failing stage. From that index this class derives, for every row, the status icon, the status
 * tint, the stage-name colour, whether the row's reason is visible, and the row's accessibility
 * label. Deriving all five in one place is the point: no caller can mark a row current while leaving
 * its reason hidden, and no two pages can disagree about what "current" looks like.
 *
 * <p>Decision D5 fixes the three treatments: stages before the first failure are <em>passed</em>,
 * the first failure is <em>current</em> and is the only row that explains itself, and later stages
 * are <em>not yet reached</em> — neutral, with no reason and no blocked key. The status is carried in
 * the row's {@code contentDescription} ("stage name, status") rather than by the icon alone, because
 * {@code color-not-only} forbids encoding state as colour and a screen reader cannot read a tint;
 * the icon and the name view are marked not-important in the layout so the row is a single stop.
 *
 * <p>The stage order is fixed: the row, status, name and reason arrays below are parallel, and the
 * index is the stage. Nothing may reorder one without the others, which is why the arrays are
 * private and the only entry point takes a stage index.
 */
final class NearbyStagePipeline {
    /** Stage count, and therefore the length of every parallel array below. */
    static final int STAGE_COUNT = 7;

    /** Index of 权限, the first stage of the §2.1 pipeline. */
    static final int PERMISSION = 0;

    private static final int[] STAGE_ROWS = {
            R.id.nearby_stage_permission_row,
            R.id.nearby_stage_discovery_row,
            R.id.nearby_stage_auth_row,
            R.id.nearby_stage_wifi_row,
            R.id.nearby_stage_quic_row,
            R.id.nearby_stage_version_row,
            R.id.nearby_stage_codec_row,
    };
    private static final int[] STAGE_STATUS_VIEWS = {
            R.id.nearby_stage_permission_status,
            R.id.nearby_stage_discovery_status,
            R.id.nearby_stage_auth_status,
            R.id.nearby_stage_wifi_status,
            R.id.nearby_stage_quic_status,
            R.id.nearby_stage_version_status,
            R.id.nearby_stage_codec_status,
    };
    private static final int[] STAGE_NAME_VIEWS = {
            R.id.nearby_stage_permission_name,
            R.id.nearby_stage_discovery_name,
            R.id.nearby_stage_auth_name,
            R.id.nearby_stage_wifi_name,
            R.id.nearby_stage_quic_name,
            R.id.nearby_stage_version_name,
            R.id.nearby_stage_codec_name,
    };
    private static final int[] STAGE_REASON_VIEWS = {
            R.id.nearby_stage_permission_reason,
            R.id.nearby_stage_discovery_reason,
            R.id.nearby_stage_auth_reason,
            R.id.nearby_stage_wifi_reason,
            R.id.nearby_stage_quic_reason,
            R.id.nearby_stage_version_reason,
            R.id.nearby_stage_codec_reason,
    };

    private NearbyStagePipeline() {}

    /**
     * Applies the D5 treatment for {@code firstFailingStage} to every row of the included pipeline.
     * An out-of-range index is a programming error, not a state, so it fails loudly instead of
     * rendering a pipeline in which nothing is marked.
     */
    static void render(AppCompatActivity host, int firstFailingStage) {
        if (firstFailingStage < 0 || firstFailingStage >= STAGE_COUNT) {
            throw new IllegalArgumentException(
                    "first failing stage out of range: " + firstFailingStage);
        }
        for (int i = 0; i < STAGE_COUNT; i++) {
            View row = host.findViewById(STAGE_ROWS[i]);
            ImageView status = host.findViewById(STAGE_STATUS_VIEWS[i]);
            TextView name = host.findViewById(STAGE_NAME_VIEWS[i]);
            TextView reason = host.findViewById(STAGE_REASON_VIEWS[i]);

            int label;
            if (i < firstFailingStage) {
                label = R.string.nearby_stage_status_passed;
                status.setImageResource(R.drawable.ic_stage_passed);
                status.setImageTintList(ColorStateList.valueOf(host.getColor(R.color.fly_on_surface)));
                name.setTextColor(host.getColor(R.color.fly_on_surface));
            } else if (i == firstFailingStage) {
                label = R.string.nearby_stage_status_current;
                status.setImageResource(R.drawable.ic_stage_failed);
                status.setImageTintList(ColorStateList.valueOf(host.getColor(R.color.fly_error)));
                name.setTextColor(host.getColor(R.color.fly_on_surface));
            } else {
                label = R.string.nearby_stage_status_not_reached;
                status.setImageResource(R.drawable.ic_stage_not_reached);
                status.setImageTintList(ColorStateList.valueOf(host.getColor(R.color.fly_outline)));
                name.setTextColor(host.getColor(R.color.fly_on_surface_muted));
            }

            // Stage name first, then its status: a screen reader must never announce the state
            // before the stage it describes.
            row.setContentDescription(name.getText() + ", " + host.getString(label));

            // Only the first failing stage explains itself; earlier and later stages carry no
            // reason (D5). The reason stays its own node after the row.
            reason.setVisibility(i == firstFailingStage ? View.VISIBLE : View.GONE);
        }
    }
}
