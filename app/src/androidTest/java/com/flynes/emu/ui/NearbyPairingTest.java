package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import android.widget.ImageView;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.os.SystemClock;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;

import android.content.Intent;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import com.flynes.emu.NearbyPairingActivity;
import com.flynes.emu.R;
import com.google.zxing.BinaryBitmap;
import com.google.zxing.RGBLuminanceSource;
import com.google.zxing.common.HybridBinarizer;
import com.google.zxing.qrcode.QRCodeReader;
import com.google.zxing.ReaderException;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * N01/N02/N03 chrome from the approved HTML mockup. Pairing stages are N07/N10,
 * not these entry pages.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbyPairingTest {

    @Test
    public void crossAppHostWaitsForHarmonyGuest() throws Exception {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> scenario = ActivityScenario.launch(intent)) {
            String invite = readInvite(scenario);
            AtomicReference<File> filesDir = new AtomicReference<>();
            scenario.onActivity(activity -> filesDir.set(activity.getFilesDir()));
            File inviteFile = new File(filesDir.get(), "nearby-cross-invite.txt");
            File sessionFile = new File(filesDir.get(), "nearby-cross-host-session.bin");
            sessionFile.delete();
            try (FileOutputStream output = new FileOutputStream(inviteFile, false)) {
                output.write(invite.getBytes(StandardCharsets.UTF_8));
                output.getFD().sync();
            }

            AtomicReference<byte[]> sessionId = new AtomicReference<>();
            AtomicReference<Object> connectedSession = new AtomicReference<>();
            long deadline = SystemClock.elapsedRealtime() + 60_000L;
            while (SystemClock.elapsedRealtime() < deadline && sessionId.get() == null) {
                scenario.onActivity(activity -> {
                    try {
                        Field field = NearbyPairingActivity.class.getDeclaredField("mvpSession");
                        field.setAccessible(true);
                        Object session = field.get(activity);
                        if (session == null) return;
                        Method snapshot = session.getClass().getDeclaredMethod("snapshot");
                        snapshot.setAccessible(true);
                        int[] value = (int[]) snapshot.invoke(session);
                        if (value != null && value[0] == NearbyMvpPeerTestBridge.LOBBY) {
                            connectedSession.set(session);
                            Method id = session.getClass().getDeclaredMethod("sessionId");
                            id.setAccessible(true);
                            sessionId.set((byte[]) id.invoke(session));
                            Field handoff = NearbyPairingActivity.class.getDeclaredField("handoffStarted");
                            handoff.setAccessible(true);
                            handoff.setBoolean(activity, true);
                            activity.finish();
                        }
                    } catch (ReflectiveOperationException error) {
                        throw new AssertionError(error);
                    }
                });
                if (sessionId.get() == null) SystemClock.sleep(50L);
            }
            org.junit.Assert.assertNotNull("Harmony guest did not join the Android host", sessionId.get());
            try (FileOutputStream output = new FileOutputStream(sessionFile, false)) {
                output.write(sessionId.get());
                output.getFD().sync();
            }
            Object session = connectedSession.get();
            org.junit.Assert.assertNotNull("Connected product session missing", session);
            Class<?> sessionClass = session.getClass();
            Method selectRom = sessionClass.getDeclaredMethod("selectRom", byte[].class);
            Method confirm = sessionClass.getDeclaredMethod("confirm");
            Method snapshot = sessionClass.getDeclaredMethod("snapshot");
            Method submitInput = sessionClass.getDeclaredMethod("submitInput", int.class);
            Method completedFrames = sessionClass.getDeclaredMethod("completedFrames");
            Method copyLatestFrame = sessionClass.getDeclaredMethod("copyLatestFrame", byte[].class);
            Method pullPcm = sessionClass.getDeclaredMethod("pullPcm", short[].class);
            for (Method method : new Method[] { selectRom, confirm, snapshot, submitInput,
                    completedFrames, copyLatestFrame, pullPcm }) method.setAccessible(true);

            Class<?> gameClass = Class.forName("com.flynes.emu.NearbyMvpGame");
            Method loadGame = gameClass.getDeclaredMethod("load", android.content.Context.class);
            loadGame.setAccessible(true);
            Object selection = loadGame.invoke(null, ApplicationProvider.getApplicationContext());
            Field romField = selection.getClass().getDeclaredField("rom");
            romField.setAccessible(true);
            org.junit.Assert.assertTrue("Host rejected manifest multiplayer ROM",
                    (Boolean) selectRom.invoke(session, (Object) (byte[]) romField.get(selection)));
            org.junit.Assert.assertTrue("Host confirmation failed", (Boolean) confirm.invoke(session));

            String endEpochArgument = InstrumentationRegistry.getArguments()
                    .getString("crossAppEndEpochMs", "0");
            long requestedEndEpoch = Long.parseLong(endEpochArgument);
            long endEpoch = requestedEndEpoch > System.currentTimeMillis()
                    ? requestedEndEpoch : System.currentTimeMillis();
            long runDeadline = SystemClock.elapsedRealtime()
                    + Math.max(30_000L, endEpoch - System.currentTimeMillis() + 60_000L);
            int state = 0;
            while (SystemClock.elapsedRealtime() < runDeadline) {
                state = ((int[]) snapshot.invoke(session))[0];
                if (state == 6 || state == 4) break;
                SystemClock.sleep(10L);
            }
            org.junit.Assert.assertEquals("Cross-app session did not start", 6, state);
            String frameArgument = InstrumentationRegistry.getArguments().getString("crossAppFrames", "600");
            long targetFrames = Long.parseLong(frameArgument);
            long pcmSamples = 0;
            short[] pcm = new short[2048];
            byte[] capturedFrame = null;
            long capturedFrameIndex = -1;
            while (((Long) completedFrames.invoke(session) < targetFrames
                    || System.currentTimeMillis() < endEpoch) &&
                    SystemClock.elapsedRealtime() < runDeadline) {
                long frame = (Long) completedFrames.invoke(session);
                int buttons = ((frame / 30L) & 1L) == 0L ? 0x01 : 0;
                if (!(Boolean) submitInput.invoke(session, buttons)) {
                    SystemClock.sleep(1L);
                    continue;
                }
                pcmSamples += (Integer) pullPcm.invoke(session, (Object) pcm);
                long completed = (Long) completedFrames.invoke(session);
                if (capturedFrame == null && completed >= targetFrames) {
                    capturedFrame = new byte[256 * 240 * 2];
                    capturedFrameIndex = (Long) copyLatestFrame.invoke(
                            session, (Object) capturedFrame);
                }
                // Match the product cadence so the single reliable QUIC stream can
                // drain in both directions instead of building a synthetic burst.
                SystemClock.sleep(8L);
            }
            long finished = (Long) completedFrames.invoke(session);
            // The peer can publish the target frame between the loop condition and
            // the body. Capture that newly visible frame before evaluating evidence.
            if (capturedFrame == null && finished >= targetFrames) {
                capturedFrame = new byte[256 * 240 * 2];
                capturedFrameIndex = (Long) copyLatestFrame.invoke(
                        session, (Object) capturedFrame);
            }
            org.junit.Assert.assertTrue("Host did not reach requested frames", finished >= targetFrames);
            org.junit.Assert.assertTrue("Host stopped before the shared wall-clock deadline",
                    System.currentTimeMillis() >= endEpoch);
            org.junit.Assert.assertNotNull("Host did not capture the requested common frame", capturedFrame);
            org.junit.Assert.assertEquals("Host common capture is not the requested completed frame",
                    targetFrames - 1L, capturedFrameIndex);
            org.junit.Assert.assertTrue("Host did not publish PCM", pcmSamples > 0);
            writeBytes(new File(filesDir.get(), "nearby-cross-host-frame.rgb565"), capturedFrame);
            writeBytes(new File(filesDir.get(), "nearby-cross-host-play.txt"), String.format(Locale.US,
                    "%d,%d,%d", finished, capturedFrameIndex, pcmSamples).getBytes(StandardCharsets.UTF_8));
            SystemClock.sleep(2_000L);
        }
    }

    private static void writeBytes(File file, byte[] bytes) throws Exception {
        if (file.exists()) org.junit.Assert.assertTrue(file.delete());
        try (FileOutputStream output = new FileOutputStream(file, false)) {
            output.write(bytes);
            output.getFD().sync();
        }
    }

    @Test
    public void createPageMatchesApprovedInviteMockup() {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> ignored = ActivityScenario.launch(intent)) {
            onView(withId(R.id.nearby_create_block)).check(matches(isDisplayed()));
            onView(withText(R.string.nearby_invite_headline)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_code_value)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_qr_wrap)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_regenerate)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_cancel)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_stage_permission_row)).check(doesNotExist());
        }
    }

    @Test
    public void createPagePublishesMachineReadableQr() throws Exception {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> scenario = ActivityScenario.launch(intent)) {
            AtomicBoolean hasQrImage = new AtomicBoolean();
            long deadline = SystemClock.elapsedRealtime() + 8000L;
            while (!hasQrImage.get() && SystemClock.elapsedRealtime() < deadline) {
                scenario.onActivity(activity -> {
                    FrameLayout frame = activity.findViewById(R.id.nearby_invite_qr);
                    hasQrImage.set(frame.getChildCount() == 1 && frame.getChildAt(0) instanceof ImageView);
                });
                if (!hasQrImage.get()) SystemClock.sleep(100L);
            }
            org.junit.Assert.assertTrue("Host did not publish a QR image", hasQrImage.get());
            AtomicReference<Bitmap> qrBitmap = new AtomicReference<>();
            scenario.onActivity(activity -> {
                FrameLayout frame = activity.findViewById(R.id.nearby_invite_qr);
                qrBitmap.set(((BitmapDrawable) ((ImageView) frame.getChildAt(0)).getDrawable()).getBitmap());
            });
            Bitmap bitmap = qrBitmap.get();
            int[] pixels = new int[bitmap.getWidth() * bitmap.getHeight()];
            bitmap.getPixels(pixels, 0, bitmap.getWidth(), 0, 0,
                    bitmap.getWidth(), bitmap.getHeight());
            String payload = new QRCodeReader().decode(new BinaryBitmap(new HybridBinarizer(
                    new RGBLuminanceSource(bitmap.getWidth(), bitmap.getHeight(), pixels)))).getText();
            org.junit.Assert.assertTrue("Unexpected LAN QR format", payload.startsWith("flynes-lan-v1:"));
        }
    }

    @Test
    public void invitationStillWorksAfterThirtySecondsAndCanBeRegenerated() throws Exception {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> scenario = ActivityScenario.launch(intent)) {
            String first = readInvite(scenario);
            SystemClock.sleep(32_000L);
            onView(withId(R.id.nearby_invite_code_value))
                    .check(matches(withText(R.string.nearby_mvp_waiting)));
            org.junit.Assert.assertEquals("Waiting must preserve the invitation", first, readInvite(scenario));
            scenario.onActivity(activity -> activity.findViewById(R.id.nearby_invite_regenerate).performClick());
            String replacement = readInvite(scenario);
            org.junit.Assert.assertFalse("Regeneration must replace the old invitation", first.equals(replacement));
            scenario.onActivity(activity -> activity.findViewById(R.id.nearby_invite_cancel).performClick());
            long finishDeadline = SystemClock.elapsedRealtime() + 2_000L;
            while (scenario.getState() != androidx.lifecycle.Lifecycle.State.DESTROYED
                    && SystemClock.elapsedRealtime() < finishDeadline) {
                SystemClock.sleep(25L);
            }
            org.junit.Assert.assertEquals(androidx.lifecycle.Lifecycle.State.DESTROYED, scenario.getState());
        }
    }

    @Test
    public void productQrConnectsARealNativeGuestIntoTheSameSession() throws Exception {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> scenario = ActivityScenario.launch(intent);
             NearbyMvpPeerTestBridge guest = new NearbyMvpPeerTestBridge()) {
            String invite = readInvite(scenario);
            Class<?> addressClass = Class.forName("com.flynes.emu.NearbyMvpLanAddress");
            Method currentAddress = addressClass.getDeclaredMethod("current");
            currentAddress.setAccessible(true);
            String localIpv4 = (String) currentAddress.invoke(null);
            org.junit.Assert.assertNotNull("Emulator has no private IPv4", localIpv4);
            org.junit.Assert.assertTrue("Native guest did not accept product QR",
                    guest.join(localIpv4, invite));

            long deadline = SystemClock.elapsedRealtime() + 12_000L;
            int[] guestSnapshot = null;
            AtomicReference<byte[]> hostSessionId = new AtomicReference<>();
            AtomicBoolean hostUiConnected = new AtomicBoolean();
            while (SystemClock.elapsedRealtime() < deadline) {
                guestSnapshot = guest.snapshot();
                scenario.onActivity(activity -> {
                    try {
                        Field field = NearbyPairingActivity.class.getDeclaredField("mvpSession");
                        field.setAccessible(true);
                        Object session = field.get(activity);
                        if (session == null) return;
                        Method sessionId = session.getClass().getDeclaredMethod("sessionId");
                        sessionId.setAccessible(true);
                        hostSessionId.set((byte[]) sessionId.invoke(session));
                        TextView status = activity.findViewById(R.id.nearby_invite_code_value);
                        hostUiConnected.set(activity.getString(R.string.nearby_mvp_connected)
                                .contentEquals(status.getText()));
                    } catch (ReflectiveOperationException error) {
                        throw new AssertionError(error);
                    }
                });
                if (guestSnapshot != null && guestSnapshot[0] == NearbyMvpPeerTestBridge.LOBBY
                        && hostSessionId.get() != null && hostUiConnected.get()) break;
                SystemClock.sleep(50L);
            }
            org.junit.Assert.assertNotNull("Guest snapshot unavailable", guestSnapshot);
            org.junit.Assert.assertEquals("Guest did not enter LOBBY",
                    NearbyMvpPeerTestBridge.LOBBY, guestSnapshot[0]);
            org.junit.Assert.assertArrayEquals("Host and guest must expose one session id",
                    guest.sessionId(), hostSessionId.get());
            org.junit.Assert.assertTrue("Host UI did not render the connected state",
                    hostUiConnected.get());
        }
    }

    private static String readInvite(ActivityScenario<NearbyPairingActivity> scenario) throws Exception {
        AtomicReference<Bitmap> bitmap = new AtomicReference<>();
        long deadline = SystemClock.elapsedRealtime() + 8000L;
        while (SystemClock.elapsedRealtime() < deadline) {
            bitmap.set(null);
            scenario.onActivity(activity -> {
                FrameLayout frame = activity.findViewById(R.id.nearby_invite_qr);
                if (frame.getChildCount() == 1 && frame.getChildAt(0) instanceof ImageView) {
                    bitmap.set(((BitmapDrawable) ((ImageView) frame.getChildAt(0)).getDrawable()).getBitmap());
                }
            });
            Bitmap value = bitmap.get();
            if (value != null) {
                int[] pixels = new int[value.getWidth() * value.getHeight()];
                value.getPixels(pixels, 0, value.getWidth(), 0, 0, value.getWidth(), value.getHeight());
                try {
                    return new QRCodeReader().decode(new BinaryBitmap(new HybridBinarizer(
                            new RGBLuminanceSource(value.getWidth(), value.getHeight(), pixels))),
                            java.util.Collections.singletonMap(com.google.zxing.DecodeHintType.PURE_BARCODE,
                                    Boolean.TRUE)).getText();
                } catch (ReaderException incompleteFrame) {
                    // Decode the generated bitmap as a pure symbol. Camera perspective
                    // detection is exercised by physical Scan Kit, not this in-memory image.
                }
            }
            SystemClock.sleep(100L);
        }
        throw new AssertionError("Host did not publish a decodable QR image");
    }

    @Test
    public void joinPageMatchesApprovedCodeMockup() {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_JOIN_CODE);
        try (ActivityScenario<NearbyPairingActivity> ignored = ActivityScenario.launch(intent)) {
            onView(withId(R.id.nearby_join_block)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_code_input)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_submit)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_stage_permission_row)).check(doesNotExist());
        }
    }

    @Test
    public void anonymousJoinControlSetIsNotBuilt() {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> ignored = ActivityScenario.launch(intent)) {
            onView(withText(R.string.nearby_join_request_anonymous)).check(doesNotExist());
            onView(withText(R.string.nearby_join_accept)).check(doesNotExist());
            onView(withText(R.string.nearby_join_reject)).check(doesNotExist());
        }
    }
}
