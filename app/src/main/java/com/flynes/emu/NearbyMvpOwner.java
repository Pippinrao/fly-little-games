package com.flynes.emu;

/** Process scoped owner for the one Android-host/Harmony-guest MVP session. */
public final class NearbyMvpOwner implements AutoCloseable {
    private NearbyMvpSession session;
    private String gameTitle = "";
    public synchronized String gameTitle() { return gameTitle; }
    public synchronized void gameTitle(String title) { gameTitle = title; }

    public synchronized NearbyMvpSession session() { return session; }

    public synchronized boolean startHost(String ipv4) {
        close();
        if (ipv4 == null) return false;
        NearbyMvpSession replacement = new NearbyMvpSession();
        if (!replacement.host(ipv4)) {
            replacement.close();
            return false;
        }
        session = replacement;
        return true;
    }

    public synchronized boolean active() {
        if (session == null) return false;
        int[] snapshot = session.snapshot();
        return snapshot != null && snapshot.length >= 2 && snapshot[0] != NearbyMvpSession.ENDED;
    }

    @Override public synchronized void close() {
        gameTitle = "";
        if (session != null) {
            session.close();
            session = null;
        }
    }
}
