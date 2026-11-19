#!/usr/bin/env python3
"""Build the Windows in-car test kit.

Produces dist/nbt-manager-windows-kit-<version>.zip (staged in build/windows-kit/):
the Windows installer (manager release and test application v1 embedded),
test application v2 and a deliberately broken v3 to upload from the phone,
the controller settings example and the in-car procedure.

Signing keys live outside the repository, in NBT_KEYS_DIR (default
~/.local/share/nbt-manager/keys), and are created on first use: one manager
key and one test-application key, RSA-3072, mode 600. Only public keys and
signed files go into the kit. Each build takes the next manager release
sequence from that directory, so a newer kit can update an older install.

Requires the QNX 6.5 environment (qcc on PATH), QNX_SCREEN_LIBDIR,
RHMI_LIBDIR, Rust with the x86_64-pc-windows-gnu target and MinGW.
"""

import argparse
import hashlib
import json
import re
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from build_release import publish  # tools/ is the script directory

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build")).resolve()
# Files ready to ship; BUILD holds only intermediate output.
DIST = Path(os.environ.get("DIST_DIR", ROOT / "dist")).resolve()
# The signed release the installer embeds: a build input, so not in dist/,
# which receives only the finished kit.
RELEASE = BUILD / "kit-release"
KEYS = Path(os.environ.get("NBT_KEYS_DIR", Path.home() / ".local/share/nbt-manager/keys"))
MANAGER_COMPAT = "nbt-evo-dev"
TEST_COMPAT = "nbt-test-1"
WINDOWS = "x86_64-pc-windows-gnu"


def run(*args, env=None, **kwargs):
    print("+", " ".join(str(a) for a in args), flush=True)
    return subprocess.run([str(a) for a in args], check=True, env=env, cwd=ROOT, **kwargs)


def keypair(name):
    private, public = KEYS / f"{name}-private.pem", KEYS / f"{name}-public.pem"
    if not private.exists():
        old = os.umask(0o077)
        try:
            run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", private)
        finally:
            os.umask(old)
        print(f"Created {private}", flush=True)
    if private.stat().st_mode & 0o077:
        raise ValueError(f"{private} must not be readable by others (chmod 600)")
    derived = subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout"], check=True,
                             capture_output=True).stdout
    if not public.exists() or public.read_bytes() != derived:
        public.write_bytes(derived)
    return private, public


def next_sequence():
    path = KEYS / "manager-sequence"
    current = int(path.read_text()) if path.exists() else 0
    return current + 1, path


def check_tools():
    for tool in ("openssl", os.environ.get("QCC", "qcc"), "cargo", "x86_64-w64-mingw32-gcc"):
        if not shutil.which(tool):
            raise ValueError(f"missing tool: {tool} (source the QNX 6.5 environment first)")
    for variable in ("QNX_SCREEN_LIBDIR", "RHMI_LIBDIR"):
        if not os.environ.get(variable):
            raise ValueError(f"set {variable}")
    targets = subprocess.run(["rustup", "target", "list", "--installed"], capture_output=True, text=True)
    if WINDOWS not in targets.stdout.split():
        raise ValueError(f"install the Rust target: rustup target add {WINDOWS}")


def check_phone_tools():
    """The phone app's installer core: Rust for Android, built with cargo-ndk."""
    targets = subprocess.run(["rustup", "target", "list", "--installed"], capture_output=True, text=True)
    if "aarch64-linux-android" not in targets.stdout.split():
        raise ValueError("install the Rust target: rustup target add aarch64-linux-android")
    if not shutil.which("cargo-ndk"):
        raise ValueError("install cargo-ndk: cargo install cargo-ndk")


# The test application's settings (apps/test/main.cpp reads NBT_SETTING_BACKGROUND and
# NBT_SETTING_CONTROLLER).
TEST_SETTINGS = ("background|Background color|blue|blue,red,green,black",
                 "controller|Use the controller|on|on,off")


def package(source, version, sequence, key, output):
    settings = [argument for setting in TEST_SETTINGS for argument in ("--setting", setting)]
    run(sys.executable, ROOT / "tools/package.py", "--app", "test", "--version", version, "--sequence", sequence,
        "--compat", TEST_COMPAT, "--key", key, "--output", output, "--bridge", "1.0", "--bridge-uses", "show,status,live-settings", *settings, source)


def phone_options():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--without-phone-app", action="store_true", help="build the car-only milestone-1 kit")
    parser.add_argument("--phone-app", type=Path, default=ROOT.parents[1] / "mobile/nbt-manager")
    return parser.parse_args()


