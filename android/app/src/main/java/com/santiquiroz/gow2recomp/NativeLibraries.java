// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

final class NativeLibraries {
    // SDLActivity loads these in order and runs SDL_main from the last one.
    private static final String[] SHELL = {"SDL3", "gow2shell"};
    // The SDK's Android build links SDL3 as libSDL3.so, the same copy whose JNI_OnLoad sets up
    // SDL's Java state, so it loads first and only once.
    private static final String[] GAME = {"SDL3", "rexruntime", "rexgpu-xenos", "gow2"};

    private NativeLibraries() {
    }

    static String[] loadOrder(boolean gameLibrariesPackaged) {
        return (gameLibrariesPackaged ? GAME : SHELL).clone();
    }
}
