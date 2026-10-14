// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class ByteSizeTest {
    @Test
    public void formatsZeroBytes() {
        assertEquals("0 B", ByteSize.format(0));
    }

    @Test
    public void formatsBytesBelowOneKilobyte() {
        assertEquals("1023 B", ByteSize.format(1023));
    }

    @Test
    public void formatsFractionalKilobytes() {
        assertEquals("1.5 KB", ByteSize.format(1536));
    }

    @Test
    public void formatsWholeMegabytes() {
        assertEquals("5.0 MB", ByteSize.format(5 * 1024 * 1024));
    }

    @Test
    public void formatsFractionalGigabytes() {
        assertEquals("8.50 GB", ByteSize.format(8L * 1024 * 1024 * 1024 + 512L * 1024 * 1024));
    }

    @Test
    public void treatsNegativeAsZero() {
        assertEquals("0 B", ByteSize.format(-5));
    }
}
