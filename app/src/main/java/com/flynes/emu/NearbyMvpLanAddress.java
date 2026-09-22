package com.flynes.emu;

import java.net.Inet4Address;
import java.net.NetworkInterface;
import java.net.SocketException;
import java.util.ArrayList;
import java.util.Enumeration;

final class NearbyMvpLanAddress {
    private NearbyMvpLanAddress() {}

    static String choose(String[] interfaceNames, String[] addresses) {
        if (interfaceNames == null || addresses == null ||
                interfaceNames.length != addresses.length) return null;
        // A portable hotspot is the intended offline path. When it is absent,
        // Android concurrent/local-only Wi-Fi commonly uses wlan1.
        for (String preferred : new String[]{"ap", "swlan", "wlan1", "wlan0", "wlan", "eth"}) {
            for (int i = 0; i < interfaceNames.length; i++) {
                if (interfaceNames[i] == null || addresses[i] == null ||
                        !interfaceNames[i].startsWith(preferred)) continue;
                String[] octets = addresses[i].split("\\.", -1);
                if (octets.length != 4) continue;
                int[] ip = new int[4];
                boolean valid = true;
                for (int part = 0; part < 4; part++) {
                    try {
                        ip[part] = Integer.parseInt(octets[part]);
                        valid &= ip[part] >= 0 && ip[part] <= 255;
                    } catch (NumberFormatException error) {
                        valid = false;
                    }
                }
                if (!valid) continue;
                if (ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
                        (ip[0] == 192 && ip[1] == 168)) return addresses[i];
            }
        }
        return null;
    }

    static String current() {
        try {
            ArrayList<String> names = new ArrayList<>();
            ArrayList<String> addresses = new ArrayList<>();
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface item = interfaces.nextElement();
                if (!item.isUp()) continue;
                Enumeration<java.net.InetAddress> ip = item.getInetAddresses();
                while (ip.hasMoreElements()) {
                    java.net.InetAddress address = ip.nextElement();
                    if (address instanceof Inet4Address) {
                        names.add(item.getName());
                        addresses.add(address.getHostAddress());
                    }
                }
            }
            return choose(names.toArray(new String[0]), addresses.toArray(new String[0]));
        } catch (SocketException error) {
            return null;
        }
    }
}
