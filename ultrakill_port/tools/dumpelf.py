#!/usr/bin/env python3
"""dumpelf.py - inspeciona ELF (NEEDED, imports, exports) sem toolchain no PC.

Uso:
  python tools/dumpelf.py <arquivo.so>              # resumo
  python tools/dumpelf.py <arquivo.so> --undef      # todos imports
  python tools/dumpelf.py <arquivo.so> --def        # todos exports (nome+tipo+size)
  python tools/dumpelf.py <arquivo.so> --str SL_IID # strings que casam o padrao

Serve para responder, com evidencia e nao com chute:
  * o que o libunity.so realmente importa (e portanto o que o shim tem que
    entregar),
  * quais nomes/objetos o FMOD procura via dlsym quando abre o OpenSL ES,
  * se o nosso libOpenSLES.so embarcado bate com isso.
"""
import argparse
import re
import sys

from elftools.elf.elffile import ELFFile


def show_needed(e):
    d = e.get_section_by_name('.dynamic')
    if not d:
        print('  (sem .dynamic)')
        return
    for tag in d.iter_tags():
        if tag.entry.d_tag in ('DT_NEEDED', 'DT_SONAME'):
            val = getattr(tag, 'needed', None) or getattr(tag, 'soname', '')
            print('  %-10s %s' % (tag.entry.d_tag.split('_')[-1], val))


def symbols(e, want_undef):
    ds = e.get_section_by_name('.dynsym')
    if not ds:
        return
    for s in ds.iter_symbols():
        if not s.name:
            continue
        undef = s['st_shndx'] == 'SHN_UNDEF'
        if undef != want_undef:
            continue
        info = s['st_info']
        print('  %-8s %-10s size=%-6d %s' % (info['type'], info['bind'],
                                             s['st_size'], s.name))


def strings(e, pattern, limit):
    rx = re.compile(pattern.encode('latin1'))
    seen = set()
    for sec in e.iter_sections():
        if sec['sh_type'] not in ('SHT_PROGBITS', 'SHT_NOBITS'):
            continue
        data = sec.data()
        for m in rx.finditer(data):
            tok = m.group()
            if len(tok) < 4 or tok in seen:
                continue
            seen.add(tok)
            print('  %s' % tok.decode('latin1'))
            if len(seen) >= limit:
                return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    ap.add_argument('--undef', action='store_true', help='lista imports')
    ap.add_argument('--def', dest='defined', action='store_true',
                    help='lista exports')
    ap.add_argument('--str', dest='pattern', help='regex de strings')
    ap.add_argument('--limit', type=int, default=200)
    a = ap.parse_args()
    f = open(a.path, 'rb')      # fica aberto: pyelftools le o stream sob demanda
    e = ELFFile(f)
    print('== %s (%s %s)' % (a.path, e.elfclass, e.header.e_machine))
    print('-- NEEDED/SONAME --')
    show_needed(e)
    if a.undef:
        print('-- IMPORTS (SHN_UNDEF) --')
        symbols(e, True)
    if a.defined:
        print('-- EXPORTS --')
        symbols(e, False)
    if a.pattern:
        print('-- STRINGS matching %s --' % a.pattern)
        strings(e, a.pattern, a.limit)


if __name__ == '__main__':
    sys.exit(main())
