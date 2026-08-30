package com.flynes.emu.catalog;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Stream;

/** Guards the pure scanner/launch scope against Java 9+ library calls absent on API 24. */
public final class Api24ProductionGuardTest {
    @Test
    public void touchedProductionScopeUsesApi24AvailableLibraryMethods() throws Exception {
        Path project = Paths.get(System.getProperty("user.dir"));
        Path mainJava = project.resolve("app/src/main/java");
        if (!Files.isDirectory(mainJava)) {
            mainJava = project.resolve("src/main/java");
        }
        assertTrue("production source root must exist", Files.isDirectory(mainJava));

        List<Path> sourceRoots = List.of(
                mainJava.resolve("com/flynes/emu/catalog"),
                mainJava.resolve("com/flynes/emu/launch"));
        List<String> forbidden = List.of(
                "List.of(",
                "List.copyOf(",
                "Map.of(",
                "Map.copyOf(",
                "Set.of(",
                "Set.copyOf(",
                "Optional.isEmpty(",
                ".readAllBytes(",
                ".writeBytes(",
                "HexFormat.",
                ".strip(",
                ".stripLeading(",
                ".stripTrailing(");
        ArrayList<String> violations = new ArrayList<>();
        for (Path root : sourceRoots) {
            try (Stream<Path> paths = Files.walk(root)) {
                paths.filter(path -> path.toString().endsWith(".java")).forEach(path -> {
                    try {
                        String source = new String(Files.readAllBytes(path), StandardCharsets.UTF_8)
                                .replace("DomainValidation.isBlank(", "");
                        if (source.contains(".isBlank(")) {
                            violations.add(path + " uses String.isBlank");
                        }
                        for (String token : forbidden) {
                            if (source.contains(token)) {
                                violations.add(path + " uses " + token);
                            }
                        }
                    } catch (Exception failure) {
                        throw new AssertionError(failure);
                    }
                });
            }
        }
        assertFalse("API 24-incompatible production calls: " + violations,
                violations.size() > 0);
    }
}
