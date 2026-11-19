# NBTEvo Android Auto Installer: file transfer

Offline investigation, 2026-11-10. Target: the user-supplied
`/home/mobyfab/src/reverse/nbtevo-aa-installer.exe`, product version 1.0.3.

## Finding

The installer ships **Plink**, and uploads files through a normal SSH command's
standard input. Its `upload_file` routine runs a remote **`cat`**, with the local
file opened in binary mode as Plink's stdin. It does not need SCP or an SFTP
subsystem for this transfer. No SCP/PSCP executable was present in the complete
66-file Inno payload inventory.

This conclusion combines REA's static artifact/native analysis with separately
decoded constants and file-backed disassembly. It is a reconstruction of the
compiled behavior, not recovered original Python source or observed car traffic.

## Artifact identity and extraction

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Original installer | 32,766,912 | `43d6efc3b151f23f6a324b9478aed5ef9f8f216bd9d5c4684d6b03abe8ccc17f` |
| `app/nbtevo_aa_installer.exe` | 17,978,864 | `a10af4f92730ec1203af0a0b3b158a423e844e86c85593240be5b255ae5e6227` |
| `app/dependencies/plink.exe` | 1,037,936 | `50479953865b30775056441b10fdcb984126ba4f98af4f64756902a807b453e7` |

The wrapper is Inno Setup 6.7.0, containing a Python 3.12/PySide6 application
compiled to native x86-64 code with Nuitka. REA reports no CLI/.NET directory
in either executable; a managed C# decompiler is not the right route.

