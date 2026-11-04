#!/usr/bin/env python3
"""install_base_apk.py - poe o APK certo no port como base.apk e PROVA que ele
bate com o payload extraido no cartao.

Por que isto existe (causa raiz de 'o port nao abre', descoberta em 04/11):
o loader (bender/unity_probe.c) informa ao libunity que o APK do jogo e
    <app>/unity_payload/BENDER_RUNTIME_UNIFIED.apk   se existir
    <app>/base.apk                                    senao
e o libunity extrai recursos dai (a string dele no binario e
'il2cpp failed to extract resources (%s => %s)' seguida de
'Not enough storage space to install required resources.'). Sem esse arquivo, a
extracao falha, o Unity abre um AlertDialog e dorme em pthread_cond_wait em
libunity.so+0x2e38ac - sem tela Android para responder. O dialogo NAO e sobre
espaco no cartao (havia 79,8 GB livres).

Se o APK for de OUTRA versao do jogo, as .so extraidas nao casam com o
global-metadata.dat e o boot morre com 'Referenced script is missing'. Entao
esta ferramenta compara o conteudo (md5) de cada entrada critica e recusa
inconsistencia.

Uso:
    python3 tools/install_base_apk.py --apk caminho/do/ULTRADROID.apk
    python3 tools/install_base_apk.py --apk X --only-verify
    python3 tools/install_base_apk.py            # so verifica o base.apk atual

Saidas:
  OK  base.apk instalado e identico ao payload
  FAIL APK ausencia / entrada faltando / conteudo divergente
"""
import argparse
import hashlib
import os
import shutil
import sys
import zipfile

# entrada do APK -> caminho no port. Toda par abaixo tem que ser IDENTICO.
CRITICAL = [
    ('lib/arm64-v8a/libunity.so', 'unity_payload/lib/arm64-v8a/libunity.so'),
    ('lib/arm64-v8a/libil2cpp.so', 'unity_payload/lib/arm64-v8a/libil2cpp.so'),
    ('lib/arm64-v8a/libmain.so', 'unity_payload/lib/arm64-v8a/libmain.so'),
    ('assets/bin/Data/Managed/Metadata/global-metadata.dat',
     'unity_payload/assets/bin/Data/Managed/Metadata/global-metadata.dat'),
    ('assets/bin/Data/data.unity3d', 'unity_payload/assets/bin/Data/data.unity3d'),
    ('assets/aa/catalog.json', 'unity_payload/assets/aa/catalog.json'),
    ('assets/aa/settings.json', 'unity_payload/assets/aa/settings.json'),
]
# boot.config e reescrito pelo launcher (renderizador/qualidade) - nao comparar
IGNORE_ASSETS = ('assets/bin/Data/boot.config',)


def md5_bytes(b):
    return hashlib.md5(b).hexdigest()


def md5_file(p, chunk=4 << 20):
    h = hashlib.md5()
    with open(p, 'rb') as f:
        for blk in iter(lambda: f.read(chunk), b''):
            h.update(blk)
    return h.hexdigest()


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument('--apk', help='APK de origem (o do repack, NAO outro)')
    ap.add_argument('--dest', default=os.path.join(root, 'base.apk'))
    ap.add_argument('--root', default=root)
    ap.add_argument('--only-verify', action='store_true')
    ap.add_argument('--pull-missing', action='store_true', default=True,
                    help='extrai do APK o lib/ que faltar no cartao')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    dst = os.path.abspath(a.dest)

    if not a.only_verify:
        if not a.apk:
            print('FAIL: preciso de --apk <arquivo> (ou use --only-verify)')
            return 2
        src = os.path.abspath(a.apk)
        if not os.path.isfile(src):
            print('FAIL: APK nao existe: %s' % src)
            return 2
        try:
            zipfile.ZipFile(src).testzip()
        except Exception as e:
            print('FAIL: nao e um zip/APK valido: %r' % e)
            return 2
        if os.path.abspath(src) == dst:
            print('   (origem == destino, nada a copiar)')
        else:
            print('   copiando %.0f MB -> %s ...' % (os.path.getsize(src) / 2**20, dst))
            shutil.copyfile(src, dst)
    if not os.path.isfile(dst):
        print('FAIL: %s nao existe. Rode:  python3 tools/install_base_apk.py --apk <ULTRADROID.apk>' % dst)
        return 2

    z = zipfile.ZipFile(dst)
    ent = {i.filename: i for i in z.infolist()}
    print('base.apk : %.0f MB, %d entradas' % (os.path.getsize(dst) / 2**20, len(ent)))
    bad = 0
    for key, rel in CRITICAL:
        card = os.path.join(root, rel.replace('/', os.sep))
        if key not in ent:
            print('  FAIL  apk nao tem %s' % key)
            bad += 1
            continue
        if not os.path.isfile(card):
            print('  FAIL  cartao nao tem %s' % rel)
            bad += 1
            continue
        ha = md5_bytes(z.read(key))
        hb = md5_file(card)
        if ha == hb:
            print('  ok    %-56s identico' % key)
        else:
            print('  FAIL  %-56s CONTEUDO DIFERE (apk %s vs cartao %s)' % (key, ha[:8], hb[:8]))
            print('        -> o payload do cartao e de OUTRA versao. Reextraia do proprio base.apk,')
            print('           senao o il2cpp vai morrer com "Referenced script is missing".')
            bad += 1

    # faltas conhecidas de assets/ no payload (boot.config a gente reescreve de proposito)
    miss = []
    for key, i in ent.items():
        if not key.startswith('assets/') or key in IGNORE_ASSETS or key.endswith('/'):
            continue
        p = os.path.join(root, 'unity_payload', 'assets', key[len('assets/'):].replace('/', os.sep))
        if not os.path.exists(p):
            miss.append((key, i.file_size))
    if miss:
        print('  WARN  %d asset(s) do apk ausentes no payload extraido:' % len(miss))
        for k, s in sorted(miss)[:8]:
            print('          %-64s %d' % (k, s))
    else:
        print('  ok    todos os assets/ do apk presentes no payload (exceto boot.config, que o launcher reescreve)')

    if a.pull_missing:
        ld = os.path.join(root, 'unity_payload', 'lib', 'arm64-v8a')
        have = set(os.listdir(ld)) if os.path.isdir(ld) else set()
        for key, i in ent.items():
            if not key.startswith('lib/arm64-v8a/'):
                continue
            name = key.split('/')[-1]
            if name not in have:
                os.makedirs(ld, exist_ok=True)
                open(os.path.join(ld, name), 'wb').write(z.read(key))
                print('  + extraido %s do apk (faltava)' % name)

    print('=== %s ===' % ('OK: base.apk consistente com o payload' if not bad else
                          'FAIL: %d divergencia(s) - NAO va para o cartao assim' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
