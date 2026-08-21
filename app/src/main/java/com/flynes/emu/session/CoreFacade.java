package com.flynes.emu.session;

public interface CoreFacade {
    boolean create();
    int loadRom(byte[] rom);
    void setInput(int mask);
    int runOneFrame();
    byte[] saveState();
    int loadState(byte[] state);
    void destroy();
}
