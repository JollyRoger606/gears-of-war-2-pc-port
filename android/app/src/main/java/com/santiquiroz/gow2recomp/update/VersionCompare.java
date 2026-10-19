// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.update;

/**
 * Compares dotted version numbers segment by segment, so {@code 0.2.10} beats {@code 0.2.9}.
 * A leading {@code v} (as in release tags) is ignored, and missing trailing segments count
 * as zero, so {@code 0.2} equals {@code 0.2.0}.
 */
public final class VersionCompare {
    private VersionCompare() {
    }

    /**
     * Returns a negative value when {@code first} is older, zero when both name the same
     * version, and a positive value when {@code first} is newer.
     */
    public static int compare(String first, String second) {
        int[] left = segments(first);
        int[] right = segments(second);
        int length = Math.max(left.length, right.length);
        for (int i = 0; i < length; i++) {
            int difference = segment(left, i) - segment(right, i);
            if (difference != 0) {
                return difference < 0 ? -1 : 1;
            }
        }
        return 0;
    }

    /** Returns true when {@code candidate} names a strictly newer version than {@code current}. */
    public static boolean isNewerThan(String candidate, String current) {
        if (candidate == null || current == null) {
            return false;
        }
        return compare(candidate, current) > 0;
    }

    private static int[] segments(String version) {
        String cleaned = version == null ? "" : version.trim();
        if (cleaned.startsWith("v") || cleaned.startsWith("V")) {
            cleaned = cleaned.substring(1);
        }
        String[] parts = cleaned.split("\\.", -1);
        int[] segments = new int[parts.length];
        for (int i = 0; i < parts.length; i++) {
            segments[i] = leadingNumber(parts[i]);
        }
        return segments;
    }

    private static int segment(int[] segments, int index) {
        return index < segments.length ? segments[index] : 0;
    }

    private static int leadingNumber(String part) {
        String text = part.trim();
        int value = 0;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c < '0' || c > '9') {
                break;
            }
            value = Math.min(Integer.MAX_VALUE, value * 10 + (c - '0'));
        }
        return value;
    }
}
