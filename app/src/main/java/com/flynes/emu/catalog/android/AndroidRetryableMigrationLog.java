package com.flynes.emu.catalog.android;

import java.util.ArrayList;
import java.util.Collections;
import java.util.EnumSet;
import java.util.List;
import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.Function;

/** Durable retry log. Old FNCA/prefs stay until the matching kind records one success. */
public final class AndroidRetryableMigrationLog {
    static final String KEY = "migration.log";
    private static final String FIELD = "\u001f";
    private static final String ROW = "\n";

    public enum Kind { CATALOG_FNCA, SETTINGS_SCHEMA4 }

    public record Attempt(Kind kind, boolean success, String detail) {
        public Attempt {
            kind = Objects.requireNonNull(kind, "kind");
            detail = detail == null ? "" : detail;
        }
    }

    private final Function<String, String> get;
    private final BiConsumer<String, String> put;
    private final ArrayList<Attempt> attempts = new ArrayList<>();
    private final EnumSet<Kind> succeeded = EnumSet.noneOf(Kind.class);

    public AndroidRetryableMigrationLog(
            Function<String, String> get, BiConsumer<String, String> put) {
        this.get = Objects.requireNonNull(get, "get");
        this.put = Objects.requireNonNull(put, "put");
        load();
    }

    public void record(Kind kind, boolean success, String detail) {
        Attempt attempt = new Attempt(kind, success, detail);
        attempts.add(attempt);
        if (success) succeeded.add(kind);
        persist();
    }

    public boolean succeeded(Kind kind) {
        return succeeded.contains(Objects.requireNonNull(kind, "kind"));
    }

    public boolean shouldRetry(Kind kind) {
        return !succeeded(kind);
    }

    public List<Attempt> attempts() {
        return Collections.unmodifiableList(attempts);
    }

    private void load() {
        String encoded = get.apply(KEY);
        if (encoded == null || encoded.isEmpty()) return;
        for (String row : encoded.split(ROW, -1)) {
            if (row.isEmpty()) continue;
            String[] fields = row.split(FIELD, -1);
            if (fields.length < 3) continue;
            Kind kind = Kind.valueOf(fields[0]);
            boolean success = "1".equals(fields[1]);
            Attempt attempt = new Attempt(kind, success, fields[2]);
            attempts.add(attempt);
            if (success) succeeded.add(kind);
        }
    }

    private void persist() {
        StringBuilder encoded = new StringBuilder();
        for (Attempt attempt : attempts) {
            if (encoded.length() > 0) encoded.append(ROW);
            encoded.append(attempt.kind().name())
                    .append(FIELD)
                    .append(attempt.success() ? "1" : "0")
                    .append(FIELD)
                    .append(attempt.detail().replace(ROW, " ").replace(FIELD, " "));
        }
        put.accept(KEY, encoded.toString());
    }
}
