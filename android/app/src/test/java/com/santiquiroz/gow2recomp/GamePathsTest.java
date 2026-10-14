// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class GamePathsTest {
    @Rule
    public TemporaryFolder temporaryFolder = new TemporaryFolder();

    @Test
    public void gameAndStagingFoldersLiveInTheDataFolder() throws IOException {
        File data = temporaryFolder.newFolder("files");
        GamePaths paths = new GamePaths(data, temporaryFolder.newFolder("cache"));

        assertEquals(new File(data, "game"), paths.gameDir());
        assertEquals(new File(data, "game.incoming"), paths.stagingDir());
    }

    @Test
    public void gameIsNotInstalledWithoutTheGameFolder() throws IOException {
        GamePaths paths = new GamePaths(temporaryFolder.newFolder("files"), temporaryFolder.newFolder("cache"));

        assertFalse(paths.isGameInstalled());
    }

    @Test
    public void gameIsNotInstalledWhenDefaultXexIsMissing() throws IOException {
        GamePaths paths = new GamePaths(temporaryFolder.newFolder("files"), temporaryFolder.newFolder("cache"));
        assertTrue(paths.gameDir().mkdirs());
        assertTrue(new File(paths.gameDir(), "content.bin").createNewFile());

        assertFalse(paths.isGameInstalled());
    }

    @Test
    public void gameIsInstalledWhenDefaultXexIsPresentInAnyCase() throws IOException {
        GamePaths paths = new GamePaths(temporaryFolder.newFolder("files"), temporaryFolder.newFolder("cache"));
        assertTrue(paths.gameDir().mkdirs());
        assertTrue(new File(paths.gameDir(), "DEFAULT.XEX").createNewFile());

        assertTrue(paths.isGameInstalled());
    }
}
