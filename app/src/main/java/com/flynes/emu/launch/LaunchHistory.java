package com.flynes.emu.launch;

@FunctionalInterface
public interface LaunchHistory {
    void recordSuccessfulLaunch(LaunchRequest request);
}
