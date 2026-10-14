// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import java.util.Objects;

public final class TreeEntry {
    private final String relativePath;
    private final String documentId;
    private final long sizeBytes;

    public TreeEntry(String relativePath, String documentId, long sizeBytes) {
        this.relativePath = relativePath;
        this.documentId = documentId;
        this.sizeBytes = sizeBytes;
    }

    public String relativePath() {
        return relativePath;
    }

    public String documentId() {
        return documentId;
    }

    public long sizeBytes() {
        return sizeBytes;
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof TreeEntry)) {
            return false;
        }
        TreeEntry that = (TreeEntry) other;
        return sizeBytes == that.sizeBytes
                && Objects.equals(relativePath, that.relativePath)
                && Objects.equals(documentId, that.documentId);
    }

    @Override
    public int hashCode() {
        return Objects.hash(relativePath, documentId, sizeBytes);
    }

    @Override
    public String toString() {
        return "TreeEntry{relativePath=" + relativePath
                + ", documentId=" + documentId
                + ", sizeBytes=" + sizeBytes + "}";
    }
}
