// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.update;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class VersionCompareTest {
    @Test
    public void multiDigitSegmentsBeatSingleDigitOnes() {
        assertTrue(VersionCompare.compare("0.2.10", "0.2.9") > 0);
    }

    @Test
    public void leadingVIsIgnored() {
        assertEquals(0, VersionCompare.compare("v0.2.2", "0.2.2"));
        assertTrue(VersionCompare.isNewerThan("v0.2.3", "0.2.2"));
    }

    @Test
    public void sameVersionIsNotNewer() {
        assertFalse(VersionCompare.isNewerThan("0.2.2", "0.2.2"));
        assertFalse(VersionCompare.isNewerThan("0.2.2", "v0.2.2"));
    }

    @Test
    public void olderVersionIsNotNewer() {
        assertFalse(VersionCompare.isNewerThan("0.2.1", "0.2.2"));
        assertFalse(VersionCompare.isNewerThan("0.1.99", "0.2.0"));
    }

    @Test
    public void missingTrailingSegmentsCountAsZero() {
        assertEquals(0, VersionCompare.compare("0.2", "0.2.0"));
        assertTrue(VersionCompare.compare("0.2.1", "0.2") > 0);
    }

    @Test
    public void majorAndMinorSegmentsWinOverPatch() {
        assertTrue(VersionCompare.compare("1.0.0", "0.9.99") > 0);
        assertTrue(VersionCompare.compare("0.3.0", "0.2.99") > 0);
    }

    @Test
    public void unknownVersionsAreNeverNewer() {
        assertFalse(VersionCompare.isNewerThan(null, "0.2.2"));
        assertFalse(VersionCompare.isNewerThan("v0.2.3", null));
    }
}
