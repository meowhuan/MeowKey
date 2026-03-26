#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import subprocess
import zipfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ReleaseArtifact:
    package_slug: str
    build_dir: str
    variant: str
    usage: str
    binary_stem: str
    debug_hid_enabled: bool | None
    preset_name: str | None = None
    preset_package_label: str | None = None
    preset_description: str | None = None


def run_command(command: list[str], workdir: Path) -> None:
    subprocess.run(command, cwd=workdir, check=True)


def bool_to_cmake(value: bool) -> str:
    return "ON" if value else "OFF"


def normalize_active_state(value: str) -> str:
    return "ON" if value.lower() == "low" else "OFF"


def preset_to_cmake_defs(config: dict[str, object]) -> dict[str, str]:
    return {
        "PICO_BOARD": str(config["board"]),
        "PICO_FLASH_SIZE_BYTES": str(int(config["flashSizeMB"]) * 1024 * 1024),
        "MEOWKEY_CREDENTIAL_CAPACITY": str(int(config["credentialCapacity"])),
        "MEOWKEY_CREDENTIAL_STORE_SECTORS": str(int(config["credentialStoreKB"]) // 4),
        "MEOWKEY_UP_DEFAULT_SOURCE": str(config["userPresenceSource"]),
        "MEOWKEY_UP_GPIO_PIN": str(int(config["userPresenceGpioPin"])),
        "MEOWKEY_UP_GPIO_ACTIVE_LOW": normalize_active_state(str(config["userPresenceGpioActiveState"])),
        "MEOWKEY_UP_TAP_COUNT": str(int(config["userPresenceTapCount"])),
        "MEOWKEY_UP_GESTURE_WINDOW_MS": str(int(config["userPresenceGestureWindowMs"])),
        "MEOWKEY_UP_REQUEST_TIMEOUT_MS": str(int(config["userPresenceTimeoutMs"])),
        "MEOWKEY_BOARD_ID_MODE": str(config["boardIdMode"]),
        "MEOWKEY_BOARD_ID_GPIO_PINS": str(config["boardIdGpioPins"]),
        "MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW": normalize_active_state(str(config["boardIdGpioActiveState"])),
        "MEOWKEY_BOARD_ID_I2C_PRESET": str(config["boardIdI2cPreset"]),
        "MEOWKEY_BOARD_ID_I2C_INSTANCE": str(int(config["boardIdI2cInstance"])),
        "MEOWKEY_BOARD_ID_I2C_SDA_PIN": str(int(config["boardIdI2cSdaPin"])),
        "MEOWKEY_BOARD_ID_I2C_SCL_PIN": str(int(config["boardIdI2cSclPin"])),
        "MEOWKEY_BOARD_ID_I2C_ADDRESS": str(config["boardIdI2cAddress"]),
        "MEOWKEY_BOARD_ID_I2C_MEM_OFFSET": str(int(config["boardIdI2cMemOffset"])),
        "MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH": str(int(config["boardIdI2cMemAddressWidth"])),
        "MEOWKEY_BOARD_ID_I2C_READ_LENGTH": str(int(config["boardIdI2cReadLength"])),
    }


def configure_and_build(
    repo_root: Path,
    build_dir: str,
    common_defs: dict[str, str],
    extra_defs: dict[str, str],
    target: str | None = None,
) -> None:
    cmake_args = [
        "cmake",
        "-S",
        ".",
        "-B",
        build_dir,
        "-G",
        "Ninja",
    ]
    merged_defs = common_defs | extra_defs
    for key, value in merged_defs.items():
        cmake_args.append(f"-D{key}={value}")
    run_command(cmake_args, repo_root)

    build_args = ["cmake", "--build", build_dir, "--parallel"]
    if target:
        build_args.extend(["--target", target])
    run_command(build_args, repo_root)


def load_release_presets(board_presets_path: Path) -> list[dict[str, object]]:
    with board_presets_path.open("r", encoding="utf-8") as handle:
        preset_file = json.load(handle)

    presets: list[dict[str, object]] = []
    for name, config in preset_file["presets"].items():
        if config.get("deprecatedAliasOf"):
            continue
        presets.append(
            {
                "name": name,
                "packageLabel": config.get("packageLabel", name),
                "releasePurpose": config.get("releasePurpose", name),
                "description": config.get("description", ""),
                "cmakeDefs": preset_to_cmake_defs(config),
            }
        )
    return presets


def ensure_clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def write_manifest(
    package_dir: Path,
    tag: str,
    version: dict[str, int],
    artifact: ReleaseArtifact,
) -> None:
    manifest = {
        "project": "MeowKey",
        "tag": tag,
        "variant": artifact.variant,
        "board": "meowkey_rp2350_usb",
        "usage": artifact.usage,
        "binaryStem": artifact.binary_stem,
        "presetName": artifact.preset_name,
        "presetPackageLabel": artifact.preset_package_label,
        "presetDescription": artifact.preset_description,
        "debugHidEnabled": artifact.debug_hid_enabled,
        "version": version,
    }
    (package_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )


def copy_flash_scripts(repo_root: Path, package_dir: Path) -> None:
    shutil.copy2(repo_root / "scripts" / "flash.ps1", package_dir / "flash.ps1")
    flash_sh_target = package_dir / "flash.sh"
    shutil.copy2(repo_root / "scripts" / "flash.sh", flash_sh_target)
    flash_sh_target.chmod(
        flash_sh_target.stat().st_mode
        | stat.S_IXUSR
        | stat.S_IXGRP
        | stat.S_IXOTH
    )


def package_artifact(
    repo_root: Path,
    dist_dir: Path,
    tag: str,
    version: dict[str, int],
    artifact: ReleaseArtifact,
) -> Path:
    build_dir = repo_root / artifact.build_dir
    package_name = f"meowkey-{tag}-{artifact.package_slug}"
    package_dir = dist_dir / package_name
    ensure_clean_dir(package_dir)

    for suffix in ("uf2", "bin", "hex", "elf", "elf.map"):
        source = build_dir / f"{artifact.binary_stem}.{suffix}"
        if source.exists():
            shutil.copy2(source, package_dir / source.name)

    generated_header = build_dir / "generated" / "meowkey_build_config.h"
    if generated_header.exists():
        shutil.copy2(generated_header, package_dir / generated_header.name)

    copy_flash_scripts(repo_root, package_dir)
    write_manifest(package_dir, tag, version, artifact)

    zip_path = dist_dir / f"{package_name}.zip"
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for source in sorted(package_dir.rglob("*")):
            archive.write(source, arcname=source.relative_to(package_dir.parent))

    return zip_path


def write_checksum_manifest(dist_dir: Path, zip_paths: list[Path]) -> Path:
    checksum_path = dist_dir / "SHA256SUMS.txt"
    with checksum_path.open("w", encoding="utf-8") as handle:
        for zip_path in sorted(zip_paths):
            digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
            handle.write(f"{digest}  {zip_path.name}\n")
    return checksum_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Build and package MeowKey release artifacts.")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--version-major", required=True, type=int)
    parser.add_argument("--version-minor", required=True, type=int)
    parser.add_argument("--version-patch", required=True, type=int)
    parser.add_argument("--pico-sdk-path", required=True)
    parser.add_argument("--picotool-fetch-path", required=True)
    parser.add_argument("--dist-dir", default="dist")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    dist_dir = repo_root / args.dist_dir
    dist_dir.mkdir(parents=True, exist_ok=True)
    for stale_path in dist_dir.iterdir():
        if stale_path.is_dir() and stale_path.name.startswith("meowkey-"):
            shutil.rmtree(stale_path)
        elif stale_path.is_file() and (stale_path.suffix == ".zip" or stale_path.name == "SHA256SUMS.txt"):
            stale_path.unlink()

    version = {
        "major": args.version_major,
        "minor": args.version_minor,
        "patch": args.version_patch,
    }

    common_defs = {
        "PICO_BOARD": "meowkey_rp2350_usb",
        "PICO_SDK_PATH": args.pico_sdk_path,
        "MEOWKEY_VERSION_MAJOR": str(args.version_major),
        "MEOWKEY_VERSION_MINOR": str(args.version_minor),
        "MEOWKEY_VERSION_PATCH": str(args.version_patch),
        "MEOWKEY_VERSION_LABEL": "",
        "PICOTOOL_FETCH_FROM_GIT_PATH": args.picotool_fetch_path,
    }

    configure_and_build(
        repo_root,
        "build-release-generic-debug",
        common_defs,
        {"MEOWKEY_ENABLE_DEBUG_HID": bool_to_cmake(True)},
    )
    configure_and_build(
        repo_root,
        "build-release-generic-debug",
        common_defs,
        {"MEOWKEY_ENABLE_DEBUG_HID": bool_to_cmake(True)},
        target="meowkey_probe",
    )
    configure_and_build(
        repo_root,
        "build-release-generic-hardened",
        common_defs,
        {"MEOWKEY_ENABLE_DEBUG_HID": bool_to_cmake(False)},
    )

    presets = load_release_presets(repo_root / "scripts" / "board-presets.json")
    for preset in presets:
        package_label = str(preset["packageLabel"])
        preset_defs = dict(preset["cmakeDefs"])

        configure_and_build(
            repo_root,
            f"build-release-preset-{package_label}-debug",
            common_defs,
            preset_defs | {"MEOWKEY_ENABLE_DEBUG_HID": bool_to_cmake(True)},
        )
        configure_and_build(
            repo_root,
            f"build-release-preset-{package_label}-hardened",
            common_defs,
            preset_defs | {"MEOWKEY_ENABLE_DEBUG_HID": bool_to_cmake(False)},
        )

    artifacts = [
        ReleaseArtifact(
            package_slug="generic-debug",
            build_dir="build-release-generic-debug",
            variant="debug",
            usage="generic",
            binary_stem="meowkey",
            debug_hid_enabled=True,
        ),
        ReleaseArtifact(
            package_slug="generic-hardened",
            build_dir="build-release-generic-hardened",
            variant="hardened",
            usage="generic",
            binary_stem="meowkey",
            debug_hid_enabled=False,
        ),
        ReleaseArtifact(
            package_slug="probe-board-id",
            build_dir="build-release-generic-debug",
            variant="probe",
            usage="board-id-detection",
            binary_stem="meowkey_probe",
            debug_hid_enabled=None,
        ),
    ]

    for preset in presets:
        package_label = str(preset["packageLabel"])
        usage = str(preset["releasePurpose"])
        description = str(preset["description"])
        preset_name = str(preset["name"])
        artifacts.extend(
            [
                ReleaseArtifact(
                    package_slug=f"preset-{package_label}-debug",
                    build_dir=f"build-release-preset-{package_label}-debug",
                    variant="debug",
                    usage=usage,
                    binary_stem="meowkey",
                    debug_hid_enabled=True,
                    preset_name=preset_name,
                    preset_package_label=package_label,
                    preset_description=description,
                ),
                ReleaseArtifact(
                    package_slug=f"preset-{package_label}-hardened",
                    build_dir=f"build-release-preset-{package_label}-hardened",
                    variant="hardened",
                    usage=usage,
                    binary_stem="meowkey",
                    debug_hid_enabled=False,
                    preset_name=preset_name,
                    preset_package_label=package_label,
                    preset_description=description,
                ),
            ]
        )

    zip_paths = [package_artifact(repo_root, dist_dir, args.tag, version, artifact) for artifact in artifacts]
    write_checksum_manifest(dist_dir, zip_paths)


if __name__ == "__main__":
    main()
