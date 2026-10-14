// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

import com.santiquiroz.gow2recomp.setup.GameInstaller;
import com.santiquiroz.gow2recomp.setup.GameSetupException;

import org.libsdl.app.SDLActivity;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public class Gow2Activity extends SDLActivity implements GameSetupScreen.Actions {
    static final String EXTRA_SKIP_GAME_SETUP = "com.santiquiroz.gow2recomp.extra.SKIP_GAME_SETUP";
    static final String EXTRA_SHOW_WHEN_LOCKED = "com.santiquiroz.gow2recomp.extra.SHOW_WHEN_LOCKED";
    private static final String TAG = "Gow2Activity";
    private static final int REQUEST_GAME_FOLDER = 0x6702;

    private final ExecutorService installExecutor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean installCancelled = new AtomicBoolean(false);
    private GamePaths paths;
    private GameSetupScreen setupScreen;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        paths = GamePaths.of(this);
        NativeEnvironment.apply(paths);
        applyDebugLaunchOptions();
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || isFinishing()) {
            return;
        }
        if (needsGameSetup()) {
            showGameSetup();
        }
    }

    @Override
    protected String[] getLibraries() {
        return NativeLibraries.loadOrder(BuildConfig.GAME_LIBRARIES);
    }

    @Override
    protected String[] getArguments() {
        return new String[] {"--game_data_root=" + paths.gameDir().getAbsolutePath()};
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_GAME_FOLDER) {
            onGameFolderChosen(chosenTree(resultCode, data));
        }
    }

    @Override
    protected void onDestroy() {
        installCancelled.set(true);
        installExecutor.shutdownNow();
        super.onDestroy();
    }

    @Override
    public void onChooseFolder() {
        try {
            startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_GAME_FOLDER);
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "no folder picker on this device", e);
            setupScreen.showChoice(getString(R.string.setup_no_picker));
        }
    }

    @Override
    public void onCheckAgain() {
        if (paths.isGameInstalled()) {
            startNativeSide();
            return;
        }
        setupScreen.showChoice(getString(R.string.setup_still_missing, paths.gameDir().getAbsolutePath()));
    }

    private boolean needsGameSetup() {
        return !paths.isGameInstalled() && !isSetupSkipRequested();
    }

    private boolean isSetupSkipRequested() {
        // Debug builds only: lets the native side start on a device that has no game files.
        return BuildConfig.DEBUG && getIntent().getBooleanExtra(EXTRA_SKIP_GAME_SETUP, false);
    }

    private void applyDebugLaunchOptions() {
        // Debug builds only: lets an unattended adb run on a locked phone show the activity.
        if (!BuildConfig.DEBUG || !getIntent().getBooleanExtra(EXTRA_SHOW_WHEN_LOCKED, false)) {
            return;
        }
        setShowWhenLocked(true);
        setTurnScreenOn(true);
    }

    private void showGameSetup() {
        Log.i(TAG, "default.xex not found in " + paths.gameDir() + ", showing the game setup");
        setupScreen = new GameSetupScreen(this, paths.gameDir(), this);
        // SDL's layout is detached, so its surface is never created and the native thread does not start.
        setContentView(setupScreen.view());
    }

    private static Uri chosenTree(int resultCode, Intent data) {
        if (resultCode != RESULT_OK || data == null) {
            return null;
        }
        return data.getData();
    }

    private void onGameFolderChosen(Uri treeUri) {
        if (setupScreen == null) {
            return;
        }
        if (treeUri == null) {
            setupScreen.showChoice(getString(R.string.setup_no_folder));
            return;
        }
        Log.i(TAG, "copying the game files from " + treeUri);
        setupScreen.showCopying();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        installExecutor.execute(() -> installFrom(treeUri));
    }

    private void installFrom(Uri treeUri) {
        try {
            GameInstaller.install(getContentResolver(), treeUri, paths.gameDir(), paths.stagingDir(),
                    this::postProgress, installCancelled);
            runOnUiThread(this::onInstallFinished);
        } catch (GameSetupException e) {
            Log.w(TAG, "game folder rejected: " + e.getMessage());
            runOnUiThread(() -> onInstallFailed(e.getMessage()));
        } catch (IOException | RuntimeException e) {
            Log.e(TAG, "game copy failed", e);
            runOnUiThread(() -> onInstallFailed(getString(R.string.setup_copy_failed, e.getMessage())));
        }
    }

    private void postProgress(long copiedBytes, long totalBytes, String relativePath) {
        runOnUiThread(() -> showProgress(copiedBytes, totalBytes, relativePath));
    }

    private void showProgress(long copiedBytes, long totalBytes, String relativePath) {
        if (setupScreen != null) {
            setupScreen.showProgress(copiedBytes, totalBytes, relativePath);
        }
    }

    private void onInstallFinished() {
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Log.i(TAG, "game files copied to " + paths.gameDir());
        if (!isDestroyed()) {
            startNativeSide();
        }
    }

    private void onInstallFailed(String message) {
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (setupScreen != null && !isDestroyed()) {
            setupScreen.showChoice(message);
        }
    }

    private void startNativeSide() {
        Log.i(TAG, "starting the native side with the game in " + paths.gameDir());
        setupScreen = null;
        setContentView(mLayout);
        mSurface.requestFocus();
    }
}
