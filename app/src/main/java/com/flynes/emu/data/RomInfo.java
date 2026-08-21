package com.flynes.emu.data;

public record RomInfo(
        RomIdentity identity,
        String title,
        String publisher,
        String developer,
        String region,
        int mapper,
        int submapper,
        int prgSize,
        int chrSize,
        int wramSize,
        int vramSize,
        boolean hasBattery,
        int system,
        int cpu,
        int ppu,
        boolean ntsc,
        boolean patched,
        String crc32,
        int players) { }
