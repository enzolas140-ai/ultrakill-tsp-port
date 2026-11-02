#!/usr/bin/env python3
"""
prepare_payload.py - transforma o payload StandaloneWindows64 do APK em
algo que o wrapper GLES/Android que a gente construiu pode tentar carregar.

Faz duas coisas:

  (A) PATH FIXUP  (deterministico, sempre roda)
  Unity's Addressables catalog.json guarda InternalIds com backslashes
  windows (ex: "{RuntimePath}\\StandaloneWindows64\\foo.bundle"). Em
  Linux, backslash NAO e separador - o runtime vira fopen() literal
  "aa/StandaloneWindows64\\foo.bundle". Para o fopen dar match:
     - Criamos hardlinks com backslash no nome do arquivo
     - Criamos aa/Android/ como symlink para aa/StandaloneWindows64/
  Assim, QUALQUER interpretacao de caminho que o Unity fazer, a gente
  atende.

  (B) SHADER STUB  (opcional, requer UnityPy)
  Para cada .bundle em StandaloneWindows64, abre com UnityPy, anda
  por cada Shader, substitui os blobs DXBC em m_ParsedForm.m_SubShaders
  por programas ESSL 300 trivial (return magenta). Salva como
  Android/<bundle>.bundle. Result: materiais renderizam flat-magenta,
  mas o boot NAO quebra em Shader::CreateProgram.

Se UnityPy nao esta instalado, (B) e pulado com um aviso. (A) sempre
roda, porque e o que destrava "file not found" que provavelmente ja
esta matando o boot antes de shader compilar.

Exit codes:
  0 = OK (path fixup feito; shader stub opcionalmente aplicado)
  1 = erro fatal (APK nao extraido ainda)
  2 = path fixup OK mas shader stub falhou - jogo vai crashar em Shader::CreateProgram

Rodar a partir da raiz do port:
  cd ultrakill_port/
  python3 tools/prepare_payload.py            # default: gamedata/
  python3 tools/prepare_payload.py --root X   # outro dir
  python3 tools/prepare_payload.py --no-stub  # so faz (A)
"""
from __future__ import annotations
import argparse
import json
import os
import shutil
import sys
from pathlib import Path

# ============================================================ (A) PATH FIXUP

def path_fixup(aa_dir: Path, verbose: bool = True) -> int:
    """Cria aa/Android/ e arquivos-literal-com-backslash apontando para
    cada bundle. Retorna numero de bundles cobertos."""
    win_dir = aa_dir / "StandaloneWindows64"
    if not win_dir.exists():
        print(f"[path_fixup] AVISO: {win_dir} nao existe - nada pra fazer")
        return 0
    and_dir = aa_dir / "Android"
    if and_dir.exists() or and_dir.is_symlink():
        # ja existe; se nao e symlink, deixa estar (o usuario pode ter
        # substituido manualmente)
        try:
            if and_dir.is_symlink() and os.readlink(and_dir) == str(win_dir):
                pass  # symlink correto ja presente
            elif and_dir.is_dir():
                # diretorio real com bundles Android - nao toca
                if verbose:
                    print(f"[path_fixup] {and_dir} existe como dir real, pulando symlink")
                return 0
        except OSError:
            pass
    else:
        and_dir.symlink_to(win_dir.name)
        if verbose: print(f"[path_fixup] symliked {and_dir} -> {win_dir.name}/")

    # varre todos os .bundle dentro e replica na forma "X\Y\Z.bundle"
    count = 0
    for bundle in win_dir.rglob("*.bundle"):
        # caminho relativo dentro de aa/
        rel = bundle.relative_to(win_dir)   # ex: "assets_assets_assets/foo.bundle"
        # Unity catalog tem "{RuntimePath}\StandaloneWindows64\<sub>\<name>.bundle"
        # O RuntimePath em Android resolve pra algo como "<dataPath>/aa".
        # Concatenado com "\StandaloneWindows64\<rel>" fica literal:
        #    "<dataPath>/aa\StandaloneWindows64\<rel>"
        # (porque o separador antes de StandaloneWindows64 é BACKSLASH tb!)
        # Criamos AMBAS as variantes plausiveis:
        #   a) aa/StandaloneWindows64\<sub>\<name>.bundle
        #   b) aa\StandaloneWindows64\<sub>\<name>.bundle
        #   c) aa/Android/<sub>/<name>.bundle  (via symlink ja feito)
        variants = [
            aa_dir / f"StandaloneWindows64\\{rel}",
            aa_dir.parent / f"{aa_dir.name}\\StandaloneWindows64\\{rel}",
        ]
        for v in variants:
            v: Path = Path(v)
            if v.exists():
                continue
            v.parent.mkdir(parents=True, exist_ok=True)
            try:
                v.hardlink_to(bundle)
            except (OSError, AttributeError):
                # filesystem do SD pode nao suportar hardlink; fallback
                v.symlink_to(os.path.relpath(bundle, v.parent))
            except Exception as e:
                print(f"[path_fixup] ERRO {v}: {e}")
                continue
        count += 1
    if verbose:
        print(f"[path_fixup] {count} bundles cobertos por backslash-literals + symlink Android/")
    return count


