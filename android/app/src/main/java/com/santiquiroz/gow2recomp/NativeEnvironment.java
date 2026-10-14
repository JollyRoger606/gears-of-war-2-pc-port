// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.File;

final class NativeEnvironment {
    private static final String TAG = "Gow2Activity";

    private NativeEnvironment() {
    }

    static void apply(GamePaths paths) {
        set("HOME", paths.dataDir());
        // ReXGlue keeps per-user data in $XDG_DATA_HOME/<app name> (GetUserFolder, filesystem_posix.cpp).
        set("XDG_DATA_HOME", paths.dataDir());
        // Android has no /tmp; std::filesystem::temp_directory_path() reads TMPDIR.
        set("TMPDIR", paths.cacheDir());
    }

    private static void set(String name, File dir) {
        try {
            Os.setenv(name, dir.getAbsolutePath(), true);
            Log.i(TAG, name + "=" + dir.getAbsolutePath());
        } catch (ErrnoException e) {
            Log.e(TAG, "cannot set " + name, e);
        }
    }
}
