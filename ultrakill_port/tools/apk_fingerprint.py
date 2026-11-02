#!/usr/bin/env python3
"""
apk_fingerprint.py - responde UMA pergunta sobre um APK do Ultradroid:
"ele embarca assets compilados para Android (GLES) ou para Windows (DXBC)?"

Metodo:
  1. Abre o APK (zip)
  2. Le `assets/aa/settings.json` -> campo `m_buildTarget`
  3. Le `assets/aa/catalog.json` -> m_InternalIds (base64 decoded)
  4. Lista as subpastas em `assets/aa/`
  5. Le o header de 1 bundle representativo -> "Type" flag do UnityFS

Veredito:
  [A] Build Android real        -> prossegue com nosso wrapper
  [B] Android-with-Windows-data -> precisa de re-export OU stub
  [C] Ambiguo                    -> olha output manualmente

Rodar:
  python3 tools/apk_fingerprint.py /caminho/pra/qualquer.apk
"""
from __future__ import annotations
import base64
import json
import re
import struct
import sys
import zipfile


def read_entry(z: zipfile.ZipFile, path: str) -> bytes | None:
    try:
        with z.open(path) as f:
            return f.read()
    except KeyError:
        return None


def verdict(z: zipfile.ZipFile, apk_name: str) -> int:
    print(f"=== fingerprint: {apk_name} ===\n")

    # 1. settings.json - m_buildTarget
    sj = read_entry(z, "assets/aa/settings.json")
    if sj is None:
        print("ERRO: assets/aa/settings.json ausente - nem e um build Unity com Addressables")
        return 3
    try:
        s = json.loads(sj)
    except json.JSONDecodeError:
        print("(settings.json nao-e-json valido - possivelmente ofuscado)")
        s = {}
    build_target = s.get("m_buildTarget", "?")
    addr_version = s.get("m_AddressablesVersion", "?")
    print(f"  settings.json: m_buildTarget={build_target}, Addressables={addr_version}")

    # 2. catalog.json - InternalIds dentro dos blobs base64
    cj = read_entry(z, "assets/aa/catalog.json")
    n_win_ids = 0
    n_and_ids = 0
    n_linux_ids = 0
    n_total_ids = 0
    win_sample = and_sample = None
    if cj is not None:
        try:
            cat = json.loads(cj)
        except json.JSONDecodeError:
            cat = {}
        for key in ("m_KeyDataString", "m_EntryDataString", "m_InternalIds"):
            v = cat.get(key)
            blob = None
            if isinstance(v, str):
                try:
                    blob = base64.b64decode(v)
                except Exception:
                    pass
            elif isinstance(v, list):
                # m_InternalIds e uma lista de strings JSON diretas
                for s2 in v:
                    n_total_ids += 1
                    if "StandaloneWindows64" in s2:
                        n_win_ids += 1
                        win_sample = win_sample or s2
                    elif "Android" in s2:
                        n_and_ids += 1
                        and_sample = and_sample or s2
                    elif "StandaloneLinux" in s2 or "Linux64" in s2:
                        n_linux_ids += 1
                continue
            if blob:
                for m in re.finditer(rb"StandaloneWindows64", blob):
                    n_win_ids += 1
                    win_sample = win_sample or blob[max(0, m.start()-20):m.end()+50].decode("ascii", "replace")
                for m in re.finditer(rb"Android", blob):
                    n_and_ids += 1
                    and_sample = and_sample or blob[max(0, m.start()-20):m.end()+40].decode("ascii", "replace")

    print(f"  catalog.json: {n_win_ids} ids StandaloneWindows64, {n_and_ids} ids Android, {n_linux_ids} Linux")
    if win_sample: print(f"    win sample: ...{win_sample[:110]}")
    if and_sample: print(f"    and sample: ...{and_sample[:110]}")

    # 3. subpastas em assets/aa/
    subdirs = set()
    for name in z.namelist():
        if name.startswith("assets/aa/") and name.count("/") >= 3:
            subdirs.add(name.split("/")[2])
    print(f"  subpastas de assets/aa/: {sorted(subdirs)}")

    # 4. header de um .bundle representativo
    probe_bundles = []
    for sd in sorted(subdirs):
        for name in z.namelist():
            if name.startswith(f"assets/aa/{sd}/") and name.endswith(".bundle"):
                probe_bundles.append(name)
                break
    for pb in probe_bundles[:3]:
        info = z.getinfo(pb)
        with z.open(pb) as f:
            hdr = f.read(80)
        if not hdr.startswith(b"UnityFS"):
            continue
        # UnityFS header: [unityVerStr (cstr)] [serializerVer (cstr)] [big-endian u32 fileLen] [u32 compressedLen][...]
        parts = []
        i = 7
        while i < len(hdr) and hdr[i] != 0:
            i += 1
        i += 1
        end = i
        while end < len(hdr) and hdr[end] != 0:
            end += 1
        ver = hdr[i:end].decode("ascii", "replace")
        # flags uint32 big-endian logo depois de fileLen
        # skip cstr + 4-byte fileLen
        try:
            tail = hdr[end+1:]
            file_len = struct.unpack(">I", tail[0:4])[0]
            comp = struct.unpack(">I", tail[4:8])[0]
            flags = struct.unpack(">I", tail[8:12])[0]
        except Exception:
            flags = 0
        comp_type = flags & 0x3f
        comp_names = {0: "LZMA", 1: "LZ4", 2: "LZ4HC", 3: "none"}
        print(f"  {pb}: unityVer={ver}, compress={comp_names.get(comp_type, comp_type)}")

    # 5. il2cpp native sizes (evidence de ARM)
    names = set(z.namelist())
    for arch in ("arm64-v8a", "armeabi-v7a"):
        ipath = f"lib/{arch}/libil2cpp.so"
        upath = f"lib/{arch}/libunity.so"
        li = z.getinfo(ipath) if ipath in names else None
        lu = z.getinfo(upath) if upath in names else None
        if li is None and lu is None:
            print(f"  lib/{arch}: (ausente - APK nao tem este ABI)")
            continue
        a = f"libil2cpp={li.file_size/1e6:.1f} MB" if li else "libil2cpp=--"
        b = f"libunity={lu.file_size/1e6:.1f} MB" if lu else "libunity=--"
        print(f"  lib/{arch}: {a}, {b}")

    # VEREDITO
    print()
    has_windows = n_win_ids > 0 or "StandaloneWindows64" in subdirs
    has_android_bundles = "Android" in subdirs
    real_android = build_target.lower() in ("android",) and has_android_bundles and not has_windows

    print("=" * 60)
    if real_android:
        print("[A] BUILD ANDROID REAL - shaders ESSL, texturas ETC2/ASTC.")
        print("    >>> nosso wrapper deve funcionar sem modify")
        print("    >>> proximo passo: apontar extract_payload.py p/ este APK")
        return 0
    elif has_windows and build_target.lower() == "android":
        print("[B] APK ANDROID COM PAYLOAD WINDOWS (mesmo caso do ULTRADROID 900MB).")
        print("    >>> o m_buildTarget diz Android mas so tem bundles StandaloneWindows64.")
        print("    >>> wrapper NAO roda sem re-export.")
        return 1
    elif has_windows and build_target.lower() != "android":
        print("[B] Build nao-Android (Windows/Linux puro em APK wrapper).")
        return 1
    else:
        print("[C] AMBIGUO - nenhuma subpasta Android nem Windows achada.")
        print("    >>> olha assets/aa/ manualmente")
        return 2


def main():
    if len(sys.argv) != 2:
        print("uso: apk_fingerprint.py /caminho/para/arquivo.apk")
        return 9
    apk = sys.argv[1]
    try:
        with zipfile.ZipFile(apk, "r") as z:
            return verdict(z, apk)
    except zipfile.BadZipFile:
        print("ERRO: nao e um zip valido (APK corrompido ou nao-APK)", file=sys.stderr)
        return 8


if __name__ == "__main__":
    sys.exit(main())
