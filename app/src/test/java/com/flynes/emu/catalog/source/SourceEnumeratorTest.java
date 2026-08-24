package com.flynes.emu.catalog.source;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.RomSource;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class SourceEnumeratorTest {
    @Test
    public void recursivelyEnumeratesInStableOrderAndKeepsOpaqueLocators() {
        FakeTree tree = new FakeTree("root");
        tree.children.put("root", List.of(
                node("z", "z.nes", false), node("folder", "folder", true),
                node("a", "a.nes", false)));
        tree.children.put("folder", List.of(node("nested", "nested.zip", false)));

        SourceEnumerator.Result result = new SourceEnumerator(16, 20_000).enumerate(
                source(), tree);

        assertEquals(SourceEnumerator.Completeness.FULL, result.completeness());
        assertEquals(3, result.candidateCount());
        assertEquals(List.of("a.nes", "nested.zip", "z.nes"), result.candidates().stream()
                .map(item -> item.displayFilename()).toList());
        assertEquals("opaque://a", result.candidates().get(0).contentLocator());
    }

    @Test
    public void cycleAndDepthOrFileLimitAreFatalAndPrivacySafe() {
        FakeTree cycle = new FakeTree("root");
        cycle.children.put("root", List.of(node("loop", "private-folder", true)));
        cycle.children.put("loop", List.of(node("root", "private-root", true)));
        SourceEnumerator.Result cycled = new SourceEnumerator(16, 20_000)
                .enumerate(source(), cycle);
        assertEquals(SourceEnumerator.Completeness.FATAL, cycled.completeness());
        assertTrue(cycled.toString().contains("OTHER"));
        assertTrue(!cycled.toString().contains("private-folder"));

        FakeTree files = new FakeTree("root");
        files.children.put("root", List.of(
                node("one", "one.nes", false), node("two", "two.nes", false)));
        assertEquals(SourceEnumerator.Completeness.FATAL,
                new SourceEnumerator(16, 1).enumerate(source(), files).completeness());
    }

    private static RomSource source() {
        return new RomSource("source", RomSource.Type.SAF_TREE, "opaque://tree",
                RomSource.PermissionState.GRANTED);
    }

    private static DocumentTreeGateway.DocumentNode node(
            String id, String name, boolean directory) {
        return new DocumentTreeGateway.DocumentNode(id, name, directory, "opaque://" + id);
    }

    private static final class FakeTree implements DocumentTreeGateway {
        final String root;
        final Map<String, List<DocumentNode>> children = new HashMap<>();
        FakeTree(String root) { this.root = root; }
        @Override public String rootDocumentId() { return root; }
        @Override public List<DocumentNode> listChildren(String parentDocumentId) {
            return children.getOrDefault(parentDocumentId, List.of());
        }
        @Override public InputStream open(String contentLocator) {
            return new ByteArrayInputStream(new byte[]{1});
        }
    }
}
