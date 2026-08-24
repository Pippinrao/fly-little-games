package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.CursorWrapper;
import android.database.MatrixCursor;
import android.net.Uri;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.rule.provider.ProviderTestRule;

import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.source.DocumentTreeGateway;
import com.flynes.emu.catalog.source.SourceEnumerator;

import org.junit.Test;
import org.junit.Rule;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AndroidDocumentTreeGatewayTest {
    @Rule public final ProviderTestRule provider = new ProviderTestRule.Builder(
            CountingProvider.class, "bounded").build();

    @Test
    public void childCursorReadsAtMostBudgetPlusOneAndDoesNotMaterializeOverflow() throws Exception {
        CountingProvider.moves = 0;
        AndroidDocumentTreeGateway gateway = new AndroidDocumentTreeGateway(
                provider.getResolver(), "content://bounded/tree/root");

        DocumentTreeGateway.ChildrenBatch batch = gateway.listChildren("root", 2);

        assertEquals(2, batch.entries().size());
        assertFalse(batch.complete());
        assertEquals(3, CountingProvider.moves);
    }

    @Test
    public void nonTreeLocatorBecomesFatalWithoutProviderWriteAccess() {
        Context context = ApplicationProvider.getApplicationContext();
        RomSource source = new RomSource(
                "source", RomSource.Type.SAF_TREE, "content://provider/not-a-tree",
                RomSource.PermissionState.GRANTED);
        SourceEnumerator.Result result = new SourceEnumerator(16, 20_000).enumerate(
                source, new AndroidDocumentTreeGateway(
                        context.getContentResolver(), source.uri()));
        assertEquals(SourceEnumerator.Completeness.FATAL, result.completeness());
        assertEquals(0, result.candidateCount());
    }

    public static final class CountingProvider extends ContentProvider {
        static int moves;

        @Override public boolean onCreate() { return true; }

        @Override public Cursor query(
                Uri uri, String[] projection, String selection,
                String[] selectionArgs, String sortOrder) {
            MatrixCursor rows = new MatrixCursor(projection);
            for (int index = 0; index < 100; index++) {
                rows.addRow(new Object[]{"id-" + index, "game-" + index + ".nes",
                        "application/octet-stream"});
            }
            return new CursorWrapper(rows) {
                @Override public boolean moveToNext() {
                    moves++;
                    return super.moveToNext();
                }
            };
        }

        @Override public String getType(Uri uri) { return null; }
        @Override public Uri insert(Uri uri, ContentValues values) { return null; }
        @Override public int delete(Uri uri, String selection, String[] selectionArgs) {
            return 0;
        }
        @Override public int update(
                Uri uri, ContentValues values, String selection, String[] selectionArgs) {
            return 0;
        }
    }
}
