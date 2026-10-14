// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import java.util.Locale;

public final class ByteSize {
    private ByteSize() {
    }

    public static String format(long bytes) {
        long value = Math.max(0L, bytes);
        if (value < 1024L) {
            return value + " B";
        }
        if (value < 1024L * 1024L) {
            return String.format(Locale.ROOT, "%.1f KB", value / 1024.0);
        }
        if (value < 1024L * 1024L * 1024L) {
            return String.format(Locale.ROOT, "%.1f MB", value / (1024.0 * 1024.0));
        }
        return String.format(Locale.ROOT, "%.2f GB", value / (1024.0 * 1024.0 * 1024.0));
    }
}
