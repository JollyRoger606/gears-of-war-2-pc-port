// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import java.io.File;
import java.io.IOException;

public final class TargetPath {
    private TargetPath() {
    }

    public static File resolve(File root, String relativePath) throws IOException {
        if (relativePath == null || relativePath.isEmpty()) {
            throw new IOException("unsafe path: " + relativePath);
        }
        checkSegments(relativePath);
        File result = new File(root, relativePath.replace('/', File.separatorChar));
        checkInsideRoot(root, result, relativePath);
        return result;
    }

    private static void checkSegments(String relativePath) throws IOException {
        String[] segments = relativePath.split("/", -1);
        for (String segment : segments) {
            if (!SafeName.isSafe(segment)) {
                throw new IOException("unsafe path: " + relativePath);
            }
        }
    }

    private static void checkInsideRoot(File root, File result, String relativePath) throws IOException {
        String wanted = root.getCanonicalPath() + File.separator;
        if (!result.getCanonicalPath().startsWith(wanted)) {
            throw new IOException("path escapes the target folder: " + relativePath);
        }
    }
}
