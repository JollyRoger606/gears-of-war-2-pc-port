// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
package com.santiquiroz.gow2recomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

public class TomlRowsTest {
    @Test
    public void appendsToEmptyFile() {
        assertEquals("resolution_scale = 1\n", TomlRows.set("", "resolution_scale", "1"));
    }

    @Test
    public void replacesFirstMatchingRowAndPreservesOtherRows() {
        String input = "# settings\n  resolution_scale = 2\nresolution_scale = 3\nother = true";

        assertEquals("# settings\n  resolution_scale = 1\nresolution_scale = 3\nother = true",
                TomlRows.set(input, "resolution_scale", "1"));
        assertEquals("2", TomlRows.get(input, "resolution_scale"));
    }

    @Test
    public void doesNotMatchKeysWithTheSamePrefix() {
        String input = "resolution_scale_extra = 2\n";

        assertNull(TomlRows.get(input, "resolution_scale"));
        assertEquals(input, TomlRows.remove(input, "resolution_scale"));
        assertEquals(input + "resolution_scale = 1\n", TomlRows.set(input, "resolution_scale", "1"));
    }

    @Test
    public void doesNotReadOrReplaceCommentedRows() {
        String input = "  # resolution_scale = 2\n";

        assertNull(TomlRows.get(input, "resolution_scale"));
        assertEquals(input, TomlRows.remove(input, "resolution_scale"));
        assertEquals(input + "resolution_scale = 1\n", TomlRows.set(input, "resolution_scale", "1"));
    }

    @Test
    public void removesOnlyFirstMatchingRowIncludingItsNewline() {
        String input = "first = true\nresolution_scale = 2\nresolution_scale = 3\nlast = false";

        assertEquals("first = true\nresolution_scale = 3\nlast = false",
                TomlRows.remove(input, "resolution_scale"));
    }

    @Test
    public void preservesCrLfWhenReplacingRemovingAndAppending() {
        String input = "# settings\r\nresolution_scale = 2\r\nother = true\r\n";

        assertEquals("# settings\r\nresolution_scale = 1\r\nother = true\r\n",
                TomlRows.set(input, "resolution_scale", "1"));
        assertEquals("# settings\r\nother = true\r\n", TomlRows.remove(input, "resolution_scale"));
        assertEquals(input + "present_effect = \"fsr\"\r\n", TomlRows.set(input, "present_effect", "\"fsr\""));
    }

    @Test
    public void addsNewlineBeforeAppendingToUnterminatedFile() {
        assertEquals("other = true\nresolution_scale = 1\n",
                TomlRows.set("other = true", "resolution_scale", "1"));
    }

    @Test
    public void ignoresSectionRowsAndInsertsNewRowsBeforeTheFirstSection() {
        String input = "# settings\r\n[graphics]\r\nresolution_scale = 2\r\n";

        assertNull(TomlRows.get(input, "resolution_scale"));
        assertEquals(input, TomlRows.remove(input, "resolution_scale"));
        assertEquals("# settings\r\nresolution_scale = 1\r\n[graphics]\r\nresolution_scale = 2\r\n",
                TomlRows.set(input, "resolution_scale", "1"));
    }

    @Test
    public void preservesInlineCommentAndSpacingWhenReplacingValue() {
        String input = "  present_effect  =  \"fsr\"  # image filter\n";

        assertEquals("\"fsr\"", TomlRows.get(input, "present_effect"));
        assertEquals("  present_effect  =  \"bilinear\"  # image filter\n",
                TomlRows.set(input, "present_effect", "\"bilinear\""));
    }

    @Test
    public void treatsHashesInsideQuotedStringsAsPartOfTheLiteral() {
        assertEquals("\"a#b\\\"c\"", TomlRows.get("name = \"a#b\\\"c\" # comment", "name"));
        assertEquals("'a#b'", TomlRows.get("name = 'a#b' # comment", "name"));
    }

    @Test
    public void removesUnterminatedLastRowWithoutChangingThePreviousNewline() {
        assertEquals("other = true\r\n",
                TomlRows.remove("other = true\r\nresolution_scale = 2", "resolution_scale"));
    }
}
