#!/usr/bin/env python3
"""Build the exact HEV Apple libraries and isolate their duplicate internals."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


IOS = Path(__file__).resolve().parents[1]
THIRD_PARTY = IOS / "ThirdParty"
SOURCES = THIRD_PARTY / "sources"
DEPENDENCIES = (
    (
        "hev-socks5-tunnel",
        "https://github.com/heiher/hev-socks5-tunnel.git",
        "0428c4ebb0df933ebac8e507832f252ef7da47f1",
        "HevSocks5Tunnel.xcframework",
    ),
    (
        "hev-socks5-server",
        "https://github.com/heiher/hev-socks5-server.git",
        "b6e41fe7c1a30aa5b8ac425233d3c95cd618a214",
        "HevSocks5Server.xcframework",
    ),
)
SERVER_PUBLIC = {
    "_hev_socks5_server_main_from_file",
    "_hev_socks5_server_main_from_str",
    "_hev_socks5_server_quit",
}


def run(*args: str | Path, cwd: Path | None = None, capture: bool = False) -> str:
    command = [str(arg) for arg in args]
    if capture:
        return subprocess.check_output(command, cwd=cwd, text=True).strip()
    subprocess.check_call(command, cwd=cwd)
    return ""


def checkout(name: str, url: str, commit: str) -> Path:
    path = SOURCES / name
    if not (path / ".git").is_dir():
        if path.exists():
            raise RuntimeError(f"{path} exists but is not a Git checkout")
        run("git", "clone", "--filter=blob:none", "--no-checkout", url, path)
    run("git", "remote", "set-url", "origin", url, cwd=path)
    run("git", "fetch", "--depth=1", "origin", commit, cwd=path)
    run("git", "checkout", "--detach", "--force", commit, cwd=path)
    run("git", "submodule", "sync", "--recursive", cwd=path)
    run("git", "submodule", "update", "--init", "--recursive", "--depth=1", cwd=path)
    head = run("git", "rev-parse", "HEAD", cwd=path, capture=True)
    if head != commit:
        raise RuntimeError(f"{name}: expected {commit}, got {head}")
    return path


def build(source: Path, framework_name: str) -> Path:
    destination = THIRD_PARTY / framework_name
    built = source / framework_name
    if destination.is_dir() and built.is_dir():
        print(f"Reusing {framework_name}", flush=True)
        return destination
    run("bash", "build-apple.sh", cwd=source)
    if not built.is_dir():
        raise RuntimeError(f"upstream build did not create {built}")
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(built, destination)
    return destination


def defined_globals(archive: Path) -> set[str]:
    output = subprocess.check_output(["xcrun", "nm", "-gjU", str(archive)], text=True,
                                     stderr=subprocess.DEVNULL).strip()
    return {line.strip() for line in output.splitlines() if line.startswith("_")}


def archive_arches(archive: Path) -> list[str]:
    return run("xcrun", "lipo", "-archs", archive, capture=True).split()


def sdk_for(identifier: str) -> str:
    if "ios" in identifier:
        return "iphonesimulator" if "simulator" in identifier else "iphoneos"
    if "tvos" in identifier:
        return "appletvsimulator" if "simulator" in identifier else "appletvos"
    if "macos" in identifier:
        return "macosx"
    raise RuntimeError(f"unknown XCFramework slice: {identifier}")


def platform_for(sdk: str) -> tuple[str, str]:
    return {
        "iphoneos": ("ios", "15.0"),
        "iphonesimulator": ("ios-simulator", "15.0"),
        "macosx": ("macos", "10.14"),
        "appletvos": ("tvos", "17.0"),
        "appletvsimulator": ("tvos-simulator", "17.0"),
    }[sdk]


def thin(archive: Path, arch: str, output: Path) -> None:
    arches = archive_arches(archive)
    if len(arches) == 1:
        shutil.copy2(archive, output)
    else:
        run("xcrun", "lipo", archive, "-thin", arch, "-output", output)


def find_objcopy() -> str | None:
    candidates: list[str] = []
    try:
        candidates.append(run("xcrun", "--find", "llvm-objcopy", capture=True))
    except subprocess.CalledProcessError:
        pass
    found = shutil.which("llvm-objcopy")
    if found:
        candidates.append(found)
    for candidate in candidates:
        if not candidate:
            continue
        try:
            subprocess.check_call([candidate, "--version"], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
            return candidate
        except (OSError, subprocess.CalledProcessError):
            pass
    return None


def objcopy_archive(objcopy: str, archive: Path, mapping: Path, output: Path) -> None:
    run(objcopy, f"--redefine-syms={mapping}", archive, output)


def native_isolate(archive: Path, arch: str, identifier: str, mapping: Path,
                   conflicts: Path, output: Path) -> None:
    """Fallback for stock Xcode, which ships ld/nmedit but no llvm-objcopy."""
    sdk = sdk_for(identifier)
    sdk_path = run("xcrun", "--sdk", sdk, "--show-sdk-path", capture=True)
    sdk_version = run("xcrun", "--sdk", sdk, "--show-sdk-version", capture=True)
    platform, minimum = platform_for(sdk)
    combined = output.with_suffix(".o")
    aliased = output.with_name(output.stem + "-aliased.o")
    localized = output.with_name(output.stem + "-private.o")
    link = ["xcrun", "--sdk", sdk, "ld", "-r", "-arch", arch, "-syslibroot", sdk_path,
            "-platform_version", platform, minimum, sdk_version]
    public = sorted(SERVER_PUBLIC & defined_globals(archive))
    roots = [argument for symbol in public for argument in ("-u", symbol)]
    run(*link, *roots, archive, "-o", combined)

    requested = set(conflicts.read_text().splitlines())
    selected = sorted(requested & defined_globals(combined))
    selected_mapping = output.with_name(output.stem + "-mapping.txt")
    selected_conflicts = output.with_name(output.stem + "-conflicts.txt")
    selected_mapping.write_text("".join(
        f"{symbol} _bajji_server_{symbol.lstrip('_')}\n" for symbol in selected
    ))
    selected_conflicts.write_text("".join(f"{symbol}\n" for symbol in selected))
    run(*link, combined, "-alias_list", selected_mapping, "-o", aliased)
    run("xcrun", "nmedit", "-R", selected_conflicts, aliased, "-o", localized)
    run("xcrun", "libtool", "-static", "-o", output, localized)


def isolate_pair(tunnel: Path, server: Path) -> None:
    tunnel_archives = {path.parent.relative_to(tunnel): path for path in tunnel.rglob("*.a")}
    server_archives = {path.parent.relative_to(server): path for path in server.rglob("*.a")}
    if tunnel_archives.keys() != server_archives.keys():
        raise RuntimeError("HEV XCFramework slices do not match")

    objcopy = find_objcopy()
    if objcopy:
        print(f"Using {objcopy} for symbol rewriting", flush=True)
    else:
        print("llvm-objcopy unavailable; using Xcode ld+nmedit symbol isolation", flush=True)

    for identifier, tunnel_archive in sorted(tunnel_archives.items(), key=lambda item: str(item[0])):
        server_archive = server_archives[identifier]
        arches = archive_arches(tunnel_archive)
        if arches != archive_arches(server_archive):
            raise RuntimeError(f"architecture mismatch in {identifier}")
        with tempfile.TemporaryDirectory(prefix="bajji-hev-") as temporary:
            temp = Path(temporary)
            patched: list[Path] = []
            for arch in arches:
                tunnel_thin = temp / f"tunnel-{arch}.a"
                server_thin = temp / f"server-{arch}.a"
                output = temp / f"server-patched-{arch}.a"
                thin(tunnel_archive, arch, tunnel_thin)
                thin(server_archive, arch, server_thin)
                collisions = sorted(
                    symbol for symbol in defined_globals(tunnel_thin) & defined_globals(server_thin)
                    if symbol not in SERVER_PUBLIC
                )
                if not collisions:
                    shutil.copy2(server_thin, output)
                else:
                    mapping = temp / f"mapping-{arch}.txt"
                    conflicts = temp / f"conflicts-{arch}.txt"
                    mapping.write_text("".join(
                        f"{symbol} _bajji_server_{symbol.lstrip('_')}\n" for symbol in collisions
                    ))
                    conflicts.write_text("".join(f"{symbol}\n" for symbol in collisions))
                    if objcopy:
                        objcopy_archive(objcopy, server_thin, mapping, output)
                    else:
                        native_isolate(server_thin, arch, str(identifier), mapping, conflicts, output)
                remaining = defined_globals(tunnel_thin) & defined_globals(output)
                if remaining:
                    raise RuntimeError(
                        f"{identifier}/{arch}: unresolved symbols: {', '.join(sorted(remaining)[:8])}"
                    )
                patched.append(output)
            if len(patched) == 1:
                shutil.copy2(patched[0], server_archive)
            else:
                run("xcrun", "lipo", "-create", *patched, "-output", server_archive)
            print(f"isolated {identifier}: {', '.join(arches)}", flush=True)


def main() -> None:
    THIRD_PARTY.mkdir(parents=True, exist_ok=True)
    SOURCES.mkdir(parents=True, exist_ok=True)
    frameworks: dict[str, Path] = {}
    for name, url, commit, framework in DEPENDENCIES:
        frameworks[framework] = build(checkout(name, url, commit), framework)
    isolate_pair(frameworks["HevSocks5Tunnel.xcframework"],
                 frameworks["HevSocks5Server.xcframework"])
    print(f"HEV forwarders ready in {THIRD_PARTY}", flush=True)


if __name__ == "__main__":
    main()
