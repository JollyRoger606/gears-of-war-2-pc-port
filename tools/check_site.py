# Checks the GitHub Pages site in docs/ before it is published: HTML structure, local links and
# images, canonical/hreflang/Open Graph tags, the JSON-LD graph shared by both languages, the
# sitemap, and a few accessibility rules. Prints each problem; exit code 1 if there is any.
# python tools/check_site.py [docs folder]
import json
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

SITE_URL = "https://santiquiroz.github.io/gears-of-war-2-pc-port/"
SITE_PATH = urlsplit(SITE_URL).path
PAGES = ("index.html", "es/index.html")
ERROR_PAGE = "404.html"
VOID_TAGS = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
SVG_SELF_CLOSING = {"use", "polygon", "path", "rect", "circle", "line", "stop"}
URL_ATTRS = ("href", "src")
SRCSET_ATTRS = ("srcset", "imagesrcset")
UNNAMEABLE_TAGS = {"div", "span", "pre", "p", "code", "b", "i", "strong", "em"}
UNOFFICIAL_MARKERS = ("unofficial", "no oficial")


class Page:
    def __init__(self, path):
        self.path = path
        self.elements = []
        self.ids = []
        self.structure_errors = []
        self.title = ""
        self.json_ld = []


class PageParser(HTMLParser):
    def __init__(self, page):
        super().__init__(convert_charrefs=True)
        self.page = page
        self.stack = []
        self.capture = None
        self.buffer = []

    def handle_starttag(self, tag, attrs):
        self.record(tag, dict(attrs))
        if tag not in VOID_TAGS:
            self.stack.append((tag, self.getpos()[0]))
        self.start_capture(tag, dict(attrs))

    def handle_startendtag(self, tag, attrs):
        self.record(tag, dict(attrs))

    def handle_endtag(self, tag):
        if tag in VOID_TAGS:
            return
        self.finish_capture(tag)
        self.close_element(tag)

    def handle_data(self, data):
        if self.capture:
            self.buffer.append(data)

    def record(self, tag, attrs):
        self.page.elements.append((tag, attrs, self.getpos()[0]))
        if "id" in attrs:
            self.page.ids.append(attrs["id"])

    def start_capture(self, tag, attrs):
        if tag == "title" or (tag == "script" and attrs.get("type") == "application/ld+json"):
            self.capture = tag
            self.buffer = []

    def finish_capture(self, tag):
        if self.capture != tag:
            return
        text = "".join(self.buffer)
        if tag == "title":
            self.page.title = text.strip()
        else:
            self.page.json_ld.append(text)
        self.capture = None

    def close_element(self, tag):
        line = self.getpos()[0]
        if not self.stack:
            self.page.structure_errors.append(f"line {line}: </{tag}> without an open element")
            return
        open_tag, open_line = self.stack.pop()
        if open_tag != tag:
            self.page.structure_errors.append(f"line {line}: </{tag}> closes <{open_tag}> opened on line {open_line}")

    def unclosed(self):
        return [f"line {line}: <{tag}> is never closed" for tag, line in self.stack if tag not in SVG_SELF_CLOSING]


def parse_page(path):
    page = Page(path)
    parser = PageParser(page)
    parser.feed(path.read_text(encoding="utf-8"))
    parser.close()
    page.structure_errors.extend(parser.unclosed())
    return page


def elements(page, tag):
    return [attrs for name, attrs, _ in page.elements if name == tag]


def meta_content(page, key):
    for attrs in elements(page, "meta"):
        if attrs.get("property") == key or attrs.get("name") == key:
            return attrs.get("content")
    return None


def link_hrefs(page, rel):
    return [attrs.get("href") for attrs in elements(page, "link") if attrs.get("rel") == rel]


def html_lang(page):
    langs = elements(page, "html")
    return langs[0].get("lang") if langs else None


def is_external(url):
    parts = urlsplit(url)
    return bool(parts.scheme) and not url.startswith(SITE_URL) or url.startswith(("mailto:", "data:"))


