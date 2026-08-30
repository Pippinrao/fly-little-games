package com.flynes.emu.catalog;

/**
 * Explicit scanner-selected policy used to turn an exact raw ZIP name into display text.
 * Launch code must never infer this from the device locale.
 */
public enum ZipNameEncoding {
    UTF8_EFS,
    UNICODE_PATH,
    CP437,
    GB18030
}
