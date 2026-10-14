// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class GameSetupScreenProgressTest {
    private static final long TEN_GIB = 10L * 1024 * 1024 * 1024;

    @Test
    public void scalesCopiedBytesToTheProgressBarRange() {
        assertEquals(GameSetupScreen.PROGRESS_SCALE / 4, GameSetupScreen.scaledProgress(TEN_GIB / 4, TEN_GIB));
    }

    @Test
    public void unknownTotalShowsAFullBar() {
        assertEquals(GameSetupScreen.PROGRESS_SCALE, GameSetupScreen.scaledProgress(0, 0));
    }

    @Test
    public void neverExceedsTheRange() {
        assertEquals(GameSetupScreen.PROGRESS_SCALE, GameSetupScreen.scaledProgress(TEN_GIB * 2, TEN_GIB));
    }

    @Test
    public void negativeCopiedBytesShowAnEmptyBar() {
        assertEquals(0, GameSetupScreen.scaledProgress(-1, TEN_GIB));
    }
}
