package com.flynes.emu.save;

import com.flynes.emu.data.RomIdentity;

import java.io.IOException;
import java.util.Optional;

public interface SaveStore {
    void writeAutosave(RomIdentity id, byte[] state, long savedAt) throws IOException;
    Optional<SaveRecord> readAutosave(RomIdentity id) throws IOException;
    void writeBattery(RomIdentity id, byte[] battery) throws IOException;
    byte[] readBattery(RomIdentity id) throws IOException;
}
