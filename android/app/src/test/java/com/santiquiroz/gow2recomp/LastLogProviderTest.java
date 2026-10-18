// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeNoException;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class LastLogProviderTest {
    @Rule
    public TemporaryFolder temporaryFolder = new TemporaryFolder();

    @Test
    public void missingLogsDirectoryHasNoLastLog() throws IOException {
        assertNull(LastLogProvider.newest(temporaryFolder.getRoot()));
    }

    @Test
    public void directoryWithoutMatchingLogsHasNoLastLog() throws IOException {
        temporaryFolder.newFolder("logs");
        temporaryFolder.newFile("logs/notes.txt");

        assertNull(LastLogProvider.newest(temporaryFolder.getRoot()));
    }

    @Test
    public void highestNumericSuffixWinsRegardlessOfModificationTime() throws IOException {
        temporaryFolder.newFolder("logs");
        File nine = temporaryFolder.newFile("logs/gow2_9.log");
        File ten = temporaryFolder.newFile("logs/gow2_10.log");
        assertTrue(nine.setLastModified(2_000_000_000L));
        assertTrue(ten.setLastModified(1_000_000_000L));

        assertEquals(ten, LastLogProvider.newest(temporaryFolder.getRoot()));
    }

    @Test
    public void ignoresMalformedNamesAndDirectoriesWithLogNames() throws IOException {
        temporaryFolder.newFolder("logs");
        File valid = temporaryFolder.newFile("logs/gow2_1.log");
        temporaryFolder.newFile("logs/gow2_999.log.bak");
        temporaryFolder.newFile("logs/gow2_999x.log");
        temporaryFolder.newFile("logs/gow2_-999.log");
        temporaryFolder.newFile("logs/gow2_.log");
        temporaryFolder.newFile("logs/other_999.log");
        temporaryFolder.newFolder("logs", "gow2_999.log");

        assertEquals(valid, LastLogProvider.newest(temporaryFolder.getRoot()));
    }

    @Test
    public void supportsNumbersLargerThanLongMaximum() throws IOException {
        temporaryFolder.newFolder("logs");
        temporaryFolder.newFile("logs/gow2_9223372036854775807.log");
        File highest = temporaryFolder.newFile("logs/gow2_9223372036854775809.log");
        temporaryFolder.newFile("logs/gow2_9223372036854775808.log");

        assertEquals(highest, LastLogProvider.newest(temporaryFolder.getRoot()));
    }

    @Test
    public void ignoresLogSymlinksWhoseTargetsAreOutsideTheLogsDirectory() throws IOException {
        File logs = temporaryFolder.newFolder("logs");
        File valid = temporaryFolder.newFile("logs/gow2_1.log");
        File outside = temporaryFolder.newFile("private.log");
        try {
            Files.createSymbolicLink(new File(logs, "gow2_999.log").toPath(), outside.toPath());
        } catch (IOException | UnsupportedOperationException | SecurityException failure) {
            assumeNoException("Symbolic links are unavailable on this platform", failure);
        }

        assertEquals(valid, LastLogProvider.newest(temporaryFolder.getRoot()));
    }
}
