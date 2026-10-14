// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class GameFolderTest {
    @Test
    public void matchesMainExecutableIgnoringCase() {
        assertTrue(GameFolder.containsMainExecutable(new String[]{"DEFAULT.XEX"}));
    }

    @Test
    public void matchesMainExecutableAmongOthers() {
        assertTrue(GameFolder.containsMainExecutable(new String[]{"foo", "default.xex"}));
    }

    @Test
    public void rejectsNullArray() {
        assertFalse(GameFolder.containsMainExecutable(null));
    }

    @Test
    public void rejectsEmptyArray() {
        assertFalse(GameFolder.containsMainExecutable(new String[]{}));
    }

    @Test
    public void rejectsSimilarExtension() {
        assertFalse(GameFolder.containsMainExecutable(new String[]{"default.xex.bak"}));
    }

    @Test
    public void ignoresNullElements() {
        assertFalse(GameFolder.containsMainExecutable(new String[]{null}));
    }
}
