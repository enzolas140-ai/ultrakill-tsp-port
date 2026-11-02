#!/usr/bin/env python3
"""build_lab_payload.py - monta lab-payload.tar.gz p/ o workflow lab-test.

Extrai do ULTRADROID.apk SOMENTE o necessario pro boot do Unity:
  - lib/arm64-v8a/*.so            (re-patcheados com unversion_elf)
  - assets/Data/**                (data.unity3d, global-metadata, boot.config, mono)
  - assets/aa/Androidsettings.json, addressables_link, catalog*  (o .bundle
    gigante de niveles NOVA incluida; so faz falta depois do menu)
  - arquivos de player.log/etc    (nada)

Nao inclue libSDL2 (CI usa o libsdl2 do apt) nem Videos (pula intro).

Uso: python build_lab_payload.py <apk> <saida.tar.gz>
"""
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import zipfile

BOOT_BUNDLE_MAX = 40_000_000   # bundles ate 40 MB vao no payload (menu/default)

def main():
    apk, out = sys.argv[1], sys.argv[2]
    work = tempfile.mkdtemp(prefix="labpay_")
    gd = os.path.join(work, "gamedata")
    os.makedirs(gd)

    z = zipfile.ZipFile(apk)
    nlib = ndata = naa = 0
    for info in z.infolist():
        n = info.filename
        if info.file_size == 0:
            continue
        # 1. native libs
        if n.startswith("lib/arm64-v8a/"):
            dst = os.path.join(gd, "lib", "arm64-v8a", os.path.basename(n))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "wb").write(z.read(n))
            nlib += 1
            continue
        # 2. Data inteira (no APK real: assets/bin/Data/**)
        if n.startswith("assets/bin/Data/"):
            rel = n[len("assets/bin/"):]
            dst = os.path.join(gd, *rel.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "wb").write(z.read(n))
            ndata += 1
            continue
        # 2b. boot.config as avesso em assets/bin/Data (cobre tb assets/Data)
        if n.startswith("assets/Data/"):
            rel = n[len("assets/"):]
            dst = os.path.join(gd, *rel.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "wb").write(z.read(n))
            ndata += 1
            continue
        # 3. aa: configs + catalog + bundles pequenos (assets/aa/Android/**)
        if n.startswith("assets/aa/"):
            base = os.path.basename(n)
            keep = (not base.endswith(".bundle")) or info.file_size <= BOOT_BUNDLE_MAX
            if keep:
                rel = n[len("assets/"):]
                dst = os.path.join(gd, *rel.split("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "wb").write(z.read(n))
                naa += 1
            continue

    # 4. re-patch unversion (idempotente; o CI tb faz, mas garantir)
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "unversion_elf.py")
    libd = os.path.join(gd, "lib", "arm64-v8a")
    sos = [os.path.join(libd, f) for f in os.listdir(libd) if f.endswith(".so")]
    if os.path.exists(tool) and sos:
        subprocess.call([sys.executable, tool] + sos)

    # 5. tar.gz
    with tarfile.open(out, "w:gz") as t:
        t.add(gd, arcname="gamedata")

    size = os.path.getsize(out) / 1e6
    print(f"libs={nlib} data={ndata} aa={naa}  ->  {out} ({size:.1f} MB)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