# ============================================================ (B) SHADER STUB
#
# Programa ESSL 300 minimista. Nao tem uniforms (Unity's material binder
# tolera uniforms faltantes em GLES; vira no-op).
# O vertex position precisa vir de _glesVertex que eh o nome interno do
# Unity p/ semantic position - o stub NAO da match com todos os inputs
# dos shaders originais, mas o link funciona porque o Unity usa
# glGetAttribLocation(-1 = nao-associado, ok).

STUB_ESSL_VERTEX = """#version 300 es
precision highp float;
in vec4 _glesVertex;
void main() { gl_Position = _glesVertex; }
"""

STUB_ESSL_FRAGMENT = """#version 300 es
precision highp float;
out vec4 _gl_FragColor;
void main() { _gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }
"""


def _unitypy_available() -> bool:
    try:
        import UnityPy  # noqa: F401
        return True
    except ImportError:
        return False


def shader_stub_pass(aa_dir: Path, verbose: bool = True) -> tuple[int, int]:
    """Retorna (bundles_processados, shaders_stubados)."""
    try:
        import UnityPy
    except ImportError:
        print("[shader_stub] UnityPy nao esta instalado - pulando stub.")
        print("              pip install UnityPy==1.17.22  && re-rodar")
        return (-1, 0)

    win = aa_dir / "StandaloneWindows64"
    if not win.exists():
        print(f"[shader_stub] {win} nao existe - pulando stub")
        return (-1, 0)

    # output para aa/Android_stubs/
    out_root = aa_dir / "Android_stubs"
    out_root.mkdir(exist_ok=True)

    n_bundles = 0
    n_shaders = 0

    for bundle in win.rglob("*.bundle"):
        rel = bundle.relative_to(win)
        try:
            env = UnityPy.load(str(bundle))
        except Exception as e:
            print(f"[shader_stub] falha ao abrir {bundle.name}: {e}")
            continue

        changed = False
        for obj in env.objects:
            if obj.type.name != "Shader":
                continue
            try:
                tree = obj.read_typetree()
            except Exception as e:
                if verbose:
                    print(f"  shader read_typetree() falhou: {e}")
                continue
            pf = tree.get("m_ParsedForm")
            if not isinstance(pf, dict):
                continue
            for sub in pf.get("m_SubShaders", []) or []:
                for p in sub.get("m_Passes", []) or []:
                    _patch_pass(p)
                    changed = True
            try:
                obj.save_typetree(tree)
                n_shaders += 1
            except Exception as e:
                if verbose:
                    print(f"  shader save_typetree() falhou: {e}")
                    continue

        # salva o bundle regravado
        outp = out_root / rel
        outp.parent.mkdir(parents=True, exist_ok=True)
        try:
            env.save(str(outp))
            n_bundles += 1
        except Exception as e:
            print(f"[shader_stub] falha ao salvar {outp}: {e}")

    if verbose:
        print(f"[shader_stub] bundles regravados: {n_bundles}")
        print(f"[shader_stub] shaders stubados:   {n_shaders}")
    return n_bundles, n_shaders


