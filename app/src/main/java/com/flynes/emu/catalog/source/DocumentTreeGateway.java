package com.flynes.emu.catalog.source;

import com.flynes.emu.catalog.DomainValidation;

import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/** Provider-neutral recursive document tree seam. */
public interface DocumentTreeGateway {
    String rootDocumentId() throws IOException, SecurityException;
    List<DocumentNode> listChildren(String parentDocumentId) throws IOException, SecurityException;
    InputStream open(String contentLocator) throws IOException, SecurityException;

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
