package com.flynes.emu.catalog.persistence;

import java.io.IOException;

public interface CatalogStateStore {
    /** Returns null when no state exists. Reading must not mutate or repair the backing data. */
    byte[] read() throws IOException;

    /** Implementations must provide all-or-nothing replacement. */
    void writeAtomically(byte[] encoded) throws IOException;
}
