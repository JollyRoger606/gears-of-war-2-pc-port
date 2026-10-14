// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp.setup;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.IOException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

public final class DocumentTreeReader {
    private DocumentTreeReader() {
    }

    public static List<TreeEntry> listFiles(ContentResolver resolver, Uri treeUri)
            throws IOException, GameSetupException {
        List<TreeEntry> entries = new ArrayList<>();
        ArrayDeque<PendingDir> pending = new ArrayDeque<>();
        pending.add(new PendingDir(DocumentsContract.getTreeDocumentId(treeUri), ""));
        drainPending(resolver, treeUri, pending, entries);
        entries.sort(Comparator.comparing(TreeEntry::relativePath));
        return Collections.unmodifiableList(entries);
    }

    private static void drainPending(ContentResolver resolver, Uri treeUri,
            ArrayDeque<PendingDir> pending, List<TreeEntry> entries)
            throws IOException, GameSetupException {
        while (!pending.isEmpty()) {
            PendingDir current = pending.removeFirst();
            listOneDirectory(resolver, treeUri, pending, entries, current);
        }
    }

    private static void listOneDirectory(ContentResolver resolver, Uri treeUri,
            ArrayDeque<PendingDir> pending, List<TreeEntry> entries, PendingDir current)
            throws IOException, GameSetupException {
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                treeUri, current.documentId);
        String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE,
        };
        try (Cursor cursor = resolver.query(childrenUri, projection, null, null, null)) {
            if (cursor == null) {
                throw new IOException("cannot list " + childrenUri);
            }
            readRows(cursor, pending, entries, current.prefix);
        }
    }

    private static void readRows(Cursor cursor, ArrayDeque<PendingDir> pending,
            List<TreeEntry> entries, String prefix) throws GameSetupException {
        int idIndex = cursor.getColumnIndexOrThrow(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID);
        int nameIndex = cursor.getColumnIndexOrThrow(
                DocumentsContract.Document.COLUMN_DISPLAY_NAME);
        int mimeIndex = cursor.getColumnIndexOrThrow(
                DocumentsContract.Document.COLUMN_MIME_TYPE);
        int sizeIndex = cursor.getColumnIndex(
                DocumentsContract.Document.COLUMN_SIZE);
        while (cursor.moveToNext()) {
            handleRow(cursor, pending, entries, prefix, idIndex, nameIndex, mimeIndex, sizeIndex);
        }
    }

    private static void handleRow(Cursor cursor, ArrayDeque<PendingDir> pending,
            List<TreeEntry> entries, String prefix,
            int idIndex, int nameIndex, int mimeIndex, int sizeIndex)
            throws GameSetupException {
        String documentId = cursor.getString(idIndex);
        String name = cursor.getString(nameIndex);
        String mimeType = cursor.getString(mimeIndex);
        if (!SafeName.isSafe(name)) {
            throw new GameSetupException(
                    "The chosen folder contains a file name that cannot be copied safely: " + name);
        }
        if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType)) {
            pending.addLast(new PendingDir(documentId, prefix + name + "/"));
            return;
        }
        entries.add(new TreeEntry(prefix + name, documentId, readSize(cursor, sizeIndex)));
    }

    private static long readSize(Cursor cursor, int sizeIndex) {
        if (sizeIndex < 0 || cursor.isNull(sizeIndex)) {
            return 0L;
        }
        return cursor.getLong(sizeIndex);
    }

    private static final class PendingDir {
        final String documentId;
        final String prefix;

        PendingDir(String documentId, String prefix) {
            this.documentId = documentId;
            this.prefix = prefix;
        }
    }
}
