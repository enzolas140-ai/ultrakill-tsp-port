#!/usr/bin/env python3
"""rename_needed.py - renomeia uma entrada DT_NEEDED dentro de um ELF.

Uso: python rename_needed.py <lib.so> <old> <new>

Regra: o novo nome precisa caber no mesmo slot de string (<= antigo) OU o
byte imediatamente apos a string antiga precisa ser 0xFF/0x00 (limite de
strtab), senao aborta sem escrever. Nao mexe em hash/versao - so no .dynstr.
Usado pelo CI lab para trocar DT_NEEDED 'libEGL.so' -> 'libegl_shim.so' sem
brigar com o Mesa real (mesmo SONAME libEGL.so.1 colide no loader).
"""
import sys
import struct
from elftools.elf.elffile import ELFFile


def main():
    path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
    data = bytearray(open(path, "rb").read())
    elf = ELFFile(open(path, "rb"))
    is64 = elf.elfclass == 64
    dyn = elf.get_section_by_name(".dynamic")
    strtab = elf.get_section_by_name(".dynstr")
    assert dyn and strtab, "sem .dynamic/.dynstr"

    DT_NEEDED = 1
    ent = dyn['sh_entsize'] or (16 if is64 else 8)
    n = dyn['sh_size'] // ent
    hits = 0
    for i in range(n):
        o = dyn['sh_offset'] + i * ent
        tag = struct.unpack_from('<q' if is64 else '<i', data, o)[0]
        val = struct.unpack_from('<Q' if is64 else '<I', data, o + (8 if is64 else 4))[0]
        if tag == 0:
            break
        if tag != DT_NEEDED:
            continue
        # val e offset em .dynstr
        so = strtab['sh_offset'] + val
        end = data.find(b"\x00", so)
        name = data[so:end].decode(errors="replace")
        if name != old:
            continue
        nb = new.encode()
        room = len(nb) + 1
        if room <= (end - so):
            data[so:so + room] = nb + b"\x00"
            hits += 1
        else:
            after = data[end + 1] if end + 1 < len(data) else 0
            if after in (0, 0xFF):
                data[so:so + len(nb)] = nb
                data[so + len(nb)] = 0
                hits += 1
            else:
                print(f"SKIP {path}: '{new}' nao cabe apos '{old}' e proximo byte={after:#x} ocupado")
                return 2
    if hits == 0:
        print(f"WARN {path}: DT_NEEDED '{old}' nao achado (talvez ja renomeado)")
        return 0
    open(path, "wb").write(data)
    print(f"OK {path}: '{old}' -> '{new}' x{hits}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
