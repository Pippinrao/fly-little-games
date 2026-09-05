package com.flynes.emu.app;

/** JVM-testable catalog/scan/settings command surface matching flynes_app.h. */
public interface FlyCatalogCommands {
    int OK = 0;
    int CONFLICT = -9;
    int SOURCE_SCOPE_BUILTIN = 1;
    int SOURCE_SCOPE_USER_DIRECTORY = 2;
    int SOURCE_SCOPE_USER_FILE = 3;
    int SCAN_FULL = 1;
    int SCAN_PARTIAL = 2;
    int SCAN_FATAL = 3;

    int scanBegin(byte[] sourceUuid, int sourceScope);

    int scanAddFile(String relativePath, String displayName, int borrowedFd,
                    byte[] expectedPhysicalSha256);

    int scanCommit(int completeness);

    void scanAbort();

    int favoriteSet(String canonicalId, boolean favorite);

    int markPlayed(String canonicalId);
}
