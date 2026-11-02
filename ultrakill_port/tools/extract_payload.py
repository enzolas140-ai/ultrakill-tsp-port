#!/usr/bin/env python3
"""
extract_payload.py - inventario e extracao do conteudo util do ULTRADROID.apk.

Por que isto e o primeiro passo real:
  o APK nao e um build Android puro. Ele embarca o CONTEUDO do build
  StandaloneWindows64 (Addressables com shaders DXBC) junto do player
  ARM nativo. Antes de qualquer tentativa de port voce precisa saber
  exatamente o que ha nas maos e quanto pesa descompactado.

Uso:
  # so inventario, nao escreve nada (use este primeiro - o payload sao ~720MB):
  python3 extract_payload.py ULTRADROID.apk --report

  # extracao real (cuidado: ~720MB de bundles + 24MB serialized):
  python3 extract_payload.py ULTRADROID.apk ./out --only bundles [--bench]

Requisitos: so stdlib (zipfile). --bench precisa de zstd ou gzip.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

BUNDLE_RE = re.compile(r"^(.+)\\.bundle$")

# ================================================================
# ProgressReporter: escreve uma barra de progresso ASCII em
# /tmp/status_message.txt (convencao PortMaster/ES) e em
# /dev/tty (se existirem). Usado em --dest para o usuario ver o
# avancar da extracao do APK no TSP.
# ================================================================
class ProgressReporter:
    STATUS_FILE = "/tmp/status_message.txt"
    TTY_CANDIDATES = ("/dev/tty1", "/dev/tty0", "/dev/console")

    def __init__(self, total_bytes: int, label: str, enabled: bool = True):
        self.total = max(1, int(total_bytes))
        self.label = label
        self.done = 0
        self.enabled = enabled
        self.last_pct = -1
        self.last_file = ""
        self.started_at = time.time()

    def tick(self, delta_bytes: int, current_file: str = ""):
        self.done += delta_bytes
        pct = min(100, int(100 * self.done / self.total))
        # atualiza UI so quando muda 2% ou quando arquivo mudou (evita
        # spam no SD card que pode ter wear-leveling caro)
        if pct != self.last_pct and (pct - self.last_pct >= 2 or pct == 100 or current_file != self.last_file):
            self.last_pct = pct
            self.last_file = current_file
            self.draw()

    def set_phase(self, phase: str):
        self.label = phase
        self.last_pct = -1
        self.draw()

    def draw(self):
        if not self.enabled: return
        bar_len = 24
        filled = int(bar_len * self.last_pct / 100)
        bar = "#" * filled + "-" * (bar_len - filled)
        elapsed = time.time() - self.started_at
        rate = (self.done / elapsed / 1e6) if elapsed > 0.1 else 0.0
        eta_s = ((self.total - self.done) / (rate * 1e6)) if rate > 0.1 else 0
        line1 = f"ULTRAKILL - {self.label}"
        line2 = f"[{bar}] {self.last_pct:3d}%  {self.done/1e6:5.1f}/{self.total/1e6:5.1f} MB"
        line3 = f"{self.last_file[:60] if self.last_file else ''}"
        line4 = f"{rate:.1f} MB/s  ETA {int(eta_s)}s"
        payload = f"\n{line1}\n{line2}\n{line3}\n{line4}\n"
        # status file (PortMaster convention)
        try:
            with open(self.STATUS_FILE, "w") as f:
                f.write(payload)
        except OSError:
            pass
        # console (best-effort)
        for tt in self.TTY_CANDIDATES:
            try:
                with open(tt, "w") as f:
                    f.write(payload)
                break
            except (OSError, PermissionError):
                continue
        # stdout pra shell caller tambem ter
        print(f"PROGRESS {self.last_pct:3d} {self.done} {self.total} {self.label}", flush=True)

# Relatorio contextual. compute_findings(z) inspeciona o APK e devolve
# os (path, note) relevantes - nao hardcodeia conclusoes de um APK
# especifico.
def compute_findings(z: zipfile.ZipFile):
    out = []
    names = set(z.namelist())
    has_win = any("StandaloneWindows64" in n for n in names)
    has_and = any("aa/Android/" in n for n in names)
    only_arm64 = ("lib/arm64-v8a/libunity.so" in names
                  and "lib/armeabi-v7a/libunity.so" not in names)

    if has_win:
        out.append(("assets/aa/catalog.json",
                    "m_InternalIds usam StandaloneWindows64 -> shaders DXBC, "
                    "um runtime GLES nao instancia"))
    if has_and:
        out.append(("assets/aa/Android/",
                    "OK - bundles Android/GLES3 nativos (shaders ESSL)"))
    if not has_win and not has_and:
        out.append(("assets/aa/",
                    "AMBIGUO - nem StandaloneWindows64 nem Android"))

    if only_arm64:
        out.append(("lib/",
                    "APENAS arm64-v8a - userspace do TSP precisa ser aarch64"))

    if "lib/arm64-v8a/libAPKVISION.so" in names:
        out.append(("lib/arm64-v8a/libAPKVISION.so",
                    "biblioteca extra (protecao? telemetria?) - "
                    "checar se o il2cpp chama e se falha sem network"))

    try:
        gm = z.read("assets/bin/Data/Managed/Metadata/global-metadata.dat")[:12]
        ver = int.from_bytes(gm[4:8], "little")
        out.append(("assets/bin/Data/Managed/Metadata/global-metadata.dat",
                    f"versao {ver} - precisa bater com libil2cpp.so embutido"))
    except Exception:
        pass

    try:
        d = z.read("assets/bin/Data/data.unity3d")[:100]
        m = re.search(rb"20[0-9][0-9]\.[0-9]+x?f?[0-9]+", d)
        if m:
            out.append(("assets/bin/Data/data.unity3d",
                        f"Unity {m.group().decode()}"))
    except Exception:
        pass
    return out


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:8.1f} {unit}"
        n /= 1024
    return f"{n:.1f} GB"


def classify(name):
    if name.startswith("assets/aa/"):
        return "addressables"
    if name.startswith("assets/bin/Data"):
        return "unity-serialized"
    if name.startswith("assets/Videos"):
        return "video"
    if name.startswith("assets/cgef"):
        return "editor-flutter"
    if name.startswith("assets/ChessEngine"):
        return "noise-stockfish"
    if name.startswith("lib/"):
        return "native-arm"
    if name.startswith(("res/", "META-INF/", "classes")):
        return "apk-boilerplate"
    return "other"


def bench_compress(data_path, sample_bytes=64 * 1024 * 1024):
    """Mede ganho real de recompactar payload (decide se zstd vale o CPU do A53)."""
    with open(data_path, "rb") as f:
        blob = f.read(sample_bytes)
    out = {}
    for algo, args in (("gzip", ["gzip", "-6", "-c"]),
                       ("zstd-3", ["zstd", "-3", "-c"]),
                       ("zstd-19", ["zstd", "-19", "-c"])):
        try:
            p = subprocess.run(args, input=blob, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, timeout=180, check=False)
            if p.returncode == 0 and p.stdout:
                out[algo] = len(p.stdout)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            out[algo] = None
    return len(blob), out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apk")
    ap.add_argument("dest", nargs="?", default=None)
    ap.add_argument("--report", action="store_true",
                    help="apenas inventario; nao extrai nada")
    ap.add_argument("--only", default="bundles",
                    help="prefixo de classe a extrair: bundles|native|serialized")
    ap.add_argument("--quiet", action="store_true",
                    help="nao atualiza barra de progresso")
    ap.add_argument("--bench", action="store_true",
                    help="mede compressibilidade do maior bundle")
    args = ap.parse_args()

    if not os.path.isfile(args.apk):
        sys.exit(f"nao encontrado: {args.apk}")

    if args.report:
        args.dest = None
    elif not args.dest:
        sys.exit("informe um diretorio de destino ou use --report "
                 "(o payload completo sao ~720MB descompactados)")

    if args.dest:
        os.makedirs(args.dest, exist_ok=True)
    zf = zipfile.ZipFile(args.apk)

    by_class = {}
    total = 0
    rows = []
    for info in zf.infolist():
        if info.is_dir():
            continue
        c = classify(info.filename)
        by_class.setdefault(c, [0, 0])
        by_class[c][0] += 1
        by_class[c][1] += info.file_size
        total += info.file_size
        rows.append((info.file_size, info.filename, c))

    rows.sort(reverse=True)
    print(f"\n=== {os.path.basename(args.apk)}: {len(rows)} arquivos, "
          f"{human(total)} descompactado ===")
    print(f"{'peso':>12}  {'classe':<20} caminho")
    for size, name, c in rows[:22]:
        print(f"{human(size)}  {c:<20} {name}")

    print("\n=== agregado por classe ===")
    for c, (n, sz) in sorted(by_class.items(), key=lambda kv: -kv[1][1]):
        pct = 100.0 * sz / total if total else 0
        print(f"{human(sz)}  {n:>4} arquivos  {pct:5.1f}%  {c}")

    print("\n=== achados (contextuais) ===")
    for path, note in compute_findings(zf):
        print(f"  {path}\n      -> {note}")

    want = {"bundles": ("addressables", "unity-serialized"),
            "native": ("native-arm",),
            "serialized": ("unity-serialized",),
            "all": None,
            "none": ()}
    key = (args.only or "none").lower()
    if key not in want:
        sys.exit(f"--only invalido: {args.only} "
                 f"(validos: {', '.join(sorted(want))})")
    keep = want[key]

    extracted = 0
    total_to_extract = sum(sz for sz, _, c in rows
                           if (keep is None or c in keep)
                           and c not in ("noise-stockfish", "editor-flutter",
                                          "video", "apk-boilerplate"))
    progress = ProgressReporter(total_to_extract,
                                 label="extraindo " + os.path.basename(args.apk),
                                 enabled=args.dest is not None and not args.quiet)
    for size, name, c in rows:
        if args.dest is None:
            break
        if keep and c not in keep:
            continue
        if c in ("noise-stockfish", "editor-flutter", "video",
                 "apk-boilerplate") and keep != ("addressables",):
            continue  # irrelevante para o runtime do jogo
        target = os.path.join(args.dest, name)
        # Unity AAssetManager le caminhos RELATIVOS ao /assets/ dentro
        # do APK. No nosso stub de Linux, o asset root e gamedata/.
        # Entao assets/aa/foo.bundle (no zip) vira gamedata/aa/foo.bundle
        # (no disco) - SEM o prefixo assets/. Ja assets/bin/Data/* vira
        # gamedata/Data/* (o "bin" tambem e redundante).
        if name.startswith("assets/bin/"):
            rel = name[len("assets/bin/"):]
        elif name.startswith("assets/"):
            rel = name[len("assets/"):]
        else:
            rel = name
        target = os.path.join(args.dest, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with zf.open(name) as src, open(target, "wb") as dst:
            # chunked com report p/ barra de progresso
            while True:
                chunk = src.read(1024 * 1024)
                if not chunk: break
                dst.write(chunk)
                progress.tick(len(chunk), current_file=name)
        extracted += 1
    if args.dest and not args.quiet:
        progress.set_phase("OK - extracao concluida")
        progress.draw()
    zf.close()
    if args.dest:
        print(f"\nextraido: {extracted} arquivos -> {os.path.abspath(args.dest)}")
    else:
        print("\n--report: nada extraido.")

    # Sumario das Addressables: o que cada bundle carrega, pra planejar o strip.
    aadir = os.path.join(args.dest or ".", "assets", "aa", "StandaloneWindows64")
    if args.dest and os.path.isdir(aadir):
        print("\n=== bundles (alvo do asset strip) ===")
        for root, _, files in os.walk(aadir):
            for fn in sorted(files):
                if fn.endswith(".bundle"):
                    p = os.path.join(root, fn)
                    print(f"{human(os.path.getsize(p))}  "
                          f"{os.path.relpath(p, aadir)}")

    if args.bench and args.dest:
        big = None
        for size, name, c in rows:
            if c == "addressables" and size > 50 * 1024 * 1024:
                big = os.path.join(args.dest, name)
                break
        if big and os.path.isfile(big):
            raw, res = bench_compress(big)
            print(f"\n=== compressibilidade ({os.path.basename(big)}) ===")
            for algo, sz in res.items():
                if sz is None:
                    print(f"  {algo:<8} ferramenta ausente")
                else:
                    print(f"  {algo:<8} {human(sz)} ({100.0*sz/raw:.1f}% de {human(raw)})")
            print("  leitura: abaixo de ~40% com zstd-19, vale reempacotar; "
                  "se precisar de -19 para chegar la, o custo de CPU do A53 nao compensa.")


if __name__ == "__main__":
    main()
