// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.math.BigInteger;
import java.util.List;

public final class LastLogProvider extends ContentProvider {
    private static final String[] DEFAULT_COLUMNS = {
            OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};

    @Override
    public boolean onCreate() {
        return true;
    }

    static File newest(File dataDir) throws IOException {
        File logs = new File(dataDir, "logs").getCanonicalFile();
        File[] files = logs.listFiles();
        if (files == null) {
            return null;
        }
        File newest = null;
        BigInteger highest = BigInteger.valueOf(-1);
        for (File file : files) {
            if (!isLog(file.getName()) || !file.isFile()) {
                continue;
            }
            File canonical = file.getCanonicalFile();
            if (!logs.equals(canonical.getParentFile())) {
                continue;
            }
            BigInteger number = logNumber(file.getName());
            if (number.compareTo(highest) > 0) {
                highest = number;
                newest = file;
            }
        }
        return newest;
    }

    static Uri uriFor(Context context, File log) throws IOException {
        Uri uri = new Uri.Builder().scheme("content")
                .authority(context.getPackageName() + ".logs")
                .appendPath(log.getName()).build();
        resolve(context, uri);
        return uri;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) {
            throw new FileNotFoundException("Read-only provider");
        }
        try {
            return ParcelFileDescriptor.open(resolve(getContext(), uri),
                    ParcelFileDescriptor.MODE_READ_ONLY);
        } catch (IOException | IllegalArgumentException e) {
            FileNotFoundException failure = new FileNotFoundException(e.getMessage());
            failure.initCause(e);
            throw failure;
        }
    }

    @Override
    public String getType(Uri uri) {
        checkedFile(uri);
        return "text/plain";
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder) {
        File file = checkedFile(uri);
        String[] columns = projection == null ? DEFAULT_COLUMNS : projection;
        MatrixCursor cursor = new MatrixCursor(columns, 1);
        Object[] values = new Object[columns.length];
        for (int i = 0; i < columns.length; i++) {
            values[i] = columnValue(columns[i], file);
        }
        cursor.addRow(values);
        return cursor;
    }

    private static Object columnValue(String column, File file) {
        if (OpenableColumns.DISPLAY_NAME.equals(column)) {
            return file.getName();
        }
        if (OpenableColumns.SIZE.equals(column)) {
            return file.length();
        }
        return null;
    }

    private File checkedFile(Uri uri) {
        try {
            return resolve(getContext(), uri);
        } catch (IOException e) {
            throw new IllegalArgumentException("Invalid log URI", e);
        }
    }

    private static File resolve(Context context, Uri uri) throws IOException {
        if (!"content".equals(uri.getScheme())
                || !(context.getPackageName() + ".logs").equals(uri.getAuthority())) {
            throw new IllegalArgumentException("Invalid log URI");
        }
        List<String> segments = uri.getPathSegments();
        if (segments.size() != 1 || !isLog(segments.get(0))) {
            throw new IllegalArgumentException("Invalid log path");
        }
        File logs = new File(GamePaths.of(context).dataDir(), "logs").getCanonicalFile();
        File file = new File(logs, segments.get(0)).getCanonicalFile();
        if (!logs.equals(file.getParentFile()) || !file.isFile()) {
            throw new FileNotFoundException("Log is outside the log directory or missing");
        }
        return file;
    }

    private static boolean isLog(String name) {
        return name.matches("gow2_[0-9]+\\.log");
    }

    private static BigInteger logNumber(String name) {
        return new BigInteger(name.substring(5, name.length() - 4));
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("Read-only provider");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read-only provider");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Read-only provider");
    }
}
