// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import java.util.Arrays;

import org.junit.Test;

public class NativeLibrariesTest {
    @Test
    public void shellLoadsSdlThenThePlaceholderMain() {
        assertArrayEquals(new String[] {"SDL3", "gow2shell"}, NativeLibraries.loadOrder(false));
    }

    @Test
    public void gameLoadsRuntimeThenGpuThenGameMainLast() {
        assertArrayEquals(new String[] {"rexruntime", "rexgpu-xenos", "gow2"}, NativeLibraries.loadOrder(true));
    }

    @Test
    public void gameDoesNotLoadASecondSdl() {
        assertFalse(Arrays.asList(NativeLibraries.loadOrder(true)).contains("SDL3"));
    }

    @Test
    public void callersCannotChangeTheSharedList() {
        NativeLibraries.loadOrder(false)[0] = "changed";
        assertEquals("SDL3", NativeLibraries.loadOrder(false)[0]);
    }
}
