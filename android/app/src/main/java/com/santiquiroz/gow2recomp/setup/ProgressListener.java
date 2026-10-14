// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

public interface ProgressListener {
    void onProgress(long copiedBytes, long totalBytes, String relativePath);
}
