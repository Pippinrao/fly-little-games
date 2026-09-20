package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

/**
 * Nearby owner creation is delayed until nearby entry. Create failure must
 * leave catalog/single-player usable and surface a real reason instead of
 * crashing. Linkage errors from the shared emulator library must still
 * propagate.
 */
public final class NearbyAvailabilityTest {

    @Test
    public void coldStartDoesNotCreateAnOwner() {
        ScriptedFactory factory = new ScriptedFactory();
        factory.script.add(new FakeOwner());
        NearbyAvailability<FakeOwner> availability = new NearbyAvailability<>(factory);

        assertEquals(0, factory.attempts);
        assertFalse(availability.ownerPresent());
        assertNull(availability.ownerOrNull());
        assertFalse(availability.ready());
    }

    @Test
    public void createFailureLeavesCatalogUsableAndReportsReason() {
        ScriptedFactory factory = new ScriptedFactory();
        factory.script.add(new NearbyAvailability.CreateFailed("nearby_blocked_session_read"));
        NearbyAvailability<FakeOwner> availability = new NearbyAvailability<>(factory);
        boolean catalogUsable = true;

        NearbyAvailability.Status status;
        try {
            status = availability.ensure();
        } catch (RuntimeException crashed) {
            catalogUsable = false;
            throw crashed;
        }

        assertTrue("catalog/single-player must survive nearby create failure", catalogUsable);
        assertFalse(status.ready());
        assertEquals("nearby_blocked_session_read", status.reasonKey());
        assertEquals("nearby_blocked_session_read", availability.reasonKey());
        assertFalse(availability.ownerPresent());
        assertNull(availability.ownerOrNull());
        assertEquals(1, factory.attempts);
        assertEquals(0, factory.live.size());
    }

    @Test
    public void successCreatesOneOwnerAndReusesIt() {
        FakeOwner owner = new FakeOwner();
        ScriptedFactory factory = new ScriptedFactory();
        factory.script.add(owner);
        factory.script.add(new FakeOwner());
        NearbyAvailability<FakeOwner> availability = new NearbyAvailability<>(factory);

        NearbyAvailability.Status first = availability.ensure();
        NearbyAvailability.Status second = availability.ensure();

        assertTrue(first.ready());
        assertTrue(second.ready());
        assertSame(owner, availability.ownerOrNull());
        assertEquals(1, factory.attempts);
        assertEquals(1, factory.live.size());
        assertEquals(0, owner.closeCount);
    }

    @Test
    public void retryAfterFailureDoesNotLeakAndDoesNotCreateASecondLiveOwner() {
        FakeOwner recovered = new FakeOwner();
        ScriptedFactory factory = new ScriptedFactory();
        factory.script.add(new NearbyAvailability.CreateFailed("nearby_blocked_quic"));
        factory.script.add(recovered);
        NearbyAvailability<FakeOwner> availability = new NearbyAvailability<>(factory);

        NearbyAvailability.Status failed = availability.ensure();
        NearbyAvailability.Status ok = availability.ensure();

        assertFalse(failed.ready());
        assertEquals("nearby_blocked_quic", failed.reasonKey());
        assertTrue(ok.ready());
        assertSame(recovered, availability.ownerOrNull());
        assertEquals(2, factory.attempts);
        assertEquals(1, factory.live.size());
        assertEquals(0, recovered.closeCount);
        availability.ensure();
        assertEquals("retry after success must not open a second owner", 2, factory.attempts);
    }

    @Test
    public void nullOwnerIsUnavailableReasonNotACrash() {
        ScriptedFactory factory = new ScriptedFactory();
        factory.script.add(null);
        NearbyAvailability<FakeOwner> availability = new NearbyAvailability<>(factory);

        NearbyAvailability.Status status = availability.ensure();

        assertFalse(status.ready());
        assertEquals("nearby_blocked_session_read", status.reasonKey());
        assertNull(availability.ownerOrNull());
        assertEquals(1, factory.attempts);
    }

    @Test
    public void illegalStateFromFactoryBecomesReasonWithoutCatchingLinkageErrors() {
        NearbyAvailability.Factory<FakeOwner> wrapped = NearbyAvailability.fromIllegalState(
                () -> {
                    throw new IllegalStateException("fly_session_create_v2 failed: -1");
                },
                "nearby_blocked_session_read");
        NearbyAvailability<FakeOwner> availability = new NearbyAvailability<>(wrapped);

        NearbyAvailability.Status status = availability.ensure();
        assertFalse(status.ready());
        assertEquals("nearby_blocked_session_read", status.reasonKey());
        assertTrue(status.reasonKey(), status.reasonKey().startsWith("nearby_blocked_"));
    }

    @Test
    public void missingSharedLibraryIsNotDisguisedAsNearbyUnavailable() {
        NearbyAvailability.Factory<FakeOwner> wrapped = NearbyAvailability.fromIllegalState(
                () -> {
                    throw new UnsatisfiedLinkError("dlopen nescore failed");
                },
                "nearby_blocked_session_read");
        NearbyAvailability<FakeOwner> availability = new NearbyAvailability<>(wrapped);

        try {
            availability.ensure();
            fail("UnsatisfiedLinkError must propagate; single-player is also unusable");
        } catch (UnsatisfiedLinkError error) {
            assertTrue(error.getMessage().contains("nescore"));
        }
        assertFalse(availability.ready());
        assertFalse(availability.ownerPresent());
        assertNull(availability.reasonKey());
    }

    private static final class FakeOwner implements AutoCloseable {
        int closeCount;

        @Override public void close() {
            closeCount++;
        }
    }

    private static final class ScriptedFactory implements NearbyAvailability.Factory<FakeOwner> {
        final List<Object> script = new ArrayList<>();
        final List<FakeOwner> live = new ArrayList<>();
        int attempts;

        @Override public FakeOwner create() throws NearbyAvailability.CreateFailed {
            Object next = script.get(attempts);
            attempts++;
            if (next instanceof NearbyAvailability.CreateFailed) {
                throw (NearbyAvailability.CreateFailed) next;
            }
            if (next instanceof Error) {
                throw (Error) next;
            }
            FakeOwner owner = (FakeOwner) next;
            if (owner != null) live.add(owner);
            return owner;
        }
    }
}
