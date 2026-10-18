// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.PowerManager;
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
    static final String EXTRA_LAUNCH_OPTIONS = "com.santiquiroz.gow2recomp.extra.LAUNCH_OPTIONS";
    private static final String TAG = "Gow2Activity";
    private static final int REQUEST_GAME_FOLDER = 0x6702;

    private final ExecutorService installExecutor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean installCancelled = new AtomicBoolean(false);
    private GamePaths paths;
    private GameSetupScreen setupScreen;
    private NetworkPlayLocks networkLocks;
    private LaunchMarker launchMarker;
    private boolean nativeStarted;
    private boolean resumed;

    public boolean requestSustainedPerformanceMode(boolean enable) {
        PowerManager pm = getSystemService(PowerManager.class);
        boolean supported = pm != null && pm.isSustainedPerformanceModeSupported();
        if (supported) {
            runOnUiThread(() -> getWindow().setSustainedPerformanceMode(enable));
        }
        Log.i(TAG, "Sustained performance mode requested: " + enable + ", supported: " + supported);
        return supported;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        paths = GamePaths.of(this);
        launchMarker = new LaunchMarker(paths.dataDir());
        boolean previousLaunchCrashed = launchMarker.exists();
        NativeEnvironment.apply(paths);
        applyDebugLaunchOptions();
        super.onCreate(savedInstanceState);
        networkLocks = new NetworkPlayLocks(this);
        networkLocks.acquire();
        if (mBrokenLibraries || isFinishing()) {
            return;
        }
        if (needsGameSetup()) {
            showGameSetup();
        } else if (shouldShowLaunchOptions(previousLaunchCrashed)) {
            showLaunchOptions(previousLaunchCrashed);
        } else {
            markNativeStarted();
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (nativeStarted || !intent.getBooleanExtra(EXTRA_LAUNCH_OPTIONS, false)) {
            return;
        }
        setIntent(intent);
        if (paths.isGameInstalled()) {
            showLaunchOptions(false);
        }
    }

    @Override
    protected void onResume() {
        resumed = true;
        if (nativeStarted) {
            createLaunchMarker();
        }
        super.onResume();
    }

    @Override
    protected void onPause() {
        resumed = false;
        try {
            launchMarker.delete();
        } catch (IOException e) {
            Log.w(TAG, "could not remove launch marker", e);
        }
        super.onPause();
    }

    private boolean shouldShowLaunchOptions(boolean crashed) {
        return (paths.isGameInstalled() && crashed)
                || getIntent().getBooleanExtra(EXTRA_LAUNCH_OPTIONS, false);
    }

    private void showLaunchOptions(boolean crashed) {
        setupScreen = null;
        LaunchOptionsScreen screen = new LaunchOptionsScreen(this, paths.dataDir(), crashed,
                this::startNativeSide);
        setContentView(screen.view());
    }

    private void markNativeStarted() {
        if (resumed) {
            createLaunchMarker();
        }
        nativeStarted = true;
    }

    private void createLaunchMarker() {
        try {
            launchMarker.create();
        } catch (IOException e) {
            Log.w(TAG, "could not create launch marker", e);
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
        if (networkLocks != null) {
            networkLocks.release();
        }
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
            onGameReady();
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
            onGameReady();
        }
    }

    private void onInstallFailed(String message) {
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (setupScreen != null && !isDestroyed()) {
            setupScreen.showChoice(message);
        }
    }

    private void onGameReady() {
        if (shouldShowLaunchOptions(launchMarker.exists())) {
            showLaunchOptions(launchMarker.exists());
            return;
        }
        startNativeSide();
    }

    private void startNativeSide() {
        Log.i(TAG, "starting the native side with the game in " + paths.gameDir());
        markNativeStarted();
        setupScreen = null;
        setContentView(mLayout);
        mSurface.requestFocus();
    }
}
