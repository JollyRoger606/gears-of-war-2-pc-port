// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import java.util.Arrays;
import java.util.Collections;

import org.junit.Test;

public class NativeLibrariesTest {
    @Test
    public void shellLoadsSdlThenThePlaceholderMain() {
        assertArrayEquals(new String[] {"SDL3", "gow2shell"}, NativeLibraries.loadOrder(false));
    }

    @Test
    public void gameLoadsSdlThenRuntimeThenGpuThenGameMainLast() {
        assertArrayEquals(new String[] {"SDL3", "rexruntime", "rexgpu-xenos", "gow2"}, NativeLibraries.loadOrder(true));
    }

    @Test
    public void gameLoadsExactlyOneSdl() {
        assertEquals(1, Collections.frequency(Arrays.asList(NativeLibraries.loadOrder(true)), "SDL3"));
    }

    @Test
    public void callersCannotChangeTheSharedList() {
        NativeLibraries.loadOrder(false)[0] = "changed";
        assertEquals("SDL3", NativeLibraries.loadOrder(false)[0]);
    }
}
