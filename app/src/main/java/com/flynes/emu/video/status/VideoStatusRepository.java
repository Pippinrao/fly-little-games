package com.flynes.emu.video.status;

import java.util.Objects;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicReference;

/** Thread-safe, process-local runtime facts. Nothing in this repository is persisted. */
public final class VideoStatusRepository {
    public interface Observer { void onStatus(VideoRuntimeStatus status); }
    public interface Subscription extends AutoCloseable { @Override void close(); }

    private static final VideoStatusRepository PROCESS = new VideoStatusRepository();

    private final AtomicReference<VideoRuntimeStatus> current = new AtomicReference<>();
    private final CopyOnWriteArrayList<Registration> observers = new CopyOnWriteArrayList<>();

    public static VideoStatusRepository process() { return PROCESS; }

    public VideoRuntimeStatus current() { return current.get(); }

    public void publish(VideoRuntimeStatus status) {
        VideoRuntimeStatus value = Objects.requireNonNull(status, "status");
        current.set(value);
        for (Registration registration : observers) registration.dispatch(value);
    }

    public void clear() { current.set(null); }

    public Subscription observe(Executor executor, Observer observer) {
        Registration registration = new Registration(
                Objects.requireNonNull(executor, "executor"),
                Objects.requireNonNull(observer, "observer"));
        observers.add(registration);
        VideoRuntimeStatus value = current.get();
        if (value != null) registration.dispatch(value);
        return () -> {
            registration.closed = true;
            observers.remove(registration);
        };
    }

    private static final class Registration {
        final Executor executor;
        final Observer observer;
        volatile boolean closed;
        Registration(Executor executor, Observer observer) {
            this.executor = executor;
            this.observer = observer;
        }
        void dispatch(VideoRuntimeStatus status) {
            executor.execute(() -> {
                if (!closed) observer.onStatus(status);
            });
        }
    }
}
