package com.flynes.emu.gamecenter;

import com.flynes.emu.catalog.BuiltinGames;

/** Projects the shared manifest's versioned profiles into the Game Center registry. */
public final class BuiltinMultiplayerCapabilities {
    private BuiltinMultiplayerCapabilities() {}

    public static GameCenterState.MultiplayerCapabilityRegistry from(BuiltinGames games) {
        long version = games == null ? 0L : games.multiplayerProfileVersion();
        GameCenterState.MultiplayerCapabilityRegistry registry =
                new GameCenterState.MultiplayerCapabilityRegistry(version);
        if (games == null) return registry;
        for (BuiltinGames.Entry entry : games.all()) {
            GameCenterState.MultiplayerEligibility eligibility;
            switch (entry.multiplayerEligibility) {
                case SUPPORTED:
                    eligibility = GameCenterState.MultiplayerEligibility.SUPPORTED;
                    break;
                case UNSUPPORTED:
                    eligibility = GameCenterState.MultiplayerEligibility.UNSUPPORTED;
                    break;
                default:
                    eligibility = GameCenterState.MultiplayerEligibility.UNKNOWN;
                    break;
            }
            registry.put(entry.canonicalId, eligibility, entry.multiplayerProfileVersion);
        }
        return registry;
    }
}
