// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

final class NativeLibraries {
    // SDLActivity loads these in order and runs SDL_main from the last one.
    private static final String[] SHELL = {"SDL3", "gow2shell"};
    // librexruntime.so links SDL3 statically and exports SDL's JNI entry points, so a separate
    // libSDL3.so would be a second SDL; prepend "SDL3" only if the SDK switches to SDL_SHARED.
    private static final String[] GAME = {"rexruntime", "rexgpu-xenos", "gow2"};

    private NativeLibraries() {
    }

    static String[] loadOrder(boolean gameLibrariesPackaged) {
        return (gameLibrariesPackaged ? GAME : SHELL).clone();
    }
}