def build_phone(options, kit, sequence, env):
    if options.without_phone_app:
        (kit / "phone/APP-STATUS.txt").write_text("Car-only kit. NBT Manager App 0.1 is required for signed phone access; milestone 2 is pending.\n")
        return
    app = options.phone_app.resolve()
    script = app / "scripts/build-release.sh"
    if not script.is_file():
        raise ValueError("phone build script missing; implement App 0.1 or explicitly use --without-phone-app")
    version = json.loads((app / "package.json").read_text())["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("invalid phone application version")
    expected = (app / "android-release-cert.sha256").read_text().strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("missing release certificate fingerprint")
    # The app's script signs with its external keystore, checks the signer itself and
    # writes NBT-Manager-<version>-<code>.apk; the kit's sequence is the version code.
    staging = BUILD / "phone-release"
    if staging.exists():
        shutil.rmtree(staging)
    # The app embeds the installer core from this repository, with the same signed
    # release and test application as the Windows installer.
    run(script, sequence, staging, env=env)
    output = kit / f"phone/NBT-Manager-{version}.apk"
    shutil.copy2(staging / f"NBT-Manager-{version}-{sequence}.apk", output)
    signer = os.environ.get("APKSIGNER") or shutil.which("apksigner") or str(
        sorted(Path(os.environ["ANDROID_HOME"]).glob("build-tools/*/apksigner"))[-1])
    verified = subprocess.run([signer, "verify", "--print-certs", str(output)],
                              capture_output=True, text=True, check=True)
    # apksigner prints "Signer #1 certificate SHA-256 digest:" or "V2 Signer: certificate ...".
    fingerprints = {value.replace(":", "").lower() for value in re.findall(
        r"(?:Signer #\d+|V[0-9.]+ Signer:) certificate SHA-256 digest: ([0-9a-fA-F:]+)", verified.stdout)}
    if fingerprints != {expected}:
        raise ValueError("APK signer does not match the committed release certificate")


def main():
    options = phone_options()
    if not options.without_phone_app and not (options.phone_app / "scripts/build-release.sh").is_file():
        raise ValueError("phone build script missing; use --without-phone-app for milestone 1")
    check_tools()
    if not options.without_phone_app:
        check_phone_tools()
    KEYS.mkdir(parents=True, exist_ok=True, mode=0o700)
    KEYS.chmod(0o700)
    manager_private, manager_public = keypair("manager")
    # The apps key signs every application package; its public half is
    # bundled with the manager (compiled into the worker). The existing
    # test-app key is reused, so packages signed before keep working.
    app_private, app_public = keypair("test-app")
    sequence, sequence_file = next_sequence()
    version = f"dev{sequence}"

    make = os.environ.get("MAKE", "make")
    # The GUI shows its version in the main menu's banner. qnx-check refuses
    # artifacts that must not ship (test hooks, missing libstdc++ symbols).
    run("nice", make, "-j", os.cpu_count() or 1, "host", "qnx", "qnx-plugin", "qnx-check", f"BUILD_DIR={BUILD}", f"MANAGER_VERSION={version}",
        f"APPS_PUBLIC_KEY={app_public}")
    # A manager whose worker lacks the apps key would accept no application package.
    key_line = next(line for line in app_public.read_bytes().splitlines()[1:] if line)
    if key_line not in (BUILD / "nbt-managerd-qnx").read_bytes():
        raise ValueError("the worker does not carry the apps public key")
    # One release per kit. The installers' Update stages it with its own
    # launcher, so a car with an older launcher needs no older-format release
    # first (docs/install-flow.md).
    release_env = dict(os.environ, MANAGER_VERSION=version, MANAGER_SEQUENCE=str(sequence),
                       MANAGER_COMPAT=MANAGER_COMPAT, MANAGER_SIGNING_KEY=str(manager_private),
                       MANAGER_PUBLIC_KEY=str(manager_public), BUILD_DIR=str(BUILD),
                       DIST_DIR=str(RELEASE))
    run(sys.executable, ROOT / "tools/build_release.py", "check", env=release_env)
    run(sys.executable, ROOT / "tools/build_release.py", "package", env=release_env)

    kit = BUILD / "windows-kit"
    if kit.exists():
        shutil.rmtree(kit)
    (kit / "phone").mkdir(parents=True)
    staging = BUILD / "windows-kit-packages"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir()
    v1 = staging / "test-v1.nbt"
    package(BUILD / "test-v1-qnx", "v1", 1, app_private, v1)
    package(BUILD / "test-v2-qnx", "v2", 2, app_private, kit / "phone/test-v2.nbt")
    package(BUILD / "test-v3-broken-qnx", "v3", 3, app_private, kit / "phone/test-v3-broken.nbt")
    for path in (v1, kit / "phone/test-v2.nbt", kit / "phone/test-v3-broken.nbt"):
        run(BUILD / "nbt-verify", path, app_public, "test", TEST_COMPAT, stdout=subprocess.DEVNULL)
    embedded = dict(NBT_MANAGER_RELEASE=str(RELEASE / "manager.nbtr"), NBT_MANAGER_PUBLIC_KEY=str(manager_public),
                    NBT_TEST_APP_PACKAGE=str(v1), NBT_TEST_APP_KEY=str(app_public), NBT_TEST_APP_COMPAT=TEST_COMPAT)
    build_phone(options, kit, sequence, dict(os.environ, NBT_INSTALLER_SOURCE=str(ROOT), **embedded))

    # Host tools (build scripts) link natively; the installer links with MinGW.
    rustc = subprocess.run(["rustc", "-vV"], check=True, capture_output=True, text=True).stdout
    host = next(line.removeprefix("host: ") for line in rustc.splitlines() if line.startswith("host: "))
    host_var = host.upper().replace("-", "_")
    cargo_env = dict(os.environ, **embedded,
                     CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="x86_64-w64-mingw32-gcc",
                     CC_x86_64_pc_windows_gnu="x86_64-w64-mingw32-gcc",
                     **{f"CARGO_TARGET_{host_var}_LINKER": "gcc", f"CC_{host.replace('-', '_')}": "gcc"})
    target_dir = BUILD / "desktop"
    run("cargo", "build", "--release", "--locked", "--target", WINDOWS, "--manifest-path",
        ROOT / "desktop/Cargo.toml", "--target-dir", target_dir, env=cargo_env)
    shutil.copy2(target_dir / WINDOWS / "release/nbt-manager-installer.exe", kit / "NBT-Manager-Installer.exe")
    # The same signed release, for the phone app: a car whose manager is this
    # version or newer updates its manager from the phone (PROTOCOL.md).
    (kit / "phone").mkdir(exist_ok=True)
    shutil.copy2(RELEASE / "manager.nbtr", kit / f"phone/NBT-Manager-{version}.nbtr")
    shutil.copy2(ROOT / "config/controller.conf.example", kit / "controller.conf.example")
    shutil.copy2(ROOT / "docs/in-car-procedure.md", kit / "PROCEDURE.md")
    (kit / "usb").mkdir()
    shutil.copy2(ROOT / "tools/car_inventory.sh", kit / "usb/car_inventory.sh")
    shutil.copy2(ROOT / "tools/car_network_diagnostics.sh", kit / "usb/car_network_diagnostics.sh")
    shutil.copy2(ROOT / "docs/car-inventory.md", kit / "CAR-INVENTORY.md")
    shutil.copy2(ROOT / "docs/gamepad-bridge.md", kit / "usb/GAMEPAD.md")

    release_sha = hashlib.sha256((RELEASE / "manager.nbtr").read_bytes()).hexdigest()
    (kit / "VERSION.txt").write_text(
        f"NBT Manager {version} (release sequence {sequence}, compatibility {MANAGER_COMPAT})\n"
        f"Manager release SHA-256: {release_sha}\n"
        f"Test application compatibility: {TEST_COMPAT}; v1 is inside the installer.\n", newline="\r\n")
    # No line of either private key may appear in any kit file. (The installer
    # contains PEM labels such as "PRIVATE KEY" from its libraries, so search
    # for the keys' own base64 lines rather than for labels.)
    secrets = [line for key in (manager_private, app_private) for line in key.read_bytes().splitlines()
               if len(line) >= 40 and not line.startswith(b"-----")]
    sums = []
    for path in sorted(p for p in kit.rglob("*") if p.is_file()):
        data = path.read_bytes()
        if any(secret in data for secret in secrets):
            raise ValueError(f"private key material in {path}")
        sums.append(f"{hashlib.sha256(data).hexdigest()}  {path.relative_to(kit).as_posix()}")
    (kit / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", newline="\r\n")
    archive = DIST / f"nbt-manager-windows-kit-{version}.zip"
    partial = BUILD / f"{archive.name}.partial"
    with zipfile.ZipFile(partial, "w", zipfile.ZIP_DEFLATED) as zip_file:
        for path in sorted(p for p in kit.rglob("*") if p.is_file()):
            zip_file.write(path, Path(f"nbt-manager-{version}") / path.relative_to(kit))
    publish(partial, archive)
    sequence_file.write_text(f"{sequence}\n")
    print(f"\nKit: {archive}\nManager {version}, release SHA-256 {release_sha}\nKeys: {KEYS}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, StopIteration, subprocess.CalledProcessError) as error:
        sys.exit(f"windows kit build failed: {error}")
