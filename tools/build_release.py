#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build and embed a signed manager release using workstation-only signing keys."""

import argparse
import errno
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from package_release import TOKEN, VERSION

ROOT = Path(__file__).resolve().parent.parent


def dist_dir():
    """Where files ready to ship go; BUILD_DIR holds only intermediate output."""
    return Path(os.environ.get("DIST_DIR", ROOT / "dist")).resolve()


def publish(staged, shipped):
    """Moves a finished file from build/ into dist/ in one rename, so dist/
    never holds partial or temporary files. Across filesystems it is copied
    under a hidden name first."""
    shipped.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.replace(staged, shipped)
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
        temporary = shipped.with_name(f".{shipped.name}.tmp")
        try:
            shutil.copy2(staged, temporary)
            os.replace(temporary, shipped)
        finally:
            temporary.unlink(missing_ok=True)
            staged.unlink(missing_ok=True)


def required(name):
    value = os.environ.get(name, "")
    if not value:
        raise ValueError(f"set {name}")
    return value


def configuration():
    version = required("MANAGER_VERSION")
    compatibility = required("MANAGER_COMPAT")
    sequence = required("MANAGER_SEQUENCE")
    if (not VERSION.fullmatch(version) or not TOKEN.fullmatch(compatibility) or
            not sequence.isascii() or not sequence.isdecimal() or
            not 0 < int(sequence) < 2**64 or str(int(sequence)) != sequence):
        raise ValueError("invalid manager version, compatibility or release sequence")
    public = Path(required("MANAGER_PUBLIC_KEY")).resolve(strict=True)
    build = Path(os.environ.get("BUILD_DIR", "build")).resolve()
    return version, compatibility, sequence, public, build


def check():
    _, _, _, public, _ = configuration()
    private = Path(required("MANAGER_SIGNING_KEY")).resolve(strict=True)
    for tool in ("openssl", os.environ.get("QCC", "qcc")):
        if not shutil.which(tool):
            raise ValueError(f"required build tool unavailable: {tool}")
    for variable, libraries in (("QNX_SCREEN_LIBDIR", ("libscreen.so",)),
                               ("RHMI_LIBDIR", ("libAppFramework.A.so", "libCoreFramework.A.so"))):
        directory = Path(required(variable))
        for library in libraries:
            if not (directory / library).is_file():
                raise ValueError(f"missing {library} in {variable}")
    # Compare public DER only; never print or copy private key material.
    def public_der(path, public_input):
        command = ["openssl", "pkey", "-in", str(path), "-pubout", "-outform", "DER"]
        if public_input:
            command.append("-pubin")
        result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True)
        if result.returncode:
            raise ValueError("cannot read signing key pair; use an unencrypted build signing key")
        return result.stdout
    if public_der(private, False) != public_der(public, True):
        raise ValueError("MANAGER_PUBLIC_KEY does not match MANAGER_SIGNING_KEY")


def verify(package, public, compatibility, build):
    result = subprocess.run([str(build / "nbt-verify-release"), str(package), str(public),
                             compatibility], check=True, capture_output=True, text=True)
    version, _, sequence, _, _ = configuration()
    if result.stdout.splitlines()[:2] != [f"version={version}", f"sequence={sequence}"]:
        raise ValueError("signed release does not match requested version and sequence")
    print(result.stdout, end="", flush=True)


def package():
    version, compatibility, sequence, public, build = configuration()
    # Publish only a complete, independently verified package. A failed build
    # preserves the previous release; private keys are never copied.
    destination = dist_dir()
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-", dir=build) as temporary:
        output = Path(temporary) / "manager.nbtr"
        command = [
            sys.executable, str(ROOT / "tools/package_release.py"),
            "--launcher", str(build / "nbt-manager-launcher"),
            "--gui", str(build / "nbt-manager-gui"),
            "--worker", str(build / "nbt-managerd-qnx"),
            "--plugin-dir", str(build / "NbtManager"),
            "--key", required("MANAGER_SIGNING_KEY"), "--output", str(output),
            "--version", version, "--sequence", sequence, "--compat", compatibility,
        ]
        capture = os.environ.get("MANAGER_CAPTURE_LIB", "")
        if capture:
            command.extend(["--capture-lib", capture])
        subprocess.run(command, check=True)
        verify(output, public, compatibility, build)
        publish(output, destination / "manager.nbtr")
    print(f"Signed release: {destination / 'manager.nbtr'}", flush=True)


def installer_dir():
    """The NBTFoundry/installer checkout (NBT_INSTALLER_DIR, default ../installer)."""
    directory = Path(os.environ.get("NBT_INSTALLER_DIR", ROOT.parent / "installer")).resolve()
    if not (directory / "Cargo.toml").is_file():
        raise ValueError(f"no installer workspace at {directory} (set NBT_INSTALLER_DIR)")
    return directory


def installer():
    _, compatibility, _, public, build = configuration()
    release = dist_dir() / "manager.nbtr"
    verify(release, public, compatibility, build)
    environment = dict(os.environ, NBT_MANAGER_RELEASE=str(release),
                       NBT_MANAGER_PUBLIC_KEY=str(public))
    # Rust's default `cc` linker may resolve to QNX after sourcing its SDK.
    rustc = subprocess.run(["rustc", "-vV"], check=True, capture_output=True, text=True)
    host = next((line.removeprefix("host: ") for line in rustc.stdout.splitlines()
                 if line.startswith("host: ")), None)
    if not host:
        raise ValueError("cannot determine native Rust target")
    linker_variable = "CARGO_TARGET_" + host.upper().replace("-", "_") + "_LINKER"
    environment.setdefault(linker_variable, os.environ.get("CC", "gcc"))
    workspace = installer_dir()
    cargo = os.environ.get("CARGO", "cargo")
    if not shutil.which(cargo):
        raise ValueError(f"required build tool unavailable: {cargo}")
    target = Path(os.environ.get("INSTALLER_TARGET_DIR", str(build / "installer"))).resolve()
    subprocess.run([cargo, "build", "--release", "--locked",
                    "--manifest-path", str(workspace / "Cargo.toml"),
                    "--target-dir", str(target)], env=environment, check=True)
    executable = "nbt-manager-installer.exe" if os.name == "nt" else "nbt-manager-installer"
    shipped = dist_dir() / executable
    staged = build / f"{executable}.staged"
    shutil.copy2(target / "release" / executable, staged)
    publish(staged, shipped)
    print(f"Installer: {shipped}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", choices=("check", "package", "installer"))
    args = parser.parse_args()
    try:
        {"check": check, "package": package, "installer": installer}[args.step]()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"release build failed: {error}\n")


if __name__ == "__main__":
    main()
