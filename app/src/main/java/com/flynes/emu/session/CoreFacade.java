package com.flynes.emu.session;

public interface CoreFacade {
    boolean create();
    int loadRom(byte[] rom);
    void setInput(int mask);
    default long setInputVersioned(int mask) { setInput(mask); return 0L; }
    int runOneFrame();
    byte[] saveState();
    int loadState(byte[] state);
    void destroy();
}
