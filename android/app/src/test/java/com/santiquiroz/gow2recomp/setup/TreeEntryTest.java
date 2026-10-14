// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

import org.junit.Test;

public class TreeEntryTest {
    @Test
    public void equalValuesAreEqual() {
        TreeEntry first = new TreeEntry("dir/file.bin", "doc:1", 42L);
        TreeEntry second = new TreeEntry("dir/file.bin", "doc:1", 42L);
        assertEquals(first, second);
    }

    @Test
    public void equalValuesShareHashCode() {
        TreeEntry first = new TreeEntry("dir/file.bin", "doc:1", 42L);
        TreeEntry second = new TreeEntry("dir/file.bin", "doc:1", 42L);
        assertEquals(first.hashCode(), second.hashCode());
    }

    @Test
    public void differentSizesAreNotEqual() {
        TreeEntry first = new TreeEntry("dir/file.bin", "doc:1", 42L);
        TreeEntry second = new TreeEntry("dir/file.bin", "doc:1", 43L);
        assertNotEquals(first, second);
    }

    @Test
    public void exposesAccessors() {
        TreeEntry entry = new TreeEntry("a/b.bin", "doc:7", 9L);
        assertEquals("a/b.bin", entry.relativePath());
        assertEquals("doc:7", entry.documentId());
        assertEquals(9L, entry.sizeBytes());
    }
}
