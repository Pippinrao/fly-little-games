package com.flynes.emu.session;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.input.InputBits;

import org.junit.Test;

public final class EmulationSessionTest {
    @Test
    public void stopBeforeStartCannotResurrectSession() throws Exception {
        for (int i = 0; i < 1000; i++) {
            FakeCore core = new FakeCore();
            EmulationSession session = new EmulationSession(core, Runnable::run);

            session.stop().get();
            SessionResult result = session.start().get();

            assertFalse(result.isSuccess());
            assertEquals(SessionState.EMPTY, session.state());
            assertEquals(0, core.runCalls);
        }
    }

    @Test
    public void pausePublishesZeroInputBeforeStoppingClock() throws Exception {
        FakeCore core = new FakeCore();
        EmulationSession session = new EmulationSession(core, Runnable::run);
        session.load(new byte[]{'N', 'E', 'S', 0x1A}).get();
        session.start().get();
        session.setInput(InputBits.A);

        SessionResult result = session.pause().get();

        assertTrue(result.isSuccess());
        assertEquals(0, core.lastInput);
        assertEquals(SessionState.PAUSED, session.state());
    }

    @Test
    public void resumeIsLegalOnlyAfterPause() throws Exception {
        FakeCore core = new FakeCore();
        EmulationSession session = new EmulationSession(core, Runnable::run);
        session.load(new byte[]{'N', 'E', 'S', 0x1A}).get();

        assertFalse(session.resume().get().isSuccess());
        session.start().get();
        session.pause().get();
        assertTrue(session.resume().get().isSuccess());
        assertEquals(SessionState.RUNNING, session.state());
    }

    private static final class FakeCore implements CoreFacade {
        int runCalls;
        int lastInput;
        boolean created;

        @Override public boolean create() { created = true; return true; }
        @Override public int loadRom(byte[] rom) { return created ? 0 : -3; }
        @Override public void setInput(int mask) { lastInput = mask; }
        @Override public int runOneFrame() { runCalls++; return 0; }
        @Override public byte[] saveState() { return new byte[0]; }
        @Override public int loadState(byte[] state) { return 0; }
        @Override public void destroy() { created = false; }
    }
}
