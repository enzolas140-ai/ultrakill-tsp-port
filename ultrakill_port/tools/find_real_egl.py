#!/usr/bin/env python3
"""find_real_egl.py - acha um libEGL aarch64 que EXPORTA as funcoes publicas.

Le o .dynsym de cada candidato (ELF64 parse puro, sem elftools/py na device)
e escolhe o primeiro que exporta eglGetDisplay + eglInitialize +
eglGetProcAddress. Imprime o caminho ou NONE.

Isso e necessario porque o ArkOS4Clone/R36C:
  - libEGL.so.1 / .so.1.0.0 -> todos symlink do blob bifrost WRONG
    (user 10.6 vs kernel utgard 11.7)
  - libEGL_mesa.so.0 pode ser (a) vendor glvnd puro (exports so
    __eglMainVND -> inutil sozinho) ou (b) build mesa completo com
    API publica -> USAVEL direto.
Uso: python3 find_real_egl.py [candidato ...]
"""
import os
import struct
import sys

DEFAULTS = [
    "/usr/lib/aarch64-linux-gnu/libEGL_mesa.so.0.0.0",
    "/usr/lib/aarch64-linux-gnu/libEGL.so.1.0.0",
    "/usr/lib/aarch64-linux-gnu/libEGL.so.1",
    "/usr/lib/libEGL.so.1.0.0",
    "/usr/lib/libEGL.so.1",
]
WANT = {"eglGetDisplay", "eglInitialize", "eglGetProcAddress"}


def dynsym_names(path):
    """Retorna set de nomes DEFINIDOS (st_shndx != SHN_UNDEF) em .dynsym."""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 2:
        return set()
    e_shoff = struct.unpack_from("<Q", d, 0x28)[0]
    e_shentsz = struct.unpack_from("<H", d, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", d, 0x3C)[0]
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsz
        sh_type = struct.unpack_from("<I", d, o + 4)[0]
        sh_offset = struct.unpack_from("<Q", d, o + 24)[0]
        sh_size = struct.unpack_from("<Q", d, o + 32)[0]
        sh_link = struct.unpack_from("<I", d, o + 40)[0]
        sh_entsize = struct.unpack_from("<Q", d, o + 56)[0]
        secs.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))
    out = set()
    for (sh_type, sh_offset, sh_size, sh_link, sh_entsize) in secs:
        if sh_type != 11:     # SHT_DYNSYM (6 e DYNAMIC!)
            continue
        if sh_link >= len(secs):
            continue
        _t, str_off, str_size, _l, _e = secs[sh_link]
        strtab = d[str_off:str_off + str_size]
        ent = sh_entsize or 24
        for i in range(sh_size // ent):
            o = sh_offset + i * ent
            st_name = struct.unpack_from("<I", d, o)[0]
            st_info = d[o + 4]
            st_shndx = struct.unpack_from("<H", d, o + 6)[0]
            if st_shndx == 0:      # SHN_UNDEF
                continue
            end = strtab.find(b"\x00", st_name)
            nm = strtab[st_name:end].decode(errors="replace")
            if (st_info & 0xF) == 2 or True:   # FUNC ou OBJECT, tanto faz
                out.add(nm)
        break
    return out


def main():
    cands = sys.argv[1:] or DEFAULTS
    for c in cands:
        try:
            rp = os.path.realpath(c).lower()
            if "mali" in rp and "mesa" not in rp:
                sys.stderr.write(f"  find_egl: {c} -> {rp} :: BLOB mali disfarçado, pulando\n")
                continue
            names = dynsym_names(c)
        except OSError:
            continue
        have = WANT & names
        status = "USAVEL" if have == WANT else ("vendor-only?" if names else "sem dynsym")
        sys.stderr.write(f"  find_egl: {c} :: {status} ({sorted(have)})\n")
        if have == WANT:
            print(c)
            return 0
    print("NONE")
    return 1


if __name__ == "__main__":
    main()
