# Tests for tools/check_site.py: the real site in docs/ and small generated pages.
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_site

DOCS = Path(__file__).resolve().parents[1] / "docs"


def write_page(tmp_path, body, head=""):
    path = tmp_path / "index.html"
    path.write_text(f"<!doctype html><html lang=\"en\"><head><title>t</title>{head}</head><body>{body}</body></html>",
                    encoding="utf-8")
    return check_site.parse_page(path)


def json_ld(nodes):
    return f"<script type=\"application/ld+json\">{json.dumps({'@graph': nodes})}</script>"


def test_published_site_has_no_problems():
    assert check_site.check_site(DOCS) == []


def test_aria_label_on_a_generic_div_is_flagged(tmp_path):
    page = write_page(tmp_path, "<div aria-label=\"x\"></div><div role=\"group\" aria-label=\"y\"></div>")
    assert len(check_site.check_aria_names(page)) == 1


def test_link_to_a_missing_fragment_is_flagged(tmp_path):
    page = write_page(tmp_path, "<a href=\"#here\">a</a><a href=\"#gone\">b</a><p id=\"here\"></p>")
    assert check_site.check_fragments(page) == ["line 1: link to missing #gone"]


def test_mismatched_end_tag_is_flagged(tmp_path):
    page = write_page(tmp_path, "<ul><li>a</ul>")
    assert check_site.check_structure(page)


def test_missing_local_image_is_flagged(tmp_path):
    (tmp_path / "here.webp").write_bytes(b"")
    page = write_page(tmp_path, "<img src=\"here.webp\" alt=\"\" width=\"1\" height=\"1\"><img src=\"gone.webp\" alt=\"\" width=\"1\" height=\"1\">")
    errors = check_site.check_local_files(tmp_path, page)
    assert len(errors) == 1 and "gone.webp" in errors[0]


def test_title_without_unofficial_is_flagged(tmp_path):
    page = write_page(tmp_path, "")
    assert check_site.check_unofficial_title(page)


def test_shared_node_that_differs_between_languages_is_flagged(tmp_path):
    (tmp_path / "es").mkdir()
    english = write_page(tmp_path, "", json_ld([{"@id": "#app", "name": "A"}]))
    spanish_path = tmp_path / "es" / "index.html"
    spanish_path.write_text(f"<html lang=\"es\"><head>{json_ld([{'@id': '#app', 'name': 'B'}])}</head></html>", encoding="utf-8")
    spanish = check_site.parse_page(spanish_path)
    assert check_site.check_shared_nodes([english, spanish]) == ["JSON-LD node #app differs between the language versions"]


def test_language_on_the_software_application_is_flagged(tmp_path):
    nodes = [{"@id": "#app", "@type": "SoftwareApplication", "inLanguage": "en"},
             {"@id": "#page", "@type": "WebPage", "inLanguage": "en"}]
    page = write_page(tmp_path, "", json_ld(nodes))
    errors = check_site.check_json_ld(page)
    assert len(errors) == 1 and "inLanguage" in errors[0]
