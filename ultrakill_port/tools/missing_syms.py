#!/usr/bin/env python3
"""missing_syms.py - lista o que o libunity.so pede e ninguem fornece.

Cruzando:
  - undefined FUNC/OBJECT symbols de libunity.so (+ libil2cpp.so)
  - exports de: stubs em libs/arm64, .so nativos, glibc do sistema NAO
    esta disponivel aqui, entao assumimos que nomes libc-padroes existam.

Como nao temos o glibc de destino em disco, filtramos por heuristicas:
nomes que comecam com A(ALooper/ANative/ASensor/...), __android, __system,
Java_, SL (OpenSL), AMedia, AAsset, ANativeWindow - esses PRECISAM dos
nossos stubs. O resto (memcpy, cos, dlopen...) o glibc cobre.
"""
import sys
import os
from elftools.elf.elffile import ELFFile


def exports_of(path):
    e = ELFFile(open(path, 'rb'))
    ds = e.get_section_by_name('.dynsym')
    out = set()
    if not ds:
        return out
    for s in ds.iter_symbols():
        if s['st_shndx'] != 'SHN_UNDEF' and s.name:
            out.add(s.name)
    return out


def undef_of(path):
    e = ELFFile(open(path, 'rb'))
    ds = e.get_section_by_name('.dynsym')
    funcs, objs = set(), set()
    if not ds:
        return funcs, objs
    for s in ds.iter_symbols():
        if s['st_shndx'] == 'SHN_UNDEF' and s.name:
            t = s['st_info']['type']
            if t == 'STT_FUNC':
                funcs.add(s.name)
            elif t == 'STT_OBJECT':
                objs.add(s.name)
    return funcs, objs


def androidish(name):
    prefixes = ('A', '__android', '__system', 'Java_', 'SL', '_ZN7android',
                'OpenSL', 'ANative', 'ALooper', 'ASensor', 'AAsset',
                'AMedia', 'AConfiguration', 'AChoreographer', 'ASurface',
                'ATransaction', 'AImage', 'AHardware')
    if name.startswith(prefixes):
        return True
    # C++ mangled que referenciam android
    if name.startswith('_Z') and '6Shader' in name:
        return True
    return False


def main():
    libs_arm = sys.argv[1]
    native = sys.argv[2]
    target = sys.argv[3] if len(sys.argv) > 3 else 'libunity.so'

    provided = set()
    for d in (libs_arm, native):
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            if f.endswith('.so'):
                try:
                    provided |= exports_of(os.path.join(d, f))
                except Exception:
                    pass

    tp = os.path.join(native, target)
    funcs, objs = undef_of(tp)

    # funcs que Parecem android/c++ MAS nao estao providos
    miss_func = sorted(n for n in funcs if androidish(n) and n not in provided)
    miss_obj = sorted(n for n in objs if n not in provided)

    print(f'target={target}')
    print(f'# undefined FUNC total={len(funcs)}  # OBJECT total={len(objs)}')
    print(f'\n== OBJECT em falta (bloqueiam dlopen mesmo c/ RTLD_LAZY) {len(miss_obj)}:')
    for n in miss_obj:
        print('   ', n)
    print(f'\n== FUNC android-like em falta (lazy: dao abort no 1o call) {len(miss_func)}:')
    for n in miss_func:
        print('   ', n)


if __name__ == '__main__':
    main()
