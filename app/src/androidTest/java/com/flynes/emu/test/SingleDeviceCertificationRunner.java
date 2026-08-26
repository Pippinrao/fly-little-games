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
        if (!this.arguments.getBoolean("flynesCertification", false)) {
            this.arguments.putString("notAnnotation", DeviceCertification.class.getName());
        }
        super.onCreate(this.arguments);
    }

    @Override public void onStart() {
        if (arguments.getBoolean("flynesCertification", false)) {
            String expectedSerial = arguments.getString("authorizedSerial", "").trim();
            String expectedModel = arguments.getString("authorizedModel", "").trim();
            String actualSerial = shell("getprop ro.serialno").trim();
            String actualModel = shell("getprop ro.product.model").trim();
            if (expectedSerial.isEmpty() || expectedModel.isEmpty()
                    || !expectedSerial.equals(actualSerial) || !expectedModel.equals(actualModel)) {
                throw new IllegalStateException("Certification device mismatch: expected serial/model "
                        + expectedSerial + "/" + expectedModel + " but found "
                        + actualSerial + "/" + actualModel);
            }
            authorized = true;
        }
        super.onStart();
    }

    static boolean isAuthorized() { return authorized; }

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
