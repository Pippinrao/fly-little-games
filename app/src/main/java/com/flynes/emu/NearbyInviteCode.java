package com.flynes.emu;

/**
 * Joiner-side input rule (UI contract nearby_ui_v1, shared
 * flynes::session::parse_invite_code): exactly six ASCII digits after trimming
 * ASCII whitespace, leading zeros preserved, no silent truncation - anything
 * else is not a code and must not produce a lookup request (C05).
 */
public final class NearbyInviteCode {
    private NearbyInviteCode() { }

    /** Returns the trimmed six-digit code, or null when the input is not one. */
    public static String normalize(String raw) {
        if (raw == null) return null;
        String trimmed = raw.trim();
        if (trimmed.length() != NearbyInviteHostState.CODE_LENGTH) return null;
        for (int i = 0; i < trimmed.length(); i++) {
            char c = trimmed.charAt(i);
            if (c < '0' || c > '9') return null;
        }
        return trimmed;
    }
}