The older distro innoextract 1.9 could not parse this loader. Extraction succeeded
with [innoextract PR 205](https://github.com/dscharrer/innoextract/pull/205),
revision `e8960a05eef5ea218bf05a1e23adc3f688c3d5b5`, built in an
isolated directory. No installer code was run during extraction.

The user subsequently supplied the Wine-installed directory:
`~/.var/app/com.usebottles.bottles/data/bottles/bottles/Apps/drive_c/Program Files (x86)/NBTEvo Android Auto Installer/`.
Its app and Plink hashes match the extracted artifacts exactly. The installed
tree has 68 files, including uninstall material, versus 66 payload files.
Only those two executables were retained from the disposable extraction after
recording every payload file's name, size and digest; the original installer is
unchanged.

## Transfer sequence

1. `find_plink` locates the bundled `dependencies/plink.exe`, with an app-folder
   fallback. This is a Windows desktop SSH client, not a head-unit SCP helper.
2. `upload_file` requires a full match of this remote temporary-path pattern:
   `/tmp/nbtevo_aa_installer(?:/release)?/[A-Za-z0-9._-]+`.
   Other destinations raise `Unsafe remote upload path`.
3. It builds this SSH command from the validated path:

   ```sh
   cat > '<remote temporary path>' && echo __UP_OK__
   ```

4. `_plink_args` supplies SSH mode, port, batch mode, an explicit host key,
   username/password authentication and the remote command. Transfers use
   non-terminal SSH I/O; the helper also has a terminal option for other commands.
5. It opens the local file as `rb` and calls `subprocess.run` with `stdin=source`,
   captured output and a timeout. The bytes travel through SSH stdin to `cat`;
   closing input gives the remote command EOF. This path has no base64 encoding
   or SCP/SFTP protocol exchange.
6. It checks the process return code and the `__UP_OK__` marker in stdout.
   Failures include decoded stderr in the upload error. The marker is an upload
   acknowledgement, not a package signature, content digest or durable commit.

The function uses its temporary staging paths; subsequent installation is a
separate phase. Other observed constants include paths under
`/net/hu-omap/fs/sda0/opt/aa` and its HMI plugin directory. That does not establish
that arbitrary filesystem layouts, permissions or processor routes work.

## Native anchors and decoded metadata

Addresses below refer to the exact x86-64 executable hash above, preferred
image base `0x140000000`; they are not process addresses under Wine/ASLR.

| Anchor | Meaning |
| --- | --- |
| `0x14002a510` / loop `0x14002a574` | Main module's constants initialization/decoding |
| RVA `0xae3c30`, file offset `0xae2a30` | Encoded main constants, `0xf577` bytes; decoded body `0xf567` bytes after 16 warm-up bytes |
| `0x14110c040` | Main module constant-pointer table, 1,706 records |
| `0x140047b70` | `_plink_args`, recovered code-object source line 388 |
| `0x140048200` | `run_remote`, recovered code-object source line 393 |
| `0x14004bf40` | `upload_file`, recovered code-object source line 508 |
| `0x14004c0a4` | Upload destination `fullmatch` call setup |
| `0x14004c14d`, `0x14004c21e` | Command prefix/suffix references (constant slots 346/347) |
| `0x14004c2eb` | `rb` mode tuple (slot 95) |
| `0x14004c40a`, `0x14004c4ae` | `subprocess.run` and `stdin` references (slots 267/348) |
| `0x14004c9fe`, `0x14004ca77` | Return-code and acknowledgement checks (slots 290/349) |

The module's constants are protected by a per-block byte-substitution table,
feedback state and eight position-dependent adjustments. Raw strings searches
therefore miss transfer text. A bounded standalone decoder followed the shipped
instructions; the main metadata parser consumed all 1,706 records and stopped
at the final terminator. Its mapping is cross-checked against the compiled
constant-pointer references, including code-object names, arguments and line
numbers. This analysis reads data and instructions; it does not execute target
code or produce a replacement installer.

REA 6.8 Evidence retained in the local scratch directory:

| Operation | Evidence ID |
| --- | --- |
| Installer artifact inventory | `ev_36076007492fbe89f5eaf73215cd85d14053a39050bc5574acad1ac866808274` |
| Extracted app classification | `ev_978c0a9e98a7fd5074ff74f29c7115d16fed5ba17b28cab3cb48113082effc43` |
| App PE resources | `ev_f0b432283f2018778d9d7c4d8fab6a37b4d20ac197d0a050769c367de9d90e14` |
| Native `upload_file` function | `ev_b8e71a57e404af0838bb59978ef502b3a819edbeb43a429199decf04cd3f40ba` |
| Native `_plink_args` function | `ev_d7e4b4b183cd20ca94278a97cdbab0ffb39f0346f5da0da4b50084daea2cf562` |
| Native `run_remote` function | `ev_0d32493d7a8471428e25324584a406c564d6a4e0f820881a154c43e6d9f648e2` |

The upload dossier exposes the call argument dictionary assigning the open file
to `stdin`, `capture_output=True`, and the supplied timeout. Its result handling
reads `returncode` and uses `PySequence_Contains` for the acknowledgement in
stdout. The argument-builder dossier corroborates the Plink flags and credential
symbol lookups; no credential values are reproduced here.

Hopper's pseudocode uses inferred native signatures and unnamed globals; the
Python names above come from the separately decoded metadata. Complete function
body ranges are unavailable through Hopper's public API, and indirect call
coverage is partial. These limits remain in the saved dossiers. The native
session was explicitly closed with verified cleanup and a saved Evidence
snapshot. An initial typed-string search for `plink` returned no matches
(`ev_b2c635ae57cae6c504711dc1eec69adbc178bcd2cdf5fcef508e53e40897738b`);
protected constants explain why that search alone cannot establish absence.

## Implications for NBT Manager

Our desktop installer already uses the same transport principle with its Rust
SSH client: `desktop/src/ssh.rs` opens an exec channel, writes binary chunks to
stdin, sends EOF, then checks acknowledgement and exit status. Initial bootstrap
uses `cat` for the temporary launcher, each command in a login shell as typed
in an SSH session; subsequent signed streams go to the launcher itself.

There is no need to ship Plink or add an SCP dependency to reproduce this
transfer. Keep our existing host-key verification, bounded streams, fixed path
policy, separate manager signing key and on-device release verification. The
other app's upload marker alone supplies none of those installation guarantees.
The observed transfer is useful corroboration of a shell/stdin approach; our
processor routing, permissions, executable storage and end-to-end behavior still
need bench validation.

## Reproduction and limits

Local scratch evidence is in ignored `build/installer-analysis/`: extraction
inventory/digests, `decoded-index.json`, `main-constants.json`, static disassembly,
REA JSON and its saved native snapshot. Binaries, decoded constants, credentials
and proprietary implementation were not added to Git. The decoder and parser in
that scratch directory are investigation helpers, not production tools or a
full generic Nuitka reader. Upstream compiler format references were consulted
from [Nuitka's constants reader](https://github.com/Nuitka/Nuitka/blob/develop/nuitka/build/static_src/HelpersConstantsBlob.c);
the shipped tags and flag bits were read from this executable, not assumed to
match current upstream.

REA was upgraded from 6.4.0 to **6.8.0** during the investigation. After the user
reinstalled Hopper, its launcher was accepted. Native import needed the returned
remediation `REA_HOPPER_STARTUP_TIMEOUT_MS=600000` rather than the two-minute
default. A short `TMPDIR` symlink into the ignored build directory avoided both
`/tmp` quota and the Unix-socket path limit. MCP was not connected to this chat;
REA's CLI and supported stdio MCP interface were used without editing user config.

No head-unit connection, uploaded release, network capture or destructive test
was performed. Actual car execution of this third-party transfer remains
unobserved. The absence claim concerns this specific payload inventory and
identified upload path, not every possible behavior of every version.
