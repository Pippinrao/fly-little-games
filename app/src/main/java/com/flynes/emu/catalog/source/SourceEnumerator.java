package com.flynes.emu.catalog.source;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.scan.PackageCandidate;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Deterministic bounded recursive source enumeration. */
public final class SourceEnumerator {
    private final int maxDepth;
    private final int maxFiles;

    public SourceEnumerator(int maxDepth, int maxFiles) {
        if (maxDepth < 0 || maxFiles <= 0) throw new IllegalArgumentException("invalid limits");
        this.maxDepth = maxDepth;
        this.maxFiles = maxFiles;
    }

    public Result enumerate(RomSource source, DocumentTreeGateway gateway) {
        DomainValidation.requireNonNull(source, "source");
        DomainValidation.requireNonNull(gateway, "document tree gateway");
        if (source.type() != RomSource.Type.SAF_TREE) {
            throw new IllegalArgumentException("only SAF tree sources can be enumerated");
        }
        ArrayList<PackageCandidate> candidates = new ArrayList<>();
        try {
            String root = DomainValidation.requireNonBlank(
                    gateway.rootDocumentId(), "root document id");
            HashSet<String> visited = new HashSet<>();
            visited.add(root);
            visit(gateway, root, 0, visited, candidates);
            return new Result(candidates, Completeness.FULL, Collections.emptyList());
        } catch (SecurityException failure) {
            return fatal(source.id(), candidates, ScanIssue.Code.PERMISSION_REVOKED);
        } catch (IOException failure) {
            return fatal(source.id(), candidates, ScanIssue.Code.IO_ERROR);
        } catch (RuntimeException failure) {
            return fatal(source.id(), candidates, ScanIssue.Code.OTHER);
        }
    }

    private void visit(
            DocumentTreeGateway gateway,
            String parent,
            int depth,
            Set<String> visited,
            List<PackageCandidate> candidates) throws IOException {
        ArrayList<DocumentTreeGateway.DocumentNode> children =
                new ArrayList<>(gateway.listChildren(parent));
        children.sort(Comparator.comparing(DocumentTreeGateway.DocumentNode::documentId)
                .thenComparing(DocumentTreeGateway.DocumentNode::displayName));
        for (DocumentTreeGateway.DocumentNode child : children) {
            if (!visited.add(child.documentId())) throw new TraversalFailure();
            if (child.directory()) {
                if (depth >= maxDepth) throw new TraversalFailure();
                visit(gateway, child.documentId(), depth + 1, visited, candidates);
            } else {
                if (candidates.size() >= maxFiles) throw new TraversalFailure();
                candidates.add(new PackageCandidate(
                        child.documentId(), child.displayName(), child.contentLocator(),
                        () -> gateway.open(child.contentLocator())));
            }
        }
    }

    private static Result fatal(
            String sourceId,
            List<PackageCandidate> candidates,
            ScanIssue.Code code) {
        return new Result(candidates, Completeness.FATAL, Collections.singletonList(new ScanIssue(
                code, ScanIssue.Severity.FATAL, sourceId, null)));
    }

    public enum Completeness { FULL, FATAL }

    public record Result(
            List<PackageCandidate> candidates,
            Completeness completeness,
            List<ScanIssue> issues) {
        public Result {
            candidates = DomainValidation.immutableList(candidates, "enumerated candidates");
            completeness = DomainValidation.requireNonNull(completeness, "completeness");
            issues = DomainValidation.immutableList(issues, "enumeration issues");
        }

        public int candidateCount() { return candidates.size(); }
    }

    private static final class TraversalFailure extends RuntimeException {
    }
}
