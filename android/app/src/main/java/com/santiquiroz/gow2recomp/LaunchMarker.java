// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

final class LaunchMarker {
    private final File marker;

    LaunchMarker(File dataDir) {
        marker = new File(dataDir, "launch_in_progress");
    }

    boolean exists() {
        return marker.exists();
    }

    void create() throws IOException {
        Files.createDirectories(marker.getParentFile().toPath());
        if (!marker.createNewFile() && !marker.isFile()) {
            throw new IOException("Cannot create launch marker: " + marker);
        }
    }

    void delete() throws IOException {
        Files.deleteIfExists(marker.toPath());
    }
}