def local_target(docs, page_path, url):
    path = unquote(urlsplit(url).path)
    if path.startswith(SITE_PATH):
        target = docs / path[len(SITE_PATH):]
    elif path.startswith("/"):
        return None
    else:
        target = page_path.parent / path
    if url.split("#")[0].endswith("/") or target.is_dir():
        target = target / "index.html"
    return target


def srcset_urls(value):
    return [part.strip().split()[0] for part in value.split(",") if part.strip()]


def page_urls(page):
    for tag, attrs, line in page.elements:
        for name in URL_ATTRS:
            if attrs.get(name):
                yield line, attrs[name]
        for name in SRCSET_ATTRS:
            for url in srcset_urls(attrs.get(name) or ""):
                yield line, url


def check_structure(page):
    return list(page.structure_errors)


def check_duplicate_ids(page):
    seen = set()
    errors = []
    for element_id in page.ids:
        if element_id in seen:
            errors.append(f"duplicate id '{element_id}'")
        seen.add(element_id)
    return errors


def check_fragments(page):
    ids = set(page.ids)
    return [f"line {line}: link to missing #{url[1:]}" for line, url in page_urls(page)
            if url.startswith("#") and len(url) > 1 and url[1:] not in ids]


def check_local_files(docs, page):
    errors = []
    for line, url in page_urls(page):
        if url.startswith("#") or is_external(url):
            continue
        target = local_target(docs, page.path, url)
        if target is None or not target.is_file():
            errors.append(f"line {line}: '{url}' does not resolve to a file under docs/")
    return errors


def check_images(page):
    errors = []
    for tag, attrs, line in page.elements:
        if tag != "img":
            continue
        if "alt" not in attrs:
            errors.append(f"line {line}: <img> without alt")
        if not attrs.get("width") or not attrs.get("height"):
            errors.append(f"line {line}: <img> without width and height")
    return errors


def check_aria_names(page):
    return [f"line {line}: aria-label on <{tag}> without a role (the generic role cannot be named)"
            for tag, attrs, line in page.elements
            if tag in UNNAMEABLE_TAGS and "aria-label" in attrs and "role" not in attrs]


def check_scrollable_code(page):
    return [f"line {line}: <pre> must be focusable (tabindex=0) to scroll with the keyboard"
            for tag, attrs, line in page.elements if tag == "pre" and attrs.get("tabindex") != "0"]


def check_no_third_party_styles(page):
    hrefs = link_hrefs(page, "stylesheet") + link_hrefs(page, "preconnect")
    return [f"third-party stylesheet or connection: {href}" for href in hrefs if href and is_external(href)]


def check_unofficial_title(page):
    title = page.title.lower()
    if any(marker in title for marker in UNOFFICIAL_MARKERS):
        return []
    return [f"<title> does not say the port is unofficial: {page.title}"]


def check_canonical(docs, page):
    canonical = link_hrefs(page, "canonical")
    if len(canonical) != 1:
        return ["needs exactly one canonical link"]
    errors = []
    if meta_content(page, "og:url") != canonical[0]:
        errors.append(f"og:url {meta_content(page, 'og:url')} differs from canonical {canonical[0]}")
    if local_target(docs, page.path, canonical[0]) != page.path:
        errors.append(f"canonical {canonical[0]} is not this page")
    return errors


def check_social_images(docs, page):
    errors = []
    for key in ("og:image", "twitter:image"):
        url = meta_content(page, key)
        target = local_target(docs, page.path, url) if url else None
        if target is None or not target.is_file():
            errors.append(f"{key} {url} does not map to a file under docs/")
        if not meta_content(page, key + ":alt"):
            errors.append(f"{key}:alt is missing")
    return errors


def hreflang_set(page):
    return sorted((attrs.get("hreflang"), attrs.get("href")) for attrs in elements(page, "link")
                  if attrs.get("rel") == "alternate" and attrs.get("hreflang"))


def json_ld_nodes(page):
    nodes = {}
    for block in page.json_ld:
        for node in json.loads(block).get("@graph", []):
            nodes[node["@id"]] = node
    return nodes


