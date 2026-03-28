#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path


def require_regex(content: str, pattern: str, description: str, failures: list[str]) -> None:
    if re.search(pattern, content, flags=re.MULTILINE) is None:
        failures.append(description)


def require_substring(content: str, needle: str, description: str, failures: list[str]) -> None:
    if needle not in content:
        failures.append(description)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    manager_channel = (repo_root / "src" / "manager_channel.c").read_text(encoding="utf-8")
    credential_header = (repo_root / "src" / "credential_store.h").read_text(encoding="utf-8")
    credential_store = (repo_root / "src" / "credential_store.c").read_text(encoding="utf-8")
    manager_repo = (
        repo_root
        / "windows"
        / "gui"
        / "MeowKey.Manager"
        / "Services"
        / "ManagerRepository.cs"
    ).read_text(encoding="utf-8")
    manager_device = (
        repo_root
        / "windows"
        / "gui"
        / "MeowKey.Manager"
        / "Services"
        / "ManagerDeviceService.cs"
    ).read_text(encoding="utf-8")
    build_config_template = (repo_root / "src" / "meowkey_build_config.h.in").read_text(encoding="utf-8")

    failures: list[str] = []

    require_regex(manager_channel, r"MEOWKEY_MANAGER_CMD_AUTHORIZE\s*=\s*0x05u", "missing AUTHORIZE command id (0x05)", failures)
    require_regex(manager_channel, r"MEOWKEY_MANAGER_CMD_DELETE_CREDENTIAL\s*=\s*0x06u", "missing DELETE_CREDENTIAL command id (0x06)", failures)
    require_regex(
        manager_channel,
        r"MEOWKEY_MANAGER_CMD_SET_USER_PRESENCE_PERSISTED\s*=\s*0x07u",
        "missing SET_USER_PRESENCE_PERSISTED command id (0x07)",
        failures,
    )
    require_regex(
        manager_channel,
        r"MEOWKEY_MANAGER_CMD_SET_USER_PRESENCE_SESSION\s*=\s*0x08u",
        "missing SET_USER_PRESENCE_SESSION command id (0x08)",
        failures,
    )
    require_regex(
        manager_channel,
        r"MEOWKEY_MANAGER_CMD_CLEAR_USER_PRESENCE_SESSION\s*=\s*0x09u",
        "missing CLEAR_USER_PRESENCE_SESSION command id (0x09)",
        failures,
    )

    require_substring(
        manager_channel,
        "#if MEOWKEY_MANAGER_REQUIRE_AUTH_FOR_SUMMARIES",
        "credential summary auth gate compile guard missing",
        failures,
    )
    require_substring(
        manager_channel,
        "manager_auth_verify(request.auth_token, MEOWKEY_MANAGER_PERMISSION_CREDENTIAL_READ)",
        "credential summary auth verification missing",
        failures,
    )
    require_substring(
        manager_channel,
        "send_response(MEOWKEY_MANAGER_STATUS_AUTH_REQUIRED, command, NULL, 0u);",
        "auth-required response path missing",
        failures,
    )

    require_substring(
        manager_channel,
        "MEOWKEY_MANAGER_PERMISSION_CREDENTIAL_READ",
        "credential-read permission constant missing",
        failures,
    )
    require_substring(
        manager_channel,
        "MEOWKEY_MANAGER_PERMISSION_CREDENTIAL_WRITE",
        "credential-write permission constant missing",
        failures,
    )
    require_substring(
        manager_channel,
        "MEOWKEY_MANAGER_PERMISSION_USER_PRESENCE_WRITE",
        "user-presence-write permission constant missing",
        failures,
    )

    require_substring(
        credential_header,
        "bool meowkey_store_delete_credential_by_slot(uint32_t slot_index);",
        "credential-store delete-by-slot declaration missing",
        failures,
    )
    require_substring(
        credential_store,
        "bool meowkey_store_delete_credential_by_slot(uint32_t slot_index)",
        "credential-store delete-by-slot implementation missing",
        failures,
    )

    if manager_repo.count("Manager.Policy.HardenedRequired") < 3:
        failures.append("WinUI manager hardened-policy lock should guard delete/persist/session-clear paths")
    if manager_repo.count("if (device.DebugHidEnabled)") < 3:
        failures.append("WinUI manager should block write actions when debug HID is enabled")

    require_regex(manager_device, r"private const byte AuthorizeCommand = 0x05;", "manager device service missing authorize command id", failures)
    require_regex(manager_device, r"private const ushort CredentialReadPermission = 0x0004;", "manager device service missing read permission bit", failures)
    require_regex(manager_device, r"private const ushort CredentialWritePermission = 0x0001;", "manager device service missing write permission bit", failures)

    require_substring(
        build_config_template,
        "#define MEOWKEY_ENABLE_SIMULATED_SECURE_ELEMENT",
        "build config template missing simulated secure element macro",
        failures,
    )
    require_substring(
        build_config_template,
        "#define MEOWKEY_MANAGER_REQUIRE_AUTH_FOR_SUMMARIES",
        "build config template missing summary-auth macro",
        failures,
    )

    if failures:
        print("[verify-manager-contract] FAILED:")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print("[verify-manager-contract] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
