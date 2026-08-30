package com.flynes.emu.catalog.android;

import android.content.ContentResolver;
import android.content.Intent;
import android.content.UriPermission;
import android.net.Uri;

import com.flynes.emu.catalog.source.ReadPermissionGateway;

/** Android persisted SAF gateway that never requests or retains write access. */
public final class PersistedReadPermissionGateway implements ReadPermissionGateway {
    public static final int PICKER_FLAGS = Intent.FLAG_GRANT_READ_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
            | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION;

    private final ContentResolver resolver;

    public PersistedReadPermissionGateway(ContentResolver resolver) {
        if (resolver == null) throw new NullPointerException("resolver");
        this.resolver = resolver;
    }

    public static Intent pickerIntent() {
        return new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).setFlags(PICKER_FLAGS);
    }

    @Override
    public void takeRead(String locator, int resultFlags) throws PermissionFailure {
        if ((resultFlags & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0) {
            throw new PermissionFailure(PermissionFailure.Code.READ_NOT_GRANTED);
        }
        try {
            resolver.takePersistableUriPermission(
                    Uri.parse(locator), Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException failure) {
            throw new PermissionFailure(PermissionFailure.Code.TAKE_FAILED, failure);
        }
    }

    @Override
    public boolean hasPersistedRead(String locator) {
        Uri expected = Uri.parse(locator);
        for (UriPermission permission : resolver.getPersistedUriPermissions()) {
            if (permission.isReadPermission() && expected.equals(permission.getUri())) return true;
        }
        return false;
    }

    @Override
    public void releaseRead(String locator) throws PermissionFailure {
        if (!hasPersistedRead(locator)) return;
        try {
            resolver.releasePersistableUriPermission(
                    Uri.parse(locator), Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException failure) {
            throw new PermissionFailure(PermissionFailure.Code.RELEASE_FAILED, failure);
        }
    }
}
