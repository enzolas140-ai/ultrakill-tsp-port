#!/usr/bin/env python3
"""chk_exec.py - mostra para onde vai o stdout do launcher e confirma que o
bloco do mapa de display imprime no MESMO arquivo que os outros ecos.

Motivo: o 'MAPA DE DISPLAY' nao aparecia no ultrakill.log do device, embora a
funcao exista, seja chamada e funcione isolada. Se o launcher so grava no log via
redirecionamentos por-comando (sem 'exec > LOG'), um bloco sem redirecionamento
escapa para o console da ES em vez do arquivo - e e isso que este chk mostra.
"""
import re
import subprocess
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'ultrakill_start.sh'
s = open(path, encoding='utf-8', errors='replace').read().splitlines()

print('--- definicoes de LOG e redirecionamentos globais ---')
for n, l in enumerate(s, 1):
    if re.search(r'LOG=', l) or re.match(r'\s*exec\s+[0-9]*>', l) or re.match(r'\s*exec\s*>>', l):
        print('%5d %s' % (n, l.strip()[:150]))

print('--- contexto da chamada uk_display_map ---')
idx = [n for n, l in enumerate(s, 1) if l.strip() == 'uk_display_map']
for n in idx:
    for k in range(max(0, n - 4), min(len(s), n + 3)):
        print('%5d %s' % (k + 1, s[k][:150]))
    print('   ...')

print('--- teste: executa a funcao extraida (stdout do script, sem redirect) ---')
fn = '\n'.join(s)
m = re.search(r'(  uk_display_map\(\)\s*\{.*?\n  \})', fn, re.S)
if not m:
    print('  FALHOU: nao achei a definicao')
    sys.exit(1)
body = m.group(1)
t = 'set -u\nLOG=/tmp/nada\n' + body + '\necho "   (mapa: inicio)"\nuk_display_map\necho "   (mapa: fim rc=$?)"\n'
import tempfile
import os
tmp = os.path.join(tempfile.gettempdir(), 'uk_map_only.sh')
open(tmp, 'w', newline='\n').write(t)
r = subprocess.run(['bash', tmp], capture_output=True, text=True)
out = (r.stdout or '') + (r.stderr or '')
linhas = [l for l in out.splitlines() if l.strip()]
print('  linhas no stdout: %d' % len(linhas))
for l in linhas[:6]:
    print('   |', l[:120])
print('  rc=%d' % r.returncode)