def _patch_pass(pass_dict: dict) -> None:
    """Substitui os campos GPU program por nossos stubs ESSL300."""
    # Unity 2019.4: ShaderPass tem:
    #   m_Program  (dict com:
    #     "subPrograms": [ {...}, ... ]  - um entry por GraphicsCaps::GraphicsDeviceType
    #     "baselineHi"/"baselineLo"
    #     ...)
    #   m_Script  (string HLSL original)
    #   m_Name
    prog = pass_dict.get("m_Program")
    if not isinstance(prog, dict):
        return
    subs = prog.get("subPrograms")
    if not isinstance(subs, list):
        return
    # stub: substitui cada subprogram por um blob ESSL
    for i, sub in enumerate(subs):
        if not isinstance(sub, dict):
            continue
        # heuristica: se entrada anterior era nonempty, assume DXBC.
        # Substitui por nossos stubs; mas mantem o count de subPrograms
        # porque Unity espera que o array tenha o mesmo length.
        sub["type"] = 2   # ShaderGpuProgramType::GLES30
        sub["entrypoint"] = "main"
        sub["keywords"] = []
        sub["needsDeskew"] = False
        sub["passFlags"] = 0
        sub["program"] = STUB_ESSL_VERTEX if i == 0 else STUB_ESSL_FRAGMENT
        sub["needsBlending"] = False
        sub["usesVaryingLocs"] = False
    pass_dict["m_Script"] = "// stub\n"


# ============================================================ CATALOG.JSON CHECK

def catalog_status(aa_dir: Path) -> str:
    """Abre catalog.json e reporta quantos InternalIds apontam pra Windows."""
    cat = aa_dir / "catalog.json"
    if not cat.exists():
        return "(catalog.json nao encontrado)"
    try:
        raw = json.loads(cat.read_text())
    except json.JSONDecodeError:
        return "(catalog.json existe mas e base64 binario interno, nao-decoded)"
    total_ids = raw.get("m_InternalIds", [])
    nwin = sum(1 for x in total_ids if "StandaloneWindows64" in x)
    nand = sum(1 for x in total_ids if "Android" in x)
    return f"catalog.json: {len(total_ids)} ids, {nwin} StandaloneWindows64, {nand} Android"


# ============================================================ MAIN

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--root", default="gamedata",
                    help="dir do port (default: ./gamedata)")
    ap.add_argument("--no-stub", action="store_true",
                    help="pula a parte (B) de shader stub, faz so path fixup")
    ap.add_argument("--strict", action="store_true",
                    help="exit !=0 se shader stub falhou (default: so avisa)")
    args = ap.parse_args()

    aa = Path(args.root) / "aa"
    if not aa.exists():
        print(f"ERRO: {aa} nao existe. Rode extract_payload.py antes.", file=sys.stderr)
        return 1

    print(f"== prepare_payload: root={args.root} ==")
    print(f"  {catalog_status(aa)}")

    # (A) path fixup
    n = path_fixup(aa)
    if n == 0:
        print("[A] nao achei bundles StandaloneWindows64 - rodou extract_payload.py?",
              file=sys.stderr)

    # (B) shader stub
    if args.no_stub:
        print("[B] shader stub pulado (--no-stub)")
        return 0

    if not _unitypy_available():
        print("[B] UnityPy ausente - pulando stub. Para instalar:")
        print("    python3 -m pip install --user UnityPy")
        if args.strict:
            return 2
        return 0

    print("[B] aplicando shader stub via UnityPy (isso demora ~10-20 min no A53)...")
    n_b, n_s = shader_stub_pass(aa)
    if n_b < 0:
        print("[B] falhou")
        if args.strict:
            return 2
    else:
        print(f"[B] OK: {n_b} bundles, {n_s} shaders stubados em gamedata/aa/Android_stubs/")
        print(f"    (para usar: mv gamedata/aa/Android gamedata/aa/Android_orig; "
              f"ln -s Android_stubs gamedata/aa/Android)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
