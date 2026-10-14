// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import static org.junit.Assert.assertEquals;

import java.io.File;
import java.io.IOException;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class TargetPathTest {
    @Rule
    public TemporaryFolder folder = new TemporaryFolder();

    @Test
    public void resolvesNestedPathUnderRoot() throws IOException {
        File root = folder.getRoot();
        File resolved = TargetPath.resolve(root, "dir/file.bin");
        assertEquals(new File(root, "dir" + File.separator + "file.bin").getPath(),
                resolved.getPath());
    }

    @Test(expected = IOException.class)
    public void rejectsParentDirectorySegments() throws IOException {
        TargetPath.resolve(folder.getRoot(), "../x");
    }

    @Test(expected = IOException.class)
    public void rejectsNestedParentTraversal() throws IOException {
        TargetPath.resolve(folder.getRoot(), "a/../../x");
    }

    @Test(expected = IOException.class)
    public void rejectsEmptyPath() throws IOException {
        TargetPath.resolve(folder.getRoot(), "");
    }

    @Test(expected = IOException.class)
    public void rejectsEmptySegment() throws IOException {
        TargetPath.resolve(folder.getRoot(), "a//b");
    }

    @Test(expected = IOException.class)
    public void rejectsAbsolutePath() throws IOException {
        TargetPath.resolve(folder.getRoot(), "/abs");
    }

    @Test(expected = IOException.class)
    public void rejectsCurrentDirectorySegment() throws IOException {
        TargetPath.resolve(folder.getRoot(), "a/./b");
    }
}
