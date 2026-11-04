#!/usr/bin/env python3
"""unversion_elf.py - remove symbol version REQUIREMENTS de um ELF.

libunity.so/libil2cpp.so etc foram linkados no Android (Bionic), que marca
as symbol references com a versao "LIBC". No glibc (SpruceOS) essas versoes
nao existem -> loader falha com:
    libm.so: version `LIBC' not found (required by libunity.so)

Neutraliza o versionamento:
  1. .gnu.version (DT_VERSYM): cada indice -> 1 (VER_NDX_GLOBAL) = referencia
     SEM versao, que casa com o simbolo default do glibc.
  2. DT_VERNEEDNUM -> 0, para o loader nao validar nenhuma versao exigida.

Usa pyelftools so para LOCALIZAR offsets; a escrita e byte-level in-place
(mantem tamanho do arquivo). Requere: pip install pyelftools

Uso:
    python unversion_elf.py <arquivo1.so> [arquivo2.so ...]
    python unversion_elf.py --check <arquivo.so>
"""
import sys
import struct
from elftools.elf.elffile import ELFFile

DT_VERNEEDNUM = 0x6fffffff
DT_VERNEED    = 0x6ffffffe
DT_IGNORE     = 0x6ffffffa
DT_VERDEF     = 0x6ffffffc
DT_VERDEFNUM  = 0x6ffffffd
DT_VERSYM     = 0x6ffffff0


def _file_offset_of_vaddr(elf, vaddr):
    """Converte um endereco virtual de .dynamic em offset de arquivo."""
    for seg in elf.iter_segments():
        if seg['p_type'] == 'PT_LOAD':
            if seg['p_vaddr'] <= vaddr < seg['p_vaddr'] + seg['p_filesz']:
                return seg['p_offset'] + (vaddr - seg['p_vaddr'])
    return None


def unversion(path, check_only=False):
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    elf = ELFFile(open(path, 'rb'))
    is64 = (elf.elfclass == 64)

    # localiza secao .gnu.version (versym)
    vs = elf.get_section_by_name('.gnu.version')
    versym_off = vs['sh_offset'] if vs else None
    versym_size = vs['sh_size'] if vs else 0

    # localiza DT_VERNEEDNUM e DT_VERNEED dentro de .dynamic
    verneednum_file_off = None
    verneed_file_off = None
    dyn = elf.get_section_by_name('.dynamic')
    if dyn:
        d_off = dyn['sh_offset']
        d_ent = dyn['sh_entsize'] or (16 if is64 else 8)
        n = dyn['sh_size'] // d_ent
        for i in range(n):
            o = d_off + i * d_ent
            if is64:
                tag = struct.unpack_from('<q', data, o)[0]
            else:
                tag = struct.unpack_from('<i', data, o)[0]
            if tag == DT_VERNEEDNUM:
                verneednum_file_off = o
            elif tag == DT_VERNEED:
                verneed_file_off = o
            if tag == 0:  # DT_NULL - fim
                break

    rep = {'versym': (versym_size // 2) if versym_off is not None else 0,
           'verneednum_off': verneednum_file_off,
           'verneed_off': verneed_file_off}

    if not check_only:
        # 1. TODOS os versym -> 0 (includindo definidos). Se um definido
        #    ficar versym=2 apontando p/ DT_VERDEF, o glibc indexa
        #    map->l_versions[2]; mas como matamos DT_VERNEED, l_versions fica
        #    NULL/inconsistente -> SIGSEGV em qualquer dlsym/RTLD_DEFAULT que
        #    varra esta lib (foi exatamente o crash em shim_input_start).
        #    zerando tudo, nenhuma symbol referencia versao, l_versions nem e
        #    consultado. dlsym(hu,"nome") acha pelo nome de qualquer forma.
        if versym_off is not None:
            for i in range(0, versym_size, 2):
                struct.pack_into('<H', data, versym_off + i, 0)
        # 2. DT_VERNEEDNUM -> 0
        if verneednum_file_off is not None:
            if is64:
                struct.pack_into('<Q', data, verneednum_file_off + 8, 0)
            else:
                struct.pack_into('<I', data, verneednum_file_off + 4, 0)
        # 3. DT_VERNEED -> DT_IGNORE (mata o PONTEIRO da tabela de verneed;
        #    glibc segue o ponteiro DT_VERNEED, nao so o contador, entao
        #    so VERNEEDNUM=0 nao basta. Tornando o tag DT_IGNORE o loader
        #    nao ve DT_VERNEED nenhum e pula _dl_check_map_versions inteiro.)
        if verneed_file_off is not None:
            if is64:
                struct.pack_into('<q', data, verneed_file_off, DT_IGNORE)
            else:
                struct.pack_into('<i', data, verneed_file_off, DT_IGNORE)
        # 4. DT_VERDEF -> DT_IGNORE e DT_VERDEFNUM -> 0 (mesma logica, agora
        #    p/ os exports proprios da lib). Sem isto, 6 simbolos definidos
        #    com versym=2 ainda chamariam l_versions[2] = NULL.
        d_off = dyn['sh_offset'] if dyn else None
        d_ent = (dyn['sh_entsize'] if dyn else (16 if is64 else 8)) or 16
        d_cnt = (dyn['sh_size'] // d_ent) if dyn else 0
        for i in range(d_cnt):
            o = d_off + i * d_ent
            tag = struct.unpack_from('<q' if is64 else '<i', data, o)[0]
            if tag == DT_VERDEF:
                struct.pack_into('<q' if is64 else '<i', data, o, DT_IGNORE)
            elif tag == DT_VERDEFNUM:
                struct.pack_into('<Q' if is64 else '<I', data,
                                 o + (8 if is64 else 4), 0)
            # 5. DT_VERSYM -> DT_IGNORE: CRUCIAL. Sem VERNEED e sem VERDEF,
            #    o glibc nao alocaria l_versions[]; mas se DT_VERSYM continua
            #    presente, l_versyms != NULL e o codigo de symbol lookup faz
            #    &l_versions[idx] sobre NULL -> SIGSEGV em dl*() chamado dos
            #    construtores do libunity.so (foi o crash ld+0xe900 si_addr=8).
            #    Matando DT_VERSYM o glibc trata a lib como 100% sem versao.
            elif tag == DT_VERSYM:
                struct.pack_into('<q' if is64 else '<i', data, o, DT_IGNORE)
        with open(path, 'wb') as f:
            f.write(data)
    return rep


def main():
    args = sys.argv[1:]
    check = args and args[0] == '--check'
    if check:
        args = args[1:]
    if not args:
        print(__doc__)
        return 2
    for p in args:
        r = unversion(p, check_only=check)
        print(f'[{"check" if check else "patch"}] {p}: '
              f'versym_entries={r["versym"]} '
              f'verneednum_off={"0x%x" % r["verneednum_off"] if r["verneednum_off"] else "NAO-ACHOU"} '
              f'verneed_off={"0x%x" % r["verneed_off"] if r["verneed_off"] else "NAO-ACHOU"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
