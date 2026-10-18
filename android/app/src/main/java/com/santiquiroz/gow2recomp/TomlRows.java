// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class TomlRows {
    private static final Pattern LINES = Pattern.compile("([^\\r\\n]*)(\\r\\n|\\r|\\n|$)");

    private TomlRows() {}

    static String set(String toml, String key, String valueLiteral) {
        Row row = find(toml, key);
        if (row == null) {
            return append(toml, key + " = " + valueLiteral);
        }
        return toml.substring(0, row.valueStart) + valueLiteral + toml.substring(row.valueEnd);
    }

    static String remove(String toml, String key) {
        Row row = find(toml, key);
        if (row == null) {
            return toml;
        }
        return toml.substring(0, row.lineStart) + toml.substring(row.lineEnd);
    }

    static String get(String toml, String key) {
        Row row = find(toml, key);
        return row == null ? null : toml.substring(row.valueStart, row.valueEnd);
    }

    private static Row find(String toml, String key) {
        Pattern assignment = Pattern.compile("[ \\t]*" + Pattern.quote(key) + "[ \\t]*=[ \\t]*(.*)");
        Matcher lines = LINES.matcher(toml);
        while (lines.find() && lines.start() < lines.end()) {
            String line = lines.group(1);
            if (isSection(line)) {
                return null;
            }
            Matcher match = assignment.matcher(line);
            if (match.matches()) {
                int valueStart = lines.start() + match.start(1);
                int valueEnd = valueStart + literalEnd(match.group(1));
                return new Row(lines.start(), lines.end(), valueStart, valueEnd);
            }
        }
        return null;
    }

    private static int literalEnd(String value) {
        int end = commentStart(value);
        while (end > 0 && Character.isWhitespace(value.charAt(end - 1))) {
            end--;
        }
        return end;
    }

    private static int commentStart(String value) {
        char quote = 0;
        boolean escaped = false;
        for (int index = 0; index < value.length(); index++) {
            char current = value.charAt(index);
            if (escaped) {
                escaped = false;
                continue;
            }
            if (quote == '"' && current == '\\') {
                escaped = true;
                continue;
            }
            if (quote != 0) {
                if (current == quote) {
                    quote = 0;
                }
                continue;
            }
            if (current == '#') {
                return index;
            }
            if (current == '"' || current == '\'') {
                quote = current;
            }
        }
        return value.length();
    }

    private static String append(String toml, String row) {
        String newline = newline(toml);
        int insertion = firstSection(toml);
        String before = toml.substring(0, insertion);
        String separator = before.isEmpty() || endsWithNewline(before) ? "" : newline;
        return before + separator + row + newline + toml.substring(insertion);
    }

    private static String newline(String toml) {
        Matcher lines = LINES.matcher(toml);
        if (lines.find() && !lines.group(2).isEmpty()) {
            return lines.group(2);
        }
        return "\n";
    }

    private static int firstSection(String toml) {
        Matcher lines = LINES.matcher(toml);
        while (lines.find() && lines.start() < lines.end()) {
            if (isSection(lines.group(1))) {
                return lines.start();
            }
        }
        return toml.length();
    }

    private static boolean isSection(String line) {
        return line.trim().startsWith("[");
    }

    private static boolean endsWithNewline(String text) {
        char last = text.charAt(text.length() - 1);
        return last == '\n' || last == '\r';
    }

    private static final class Row {
        final int lineStart;
        final int lineEnd;
        final int valueStart;
        final int valueEnd;

        Row(int lineStart, int lineEnd, int valueStart, int valueEnd) {
            this.lineStart = lineStart;
            this.lineEnd = lineEnd;
            this.valueStart = valueStart;
            this.valueEnd = valueEnd;
        }
    }
}
