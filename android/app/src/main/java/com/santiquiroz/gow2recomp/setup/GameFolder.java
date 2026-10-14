// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

public final class GameFolder {
    public static final String MAIN_EXECUTABLE = "default.xex";

    private GameFolder() {
    }

    public static boolean containsMainExecutable(String[] names) {
        if (names == null) {
            return false;
        }
        for (String name : names) {
            if (matchesMainExecutable(name)) {
                return true;
            }
        }
        return false;
    }

    private static boolean matchesMainExecutable(String name) {
        if (name == null) {
            return false;
        }
        return MAIN_EXECUTABLE.equalsIgnoreCase(name);
    }
}
