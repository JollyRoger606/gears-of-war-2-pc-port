// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class SafeNameTest {
    @Test
    public void acceptsPlainFileName() {
        assertTrue(SafeName.isSafe("default.xex"));
    }

    @Test
    public void acceptsDirectoryName() {
        assertTrue(SafeName.isSafe("Content"));
    }

    @Test
    public void acceptsNameWithSpace() {
        assertTrue(SafeName.isSafe("a b.bin"));
    }

    @Test
    public void rejectsNull() {
        assertFalse(SafeName.isSafe(null));
    }

    @Test
    public void rejectsEmptyName() {
        assertFalse(SafeName.isSafe(""));
    }

    @Test
    public void rejectsCurrentDirectorySegment() {
        assertFalse(SafeName.isSafe("."));
    }

    @Test
    public void rejectsParentDirectorySegments() {
        assertFalse(SafeName.isSafe(".."));
    }

    @Test
    public void rejectsForwardSlash() {
        assertFalse(SafeName.isSafe("a/b"));
    }

    @Test
    public void rejectsBackslash() {
        assertFalse(SafeName.isSafe("a\\b"));
    }

    @Test
    public void rejectsEmbeddedNull() {
        assertFalse(SafeName.isSafe("x" + (char) 0 + "y"));
    }
}
