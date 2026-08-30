package com.flynes.emu.test;

import android.os.Bundle;
import android.os.ParcelFileDescriptor;

import androidx.test.runner.AndroidJUnitRunner;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;

/** Fails closed before certification tests unless adb serial and model were explicitly verified. */
public final class SingleDeviceCertificationRunner extends AndroidJUnitRunner {
    private Bundle arguments;
    private static volatile boolean authorized;

    @Override public void onCreate(Bundle arguments) {
        this.arguments = arguments == null ? new Bundle() : new Bundle(arguments);
        authorized = false;
        if (!certificationRequested()) {
            this.arguments.putString("notAnnotation", DeviceCertification.class.getName());
        }
        super.onCreate(this.arguments);
    }

    @Override public void onStart() {
        if (certificationRequested()) {
            String expectedSerial = arguments.getString("authorizedSerial", "").trim();
            String expectedModel = arguments.getString("authorizedModel", "").trim();
            String actualSerial = shell("getprop ro.serialno").trim();
            String actualModel = shell("getprop ro.product.model").trim();
            String qemu = shell("getprop ro.kernel.qemu").trim();
            String bootQemu = shell("getprop ro.boot.qemu").trim();
            String hardware = shell("getprop ro.hardware").trim().toLowerCase();
            String fingerprint = shell("getprop ro.build.fingerprint").trim().toLowerCase();
            if (expectedSerial.isEmpty() || expectedModel.isEmpty()
                    || !expectedSerial.equals(actualSerial) || !expectedModel.equals(actualModel)) {
                throw new IllegalStateException("Certification device mismatch: expected serial/model "
                        + expectedSerial + "/" + expectedModel + " but found "
                        + actualSerial + "/" + actualModel);
            }
            if ("1".equals(qemu) || "1".equals(bootQemu) || hardware.contains("ranchu")
                    || hardware.contains("goldfish") || fingerprint.contains("generic")) {
                throw new IllegalStateException(
                        "Certification requires a physical device, not an emulator");
            }
            authorized = true;
        }
        super.onStart();
    }

    static boolean isAuthorized() { return authorized; }

    private boolean certificationRequested() {
        Object value = arguments.get("flynesCertification");
        return value != null && "true".equals(value.toString());
    }

    private String shell(String command) {
        try (ParcelFileDescriptor descriptor = getUiAutomation().executeShellCommand(command);
             FileInputStream input = new FileInputStream(descriptor.getFileDescriptor());
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[256];
            int read;
            while ((read = input.read(buffer)) >= 0) output.write(buffer, 0, read);
            return output.toString(StandardCharsets.UTF_8.name());
        } catch (Exception failure) {
            throw new IllegalStateException("Unable to verify certification device", failure);
        }
    }
}
