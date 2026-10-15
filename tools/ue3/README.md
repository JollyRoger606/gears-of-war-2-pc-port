# UnrealScript package tools

These tools read the decompressed packages of your own copy of the game. The repository contains no game data; you produce the inputs yourself by building ue3x360_decompress.c and decompressing your .xxx files into out/ue3/pkg, which is gitignored as part of out/.

## Setup

- Build the decompressor against libmspack (LGPL, not included in this repository): `clang -O2 -I<libmspack>/mspack tools/ue3/ue3x360_decompress.c <libmspack>/mspack/lzxd.c -o out/ue3/ue3x360_decompress.exe` (from the repository root).
- Fully compressed packages (Core, Engine, GameFramework, GearGame, IpDrv, OnlineSubsystemLive) are decompressed with `out/ue3/ue3x360_decompress <Game>/GearGame/CookedXenon/GearGame.xxx out/ue3/pkg/GearGame.upk`.
- Chunk-compressed packages (maps, GearStart) are decompressed with `python -I tools/ue3/decompress_chunked.py <map>.xxx <map>.upk`.
- The package folder defaults to `out/ue3/pkg` and is overridden by `UE3_PKG_DIR`.
- The decompressor path defaults to `out/ue3/ue3x360_decompress.exe` on Windows (`out/ue3/ue3x360_decompress` elsewhere) and is overridden by `UE3_DECOMPRESS`.
- The Kismet dump folder defaults to `out/ue3/kismet` and is overridden by `UE3_KISMET_DIR`.

## Tools

- `ue3pkg.py` - Minimal reader for a decompressed GoW2 (UE3 ver 575, big-endian) package.
- `upk.py` - Minimal reader for the decompressed GoW2 Xbox 360 packages (UE3 file version 575, big-endian).
- `ue3script.py` - UnrealScript bytecode decompiler for GoW2's cooked script (UE3 ver 575, big-endian).
- `ubc.py` - UnrealScript bytecode lister for GoW2 cooked packages (UE3 file version 575, big-endian).
- `ubx.py` - Lists bytecode of exports by index.
- `scan_scripts.py` - Disassembles every function in a package and prints the lines that mention any of the given substrings, prefixed with the function path.
- `layout.py` - Computes UE3 script property offsets for a class from the decompressed package.
- `cdo.py` - Prints the tagged properties of a class default object (Default__<Class>) from a decompressed GoW2 package (UE3 575, big-endian).
- `decompress_chunked.py` - Expand a chunk-compressed UE3 X360 package (summary lists CompressedChunks) into a plain one.
- `ue3x360_decompress.c` - Decompresses the player's own fully compressed UE3 Xbox 360 packages (.xxx) into plain packages.
- `coalesced_dump.py` - Dumps sections of a GoW2 Coalesced_*.bin bundle (read-only).
- `scan_hdr.py` - Decompress only the header chunks of UE3 X360 .xxx packages and report exports/imports matching a regex.
- `check_script_sizes.py` - Checks that every Function export's on-disk bytecode length equals its serialized ScriptSize (i.e. disk layout == in-memory layout on the 32-bit 360), and summarizes package flags.
- `fbytes.py` - Print each top-level statement of a function with its raw bytecode bytes.
- `kismet_scan.py` - Dump Kismet objects of a decompressed GoW2 map package as JSON lines.
- `kismet_agg.py` - Aggregate Kismet dumps (*.jsonl) from UE3_KISMET_DIR (default out/ue3/kismet).
- `kgraph.py` - Follow Kismet output links from every object of a class in one map.
- `hero_links.py` - Per op class: how often a variable link targets the heroes (SeqVar_Marcus/SeqVar_Dom) without SeqVar_Player, with SeqVar_Player, or both.
- `ue3paths.py` - Repository-relative default folders for the UE3 tools, overridable by environment variables.
