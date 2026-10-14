// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import android.content.Context;

import com.santiquiroz.gow2recomp.setup.GameFolder;

import java.io.File;

final class GamePaths {
    private static final String GAME_FOLDER = "game";
    private static final String STAGING_FOLDER = "game.incoming";

    private final File dataDir;
    private final File cacheDir;

    GamePaths(File dataDir, File cacheDir) {
        this.dataDir = dataDir;
        this.cacheDir = cacheDir;
    }

    static GamePaths of(Context context) {
        File external = context.getExternalFilesDir(null);
        // Android/data/<package>/files is reachable over USB; fall back to private storage when unmounted.
        File dataDir = external != null ? external : context.getFilesDir();
        return new GamePaths(dataDir, context.getCacheDir());
    }

    File dataDir() {
        return dataDir;
    }

    File cacheDir() {
        return cacheDir;
    }

    File gameDir() {
        return new File(dataDir, GAME_FOLDER);
    }

    File stagingDir() {
        return new File(dataDir, STAGING_FOLDER);
    }

    boolean isGameInstalled() {
        return GameFolder.containsMainExecutable(gameDir().list());
    }
}