def check_json_ld(page):
    try:
        nodes = json_ld_nodes(page)
    except (ValueError, KeyError) as exc:
        return [f"JSON-LD does not parse: {exc}"]
    errors = [f"{node_id}: SoftwareApplication must not carry inLanguage (it describes the page)"
              for node_id, node in nodes.items() if node.get("@type") == "SoftwareApplication" and "inLanguage" in node]
    pages = [node for node in nodes.values() if node.get("@type") == "WebPage"]
    canonical = link_hrefs(page, "canonical")
    if len(pages) != 1:
        return errors + ["JSON-LD needs exactly one WebPage node"]
    if pages[0].get("inLanguage") != html_lang(page):
        errors.append(f"WebPage inLanguage {pages[0].get('inLanguage')} differs from <html lang> {html_lang(page)}")
    if canonical and pages[0].get("url") != canonical[0]:
        errors.append(f"WebPage url {pages[0].get('url')} differs from canonical {canonical[0]}")
    return errors


def check_page(docs, page):
    checks = (
        check_structure(page), check_duplicate_ids(page), check_fragments(page),
        check_local_files(docs, page), check_images(page), check_aria_names(page),
        check_scrollable_code(page), check_no_third_party_styles(page), check_unofficial_title(page),
        check_canonical(docs, page), check_social_images(docs, page), check_json_ld(page),
    )
    return [error for errors in checks for error in errors]


def check_shared_nodes(pages):
    graphs = [json_ld_nodes(page) for page in pages]
    shared = set.intersection(*(set(graph) for graph in graphs))
    return [f"JSON-LD node {node_id} differs between the language versions"
            for node_id in sorted(shared) if any(graph[node_id] != graphs[0][node_id] for graph in graphs)]


def check_hreflang(pages):
    sets = [hreflang_set(page) for page in pages]
    errors = [f"{page.path.name}: hreflang links differ from the English page" for page, links in zip(pages, sets) if links != sets[0]]
    if not any(lang == "x-default" for lang, _ in sets[0]):
        errors.append("hreflang has no x-default")
    canonicals = {link_hrefs(page, "canonical")[0] for page in pages}
    if canonicals != {href for lang, href in sets[0] if lang != "x-default"}:
        errors.append("hreflang alternates do not match the canonical URLs")
    return errors


def sitemap_locs(docs):
    text = (docs / "sitemap.xml").read_text(encoding="utf-8")
    return {part.split("</loc>")[0] for part in text.split("<loc>")[1:]}


def check_sitemap(docs, pages):
    canonicals = {link_hrefs(page, "canonical")[0] for page in pages}
    locs = sitemap_locs(docs)
    return [] if locs == canonicals else [f"sitemap <loc> {sorted(locs)} differs from canonicals {sorted(canonicals)}"]


def check_error_page(docs):
    path = docs / ERROR_PAGE
    if not path.is_file():
        return [f"{ERROR_PAGE} is missing"]
    page = parse_page(path)
    errors = check_structure(page) + check_local_files(docs, page)
    if "noindex" not in (meta_content(page, "robots") or ""):
        errors.append(f"{ERROR_PAGE} must be noindex")
    relative = [url for _, url in page_urls(page) if not url.startswith(("/", "#")) and not is_external(url)]
    errors += [f"{ERROR_PAGE}: '{url}' must be root-absolute (GitHub serves it at any depth)" for url in relative]
    return errors


def check_site(docs):
    pages = [parse_page(docs / name) for name in PAGES]
    problems = []
    for page in pages:
        problems += [f"{page.path.relative_to(docs).as_posix()}: {error}" for error in check_page(docs, page)]
    problems += check_shared_nodes(pages) + check_hreflang(pages) + check_sitemap(docs, pages)
    problems += check_error_page(docs)
    return problems


def main(argv):
    docs = Path(argv[0]) if argv else Path(__file__).resolve().parents[1] / "docs"
    problems = check_site(docs)
    for problem in problems:
        print(problem)
    print(f"{len(problems)} problem(s) in {docs}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
