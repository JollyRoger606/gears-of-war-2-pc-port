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
        // The runtime keeps its config and logs beside the executable, which for an app is /system/bin.
        set("REX_EXECUTABLE_FOLDER", paths.dataDir());
        // SDL reads its hints from the environment. The runtime's window is resizable and names no
        // orientation, which makes SDL allow every orientation and the phone turns the game upright.
        set("SDL_ORIENTATIONS", "LandscapeLeft LandscapeRight");
    }

    private static void set(String name, File dir) {
        set(name, dir.getAbsolutePath());
    }

    private static void set(String name, String value) {
        try {
            Os.setenv(name, value, true);
            Log.i(TAG, name + "=" + value);
        } catch (ErrnoException e) {
            Log.e(TAG, "cannot set " + name, e);
        }
    }
}
