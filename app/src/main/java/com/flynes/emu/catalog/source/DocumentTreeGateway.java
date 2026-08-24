package com.flynes.emu.catalog.source;

import com.flynes.emu.catalog.DomainValidation;

import java.io.IOException;
import java.io.InputStream;

/** Provider-neutral recursive document tree seam. */
public interface DocumentTreeGateway {
    String rootDocumentId() throws IOException, SecurityException;
    ChildrenBatch listChildren(String parentDocumentId, int remainingNodeBudget)
            throws IOException, SecurityException;
    InputStream open(String contentLocator) throws IOException, SecurityException;

    record ChildrenBatch(java.util.List<DocumentNode> entries, boolean complete) {
        public ChildrenBatch {
            entries = DomainValidation.immutableList(entries, "document children");
        }
    }

    record DocumentNode(
            String documentId,
            String displayName,
            boolean directory,
            String contentLocator) {
        public DocumentNode {
            documentId = DomainValidation.requireNonBlank(documentId, "document id");
            displayName = DomainValidation.requireNonBlank(displayName, "document display name");
            contentLocator = DomainValidation.requireNonBlank(contentLocator, "content locator");
        }
    }
}
