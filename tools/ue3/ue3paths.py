"""Repository-relative default folders for the UE3 tools, overridable by environment variables."""
import os
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def pkg_dir() -> Path:
    env = os.environ.get("UE3_PKG_DIR")
    if env:
        return Path(env)
    return repo_root() / "out" / "ue3" / "pkg"


def decompressor() -> Path:
    if "UE3_DECOMPRESS" in os.environ:
        return Path(os.environ["UE3_DECOMPRESS"])
    return repo_root() / "out" / "ue3" / ("ue3x360_decompress.exe" if os.name == "nt" else "ue3x360_decompress")


def kismet_dir() -> Path:
    if "UE3_KISMET_DIR" in os.environ:
        return Path(os.environ["UE3_KISMET_DIR"])
    return repo_root() / "out" / "ue3" / "kismet"
