package com.flynes.emu.catalog.android;

import java.util.Objects;
import java.util.UUID;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Function;

/** Platform-only UUID hex → persistable URI map. Never written into FLYCAT01. */
public final class AndroidUuidSafMap {
    static final String BUILTIN_KEY = "builtin.uuid";
    static final String LOCATOR_PREFIX = "locator:";

    private final Function<String, String> get;
    private final BiConsumer<String, String> put;
    private final Consumer<String> remove;

    public AndroidUuidSafMap(
            Function<String, String> get,
            BiConsumer<String, String> put,
            Consumer<String> remove) {
        this.get = Objects.requireNonNull(get, "get");
        this.put = Objects.requireNonNull(put, "put");
        this.remove = Objects.requireNonNull(remove, "remove");
    }

    public void put(byte[] uuid, String persistableUri) {
        String hex = toHex(requireAssigned(uuid));
        if (BUILTIN_KEY.equals(hex)) {
            throw new IllegalArgumentException("uuid collides with builtin key");
        }
        String locator = Objects.requireNonNull(persistableUri, "persistable URI").trim();
        if (locator.isEmpty()) throw new IllegalArgumentException("persistable URI");
        String previous = get.apply(hex);
        if (previous != null && !previous.isEmpty() && !previous.equals(locator)) {
            remove.accept(LOCATOR_PREFIX + previous);
        }
        put.accept(hex, locator);
        put.accept(LOCATOR_PREFIX + locator, hex);
    }

    public String get(byte[] uuid) {
        return get.apply(toHex(requireUuid(uuid)));
    }

    public void remove(byte[] uuid) {
        String hex = toHex(requireUuid(uuid));
        if (BUILTIN_KEY.equals(hex)) return;
        String locator = get.apply(hex);
        remove.accept(hex);
        if (locator != null && !locator.isEmpty()) remove.accept(LOCATOR_PREFIX + locator);
    }

    public byte[] uuidForLocator(String persistableUri) {
        String locator = Objects.requireNonNull(persistableUri, "persistable URI").trim();
        if (locator.isEmpty()) return null;
        String hex = get.apply(LOCATOR_PREFIX + locator);
        return hex == null || hex.isEmpty() ? null : parseHex(hex);
    }

    public boolean isAssigned(byte[] uuid) {
        byte[] checked = requireUuid(uuid);
        for (byte value : checked) {
            if (value != 0) return true;
        }
        return false;
    }

    public byte[] builtinUuid() {
        String stored = get.apply(BUILTIN_KEY);
        if (stored != null && !stored.isEmpty()) return parseHex(stored);
        byte[] generated = uuidBytes(UUID.randomUUID());
        put.accept(BUILTIN_KEY, toHex(generated));
        return generated;
    }

    public static String toHex(byte[] uuid) {
        return com.flynes.emu.catalog.HexEncoding.lower(requireUuid(uuid));
    }

    public static byte[] parseHex(String hex) {
        if (hex == null || hex.length() != 32) {
            throw new IllegalArgumentException("uuid hex");
        }
        byte[] bytes = new byte[16];
        for (int index = 0; index < 16; index++) {
            int high = Character.digit(hex.charAt(index * 2), 16);
            int low = Character.digit(hex.charAt(index * 2 + 1), 16);
            if (high < 0 || low < 0) throw new IllegalArgumentException("uuid hex");
            bytes[index] = (byte) ((high << 4) | low);
        }
        return bytes;
    }

    private static byte[] requireAssigned(byte[] uuid) {
        byte[] checked = requireUuid(uuid);
        if (!hasNonZero(checked)) throw new IllegalArgumentException("uuid must not be all zero");
        return checked;
    }

    private static byte[] requireUuid(byte[] uuid) {
        if (uuid == null || uuid.length != 16) throw new IllegalArgumentException("uuid");
        return uuid;
    }

    private static boolean hasNonZero(byte[] uuid) {
        for (byte value : uuid) {
            if (value != 0) return true;
        }
        return false;
    }

    private static byte[] uuidBytes(UUID value) {
        byte[] bytes = new byte[16];
        long high = value.getMostSignificantBits();
        long low = value.getLeastSignificantBits();
        for (int index = 0; index < 8; index++) {
            bytes[index] = (byte) (high >>> (8 * (7 - index)));
            bytes[8 + index] = (byte) (low >>> (8 * (7 - index)));
        }
        if (!hasNonZero(bytes)) throw new IllegalStateException("generated zero uuid");
        return bytes;
    }
}
