package com.flynes.emu.video.power;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import com.flynes.emu.video.quality.AdaptiveQualityPolicy;
import com.flynes.emu.video.quality.AdaptiveTransition;
import com.flynes.emu.video.quality.AdaptiveTriggerClass;
import com.flynes.emu.video.quality.RuntimeTemporalState;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;

public final class AdaptiveQualityControllerTest {
    @Test public void criticalPauseHasPriorityOverEveryGraphDowngrade() {
        AdaptiveQualityController controller = controller("motion");

        ThermalPowerSnapshot snapshot = new ThermalPowerSnapshot(ThermalBand.CRITICAL,
                true, 5, 45.0f, 99.0f, 1_000L);
        TemporalTransition transition = controller.update(snapshot, true,
                RuntimeTemporalState.MOTION_COMPENSATING, false);

        assertEquals("motion", controller.targetConfigurationId());
        assertEquals(RuntimeTemporalState.SURFACE_SUSPENDED_HOLD,
                transition.toTemporalState());
        assertEquals(AdaptiveTriggerClass.CRITICAL_THERMAL, transition.triggerClass());
    }

    @Test public void severeAndBatteryPoliciesWalkExactlyOneExplicitEdge() {
        AdaptiveQualityController thermal = controller("motion");
        TemporalTransition thermalTransition = thermal.update(new ThermalPowerSnapshot(
                        ThermalBand.SEVERE, true, 5, 43.0f, 99.0f, 1_000L), true,
                RuntimeTemporalState.MOTION_COMPENSATING, false);
        assertEquals("native-sharp", thermal.targetConfigurationId());
        assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                thermalTransition.toTemporalState());
        assertEquals(AdaptiveTriggerClass.THERMAL_OR_BATTERY_TEMPERATURE,
                thermalTransition.triggerClass());

        AdaptiveQualityController batterySaver = controller("motion");
        TemporalTransition saverTransition = batterySaver.update(new ThermalPowerSnapshot(
                        ThermalBand.NONE, true, 80, 35.0f, 1.0f, 1_000L), true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, false);
        assertEquals("native-nearest", batterySaver.targetConfigurationId());
        assertEquals(AdaptiveTriggerClass.SYSTEM_BATTERY_SAVER,
                saverTransition.triggerClass());
    }

    @Test public void optionalDegradationRequiresOptInAndRecoveryNeedsContinuousSafeWindow() {
        AdaptiveQualityController controller = controller("motion");
        ThermalPowerSnapshot moderate = new ThermalPowerSnapshot(ThermalBand.MODERATE,
                false, 80, 40.0f, 9.0f, 1_000L);

        assertNull(controller.update(moderate, false,
                RuntimeTemporalState.IMMEDIATE_NATIVE, false));
        assertEquals("motion", controller.targetConfigurationId());

        controller.update(moderate, true, RuntimeTemporalState.IMMEDIATE_NATIVE, false);
        assertEquals("native-sharp", controller.targetConfigurationId());

        ThermalPowerSnapshot safeStart = new ThermalPowerSnapshot(ThermalBand.LIGHT,
                false, 80, 37.0f, 2.0f, 2_000L);
        assertNull(controller.update(safeStart, true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, true));
        assertNull(controller.update(new ThermalPowerSnapshot(ThermalBand.LIGHT,
                        false, 80, 37.0f, 2.0f, 121_999L), true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, true));
        assertEquals("native-sharp", controller.targetConfigurationId());

        TemporalTransition recovered = controller.update(new ThermalPowerSnapshot(
                        ThermalBand.LIGHT, false, 80, 37.0f, 2.0f, 122_000L), true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, true);
        assertEquals("motion", controller.targetConfigurationId());
        assertEquals("motion", recovered.toConfigurationId());
    }

    @Test public void unsafeSampleResetsRecoveryTimer() {
        AdaptiveQualityController controller = controller("motion");
        controller.update(new ThermalPowerSnapshot(ThermalBand.MODERATE, false,
                        80, 40.0f, 9.0f, 1_000L), true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, false);
        controller.update(new ThermalPowerSnapshot(ThermalBand.LIGHT, false,
                        80, 37.0f, 2.0f, 2_000L), true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, false);
        controller.update(new ThermalPowerSnapshot(ThermalBand.MODERATE, false,
                        80, 39.0f, 2.0f, 80_000L), true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, false);

        assertNull(controller.update(new ThermalPowerSnapshot(ThermalBand.LIGHT, false,
                        80, 37.0f, 2.0f, 122_001L), true,
                RuntimeTemporalState.IMMEDIATE_NATIVE, true));
        assertEquals("native-sharp", controller.targetConfigurationId());
    }

    private static AdaptiveQualityController controller(String initial) {
        java.util.List<AdaptiveTransition> transitions = Arrays.asList(
                new AdaptiveTransition("motion", "native-sharp",
                        AdaptiveTriggerClass.THERMAL_OR_BATTERY_TEMPERATURE),
                new AdaptiveTransition("motion", "native-nearest",
                        AdaptiveTriggerClass.SYSTEM_BATTERY_SAVER),
                new AdaptiveTransition("motion", "native-nearest",
                        AdaptiveTriggerClass.LOW_BATTERY),
                new AdaptiveTransition("motion", "native-sharp",
                        AdaptiveTriggerClass.OPTIONAL_ADAPTIVE_PROTECTION));
        Map<String, Float> budgets = new HashMap<>();
        budgets.put("motion", 8.0f);
        budgets.put("native-sharp", 8.0f);
        budgets.put("native-nearest", 8.0f);
        AdaptiveQualityPolicy policy = new AdaptiveQualityPolicy(transitions, budgets,
                40.0f, 42.0f, 38.0f, 120_000L);
        return new AdaptiveQualityController(policy, initial);
    }
}
