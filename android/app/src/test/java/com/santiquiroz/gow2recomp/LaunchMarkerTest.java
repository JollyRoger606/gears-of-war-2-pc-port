// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class LaunchMarkerTest {
    @Rule
    public TemporaryFolder temporaryFolder = new TemporaryFolder();

    @Test
    public void markerIsAbsentBeforeFirstLaunch() {
        LaunchMarker marker = new LaunchMarker(temporaryFolder.getRoot());

        assertFalse(marker.exists());
    }

    @Test
    public void createsMarkerAtTheExpectedPathAndSurvivesANewInstance() throws IOException {
        File dataDir = temporaryFolder.newFolder("files");
        LaunchMarker marker = new LaunchMarker(dataDir);

        marker.create();

        assertTrue(new File(dataDir, "launch_in_progress").isFile());
        assertTrue(new LaunchMarker(dataDir).exists());
    }

    @Test
    public void createsMissingDataDirectory() throws IOException {
        File dataDir = new File(temporaryFolder.getRoot(), "new/files");
        LaunchMarker marker = new LaunchMarker(dataDir);

        marker.create();

        assertTrue(marker.exists());
    }

    @Test
    public void repeatedCreatesAndDeletesAreHarmless() throws IOException {
        LaunchMarker marker = new LaunchMarker(temporaryFolder.getRoot());

        marker.create();
        marker.create();
        marker.delete();
        marker.delete();

        assertFalse(marker.exists());
    }

    @Test(expected = IOException.class)
    public void createReportsAnUnwritableDataPath() throws IOException {
        LaunchMarker marker = new LaunchMarker(temporaryFolder.newFile("not-a-directory"));

        marker.create();
    }
}
