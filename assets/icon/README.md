# Project icon

The icon is a stencil insignia: a riveted steel shield with a chevron notch on top and a Roman
numeral II sprayed through a stencil in orange-red. It is plain geometry drawn by
`render_icon.py`, with no font in the mark itself. It contains no cog, no skull, nothing shaped
like the Crimson Omen, no logotype and no traced artwork from the game.

Gears of War is a trademark of Microsoft. The icon never shows the name. Wherever the icon
appears with the name (release page, store listing, social card, website), the name is
plain text and the "unofficial, not affiliated with Microsoft, Xbox Game Studios, The Coalition
or Epic Games" notice goes with it.

## Files

| File | Use |
|---|---|
| `gow2-icon-1024.png` | Master, transparent, full bleed (README, release page, anywhere a large icon is needed) |
| `gow2.ico` | Windows exe icon, 16 to 256 px (16, 20, 24, 32, 40, 48, 64, 128, 256); embedded by `src/gow2.rc` |
| `favicon.ico` | Browser favicon, 16, 32 and 48 px |
| `favicon-32.png` | PNG favicon |
| `favicon-180.png` | `apple-touch-icon`, opaque on the background colour |
| `gow2-icon-flat.svg` | Flat vector: steel rim, flat red, no texture. For SVG favicons and badges on busy or mid-tone backgrounds |
| `gow2-icon-mono.svg` | Single-colour vector: light shield (`#E8ECEF`) with the II knocked out. For dark backgrounds only; on light ones use `gow2-icon-flat.svg` |
| `og-image-1200x630.png` | Open Graph / social preview card in English (GitHub: Settings > Social preview; the website's `docs/assets/og-image.jpg`) |
| `og-image-es-1200x630.png` | The same card in Spanish (the website's `docs/assets/og-image-es.jpg`) |
| `android/` | Android launcher icons, laid out like `res/` (see below) |

The 16, 20, 24, 32, 40 and 48 px frames are drawn on whole pixels by hand-fitted layouts in
`icon_mark.py` (`HINTED`); larger sizes come from the vector design. Up to 64 px the serifs and
stems are one solid shape; the stencil cuts open once they are at least 2 px wide (about 100 px
and up), and the paint chips, rim scratches and overspray fade in from 128 px to full strength at
512 px. The red stays orange-red (`#FF4E28` to `#CE1814`), away from the deep crimson of the
game's own branding.

## Android

`android/` mirrors an Android `res/` folder:

- `mipmap-anydpi/ic_launcher.xml` and `ic_launcher_round.xml`: adaptive icon (background colour,
  foreground layer, and the monochrome layer that Android 13+ uses for themed icons).
- `mipmap-{mdpi,hdpi,xhdpi,xxhdpi,xxxhdpi}/`: `ic_launcher_foreground.png` (108 dp layer,
  transparent), `ic_launcher_monochrome.png` (same layer, one flat shape), and the legacy
  `ic_launcher.png` / `ic_launcher_round.png` (48 dp).
- `ic_launcher_foreground_432.png` and `ic_launcher_monochrome_432.png`: the xxxhdpi layers, for
  tools that want a single source image.
- `ic_launcher_playstore_512.png`: Play Store listing icon (opaque, Google applies the mask).

Adaptive background colour: **`#1B1D21`**. In `values/colors.xml`:

```xml
<color name="ic_launcher_background">#FF1B1D21</color>
```

The mark stays inside the 66 dp safe circle of the 108 dp layer, which every launcher mask keeps:
its farthest pixel is at 0.30 of the layer width, and the limit is 33/108 = 0.3056. It was also
checked by eye under circle, squircle and teardrop masks.

To use it in `android/app`: copy the `mipmap-*` folders into `android/app/src/main/res/`
(this replaces `mipmap-anydpi/ic_launcher.xml`), set `ic_launcher_background` as above, and delete
the placeholder `drawable/ic_launcher_foreground.xml`, which nothing references any more.

## Windows exe

`src/gow2.rc` embeds `gow2.ico` as the first icon resource, and `CMakeLists.txt` adds it to the
`gow2` sources on Windows. Explorer shows the first icon resource of an exe, and SDL3 gives the
same icon to the game window, so the title bar and taskbar match the file.

## Regenerating

```text
python assets/icon/render_icon.py
python assets/icon/render_icon.py --preview out/brand/final
python tools/site_assets.py
```

`tools/site_assets.py` then copies the favicons and the social cards into the website
(`docs/assets`) and rebuilds its WebP screenshots.

Needs Python 3 with Pillow and NumPy (`pip install pillow numpy`; tested with Python 3.11,
Pillow 10.4 and NumPy 2.4), on Windows for the font (see below). Every file listed above is rewritten from scratch, and the output is
byte-identical from run to run, because the wear pattern uses a fixed seed. `--preview` also writes review sheets to a
folder outside the repository (the sizes on dark and light backgrounds, the Android masks, the
themed icon and the legacy icons).

The script is split into three files: `render_icon.py` writes the outputs, `icon_mark.py` draws
the mark, and `icon_sheets.py` composes the social card and the preview sheets.

The script refuses to write anything if:

- the numerals come closer than 2.5 design units to the rim (checked on the geometry),
- a hand-fitted small frame leaves less than 1 px between a numeral and the rim,
- any Android foreground or monochrome layer has a pixel outside the 66 dp safe circle.

## License

The icon is original work for this project and is covered by the repository's BSD-3-Clause
license (see `LICENSE` at the root). That covers the mark and every file here that contains only
the mark: the PNG, ICO and SVG icons and the Android layers.

The social cards (`og-image-1200x630.png`, `og-image-es-1200x630.png`) and their website copies
(`docs/assets/og-image.jpg`, `docs/assets/og-image-es.jpg`) also contain two things that are not
covered by that license; the website's screenshot images (`docs/screenshots/*.jpg` and every
`docs/assets/img/*.webp` except `icon-320.webp`) contain the first:

- A screenshot of the game running in this port (`docs/screenshots/act1-4k-internal.jpg`). Gears
  of War 2 content is © Microsoft Corporation (originally © 2008 Epic Games, Inc.).
- Text set in Bahnschrift, a Microsoft font that ships with Windows 10 and 11
  (`C:\Windows\Fonts\bahnschrift.ttf`). The font file is not part of this repository and is not
  redistributed; only the rendered text is. Bahnschrift is licensed by Microsoft with Windows,
  not under BSD-3-Clause. The script also uses it for the labels on the preview sheets. No font
  is used in the icon.
