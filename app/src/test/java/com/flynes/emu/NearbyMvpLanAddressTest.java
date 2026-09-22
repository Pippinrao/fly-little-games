package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

public final class NearbyMvpLanAddressTest {
    @Test public void prefersReachableWifiOverVirtualAndCellular() {
        assertEquals("192.168.3.18", NearbyMvpLanAddress.choose(
                new String[]{"lo", "rmnet_data0", "veth0", "wlan0"},
                new String[]{"127.0.0.1", "10.21.2.4", "192.168.208.1", "192.168.3.18"}));
    }

    @Test public void rejectsUnreachableAddressChoices() {
        assertNull(NearbyMvpLanAddress.choose(
                new String[]{"lo", "rmnet_data0", "wlan0"},
                new String[]{"127.0.0.1", "10.21.2.4", "169.254.1.2"}));
    }

    @Test public void usesPrivateEthernetWhenWifiIsAbsent() {
        assertEquals("10.0.2.15", NearbyMvpLanAddress.choose(
                new String[]{"lo", "rmnet_data0", "eth0"},
                new String[]{"127.0.0.1", "10.21.2.4", "10.0.2.15"}));
    }

    @Test public void prefersConcurrentLocalWifiWhenPresent() {
        assertEquals("192.168.3.159", NearbyMvpLanAddress.choose(
                new String[]{"wlan0", "wlan1"},
                new String[]{"192.168.3.15", "192.168.3.159"}));
    }

    @Test public void prefersHotspotInterfaceWhenPresent() {
        assertEquals("10.244.139.74", NearbyMvpLanAddress.choose(
                new String[]{"wlan0", "wlan1", "ap0"},
                new String[]{"192.168.3.15", "192.168.3.159", "10.244.139.74"}));
    }
}
