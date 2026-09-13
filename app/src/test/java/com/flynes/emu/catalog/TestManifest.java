package com.flynes.emu.catalog;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Loads the real bundled-game manifest for JVM tests, so a test can never pass
 * against a hand-written stand-in that the shipped manifest does not match.
 */
public final class TestManifest {

    private TestManifest() {
    }

    /** Repository root, found by walking up to the shared manifest. */
    public static Path repoRoot() {
        Path candidate = Paths.get("").toAbsolutePath();
        for (int depth = 0; depth < 6 && candidate != null; depth++) {
            if (Files.isRegularFile(candidate.resolve("content/assets/builtin-games.json"))) {
                return candidate;
            }
            candidate = candidate.getParent();
        }
        throw new AssertionError(
                "content/assets/builtin-games.json not found above " + Paths.get("").toAbsolutePath());
    }

    /** The shipped manifest, parsed by the production parser. */
    public static BuiltinGames load() {
        try (InputStream in = Files.newInputStream(
                repoRoot().resolve("content/assets/builtin-games.json"))) {
            return BuiltinGames.parse(in);
        } catch (IOException failure) {
            throw new AssertionError("the shipped bundled-game manifest must be readable", failure);
        }
    }
}
