// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

public final class SafeName {
    private SafeName() {
    }

    public static boolean isSafe(String name) {
        if (name == null || name.isEmpty()) {
            return false;
        }
        if (name.equals(".") || name.equals("..")) {
            return false;
        }
        return hasNoForbiddenChars(name);
    }

    private static boolean hasNoForbiddenChars(String name) {
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c == '/' || c == '\\' || c == '\0') {
                return false;
            }
        }
        return true;
    }
}
