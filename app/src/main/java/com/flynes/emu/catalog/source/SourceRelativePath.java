package com.flynes.emu.catalog.source;

/**
 * Pure-JVM mirror of the native catalog's canonical relative-path rules
 * ({@code is_safe_relative_path} in {@code shared/src/app/flynes_app.cpp}).
 *
 * <p>The native catalog keys every package by a platform-neutral relative logical path using
 * {@code '/'} separators. A path that breaks these rules is rejected during the scan, so the
 * platform layer must only ever hand over a path that passes them.
 */
public final class SourceRelativePath {
    private SourceRelativePath() {
    }

    /** Returns whether {@code path} is a canonical relative logical path. */
    public static boolean isCanonical(String path) {
        if (path == null || path.isEmpty()) return false;
        if (isSeparator(path.charAt(0)) || isSeparator(path.charAt(path.length() - 1))) {
            return false;
        }
        if (path.indexOf('\\') >= 0) return false;
        if (path.length() >= 3 && isAsciiLetter(path.charAt(0))
                && path.charAt(1) == ':' && path.charAt(2) == '/') {
            return false;
        }
        for (String segment : path.split("/", -1)) {
            if (segment.isEmpty() || ".".equals(segment) || "..".equals(segment)) return false;
        }
        return true;
    }

    private static boolean isSeparator(char value) {
        return value == '/' || value == '\\';
    }

    private static boolean isAsciiLetter(char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    }
}
