#!/usr/bin/env python3
"""audit_port.py - varredura do port ULTRAKILL (SD do TrimUI Smart Pro).

Checa o que da para checar SEM o device, isto e, os erros que se podem cometer
numa pasta montada no Windows e que depois viram "nao rodou":

  1. arquivos obrigatorios presentes (entry na raiz, gamelist, metadata,
     launcher .sh/.bin IDENTICOS, binarios, payload, packs)
  2. ELF aarch64 de verdade em cada binario (magic + e_machine)
  3. CRLF em .sh/.conf (bash do device + 'source' do conf quebram com \r)
  4. variaveis usadas sem default numa arvore com 'set -u' (aborte silencioso
     e o pior bug possivel: o port morre antes de escrever qualquer log)
  5. chaves do ultrakill.conf que NENHUM codigo le (knob morto = tuning que nao
     acontece e voce acha que ajustou)
  6. knobs que o binario le (strings) e o conf documenta, dos dois lados
  7. python compila (compile()) em todos os tools/
  7b. catalog.json x bundles em aa/Android, boot.config, volumes do jogo,
     frameRateLimit, pastas de persistencia, metadata
  8. espaco livre no cartao (o loader do ULTRAKILL reclama de storage!)
  8b. resto de stub antigo / .bak que pode confundir
  9. consistencia entry<->gamelist<->metadata (nome do arquivo, button=)

Uso:  python3 tools/audit_port.py [--root <dir do port>]
Saida: linhas FAIL/WARN/OK + resumo com contagem. Exit!=0 se houver FAIL.
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys

EM_AARCH64 = 183


def elf64_aarch64(path):
    try:
        with open(path, 'rb') as f:
            h = f.read(20)
    except OSError:
        return False
    if len(h) < 20 or h[:4] != b'\x7fELF':
        return False
    ei_class, ei_data = h[4], h[5]
    machine = struct.unpack('<H', h[18:20])[0] if ei_data == 1 else struct.unpack('>H', h[18:20])[0]
    return ei_class == 2 and machine == EM_AARCH64


# ---------------------------------------------------------------- checks
def check_required(root, port_parent, rep):
    need = [
        ('ultrakill_start.sh', 'file'), ('ultrakill_start.bin', 'file'),
        ('ultrakill.conf', 'file'), ('ultrakill.txt', 'file'),
        ('LEIA-ESTE-PRIMEIRO.txt', 'file'),
        ('unity_probe.aarch64', 'file'),
        ('gamedata/bin/ultrakill', 'file'),
        ('libs/arm64/libOpenSLES.so', 'file'),
        ('libs/arm64/libEGL.so', 'file'),
        ('libs/arm64/libandroid.so', 'file'),
        ('libs/arm64/liblog.so', 'file'),
        ('libs/arm64/libmediandk.so', 'file'),
        ('audio_bridge/libOpenSLES.so', 'file'),
        ('unity_payload/assets/aa/catalog.json', 'file'),
        ('unity_payload/assets/bin/Data/Managed/Metadata/global-metadata.dat', 'file'),
        ('unity_payload/lib/arm64-v8a/libunity.so', 'file'),
        ('unity_payload/lib/arm64-v8a/libil2cpp.so', 'file'),
        ('bender/unity_probe.c', 'file'),
        ('wrapper/CMakeLists.txt', 'file'),
    ]
    for rel, kind in need:
        p = os.path.join(root, rel.replace('/', os.sep))
        rep.add('OK' if os.path.isfile(p) else 'FAIL', 'existe: %s' % rel,
                '' if os.path.isfile(p) else 'FALTA %s' % p)
    bundles = os.path.join(root, 'unity_payload', 'assets', 'aa', 'Android')
    nb = len([f for f in os.listdir(bundles) if f.endswith('.bundle')]) if os.path.isdir(bundles) else 0
    rep.add('OK' if nb >= 3 else 'FAIL', 'bundles Android/GLES em aa/Android', '%d bundle(s)' % nb)
    # entry point na RAIZ de /roms/ports
    ents = [f for f in ('ULTRAKIL.sh', 'ultrakill.sh') if os.path.isfile(os.path.join(port_parent, f))]
    rep.add('OK' if ents else 'FAIL', 'entry point em /roms/ports', ','.join(ents) or 'nenhum')
    if len(ents) > 1:
        rep.add('WARN', 'mais de um entry (ES pode listar os dois)', ','.join(ents))
    gl = os.path.join(port_parent, 'gamelist.xml')
    if os.path.isfile(gl):
        txt = open(gl, encoding='utf-8', errors='replace').read()
        for e in ents:
            rep.add('OK' if ('./' + e) in txt else 'FAIL', 'gamelist aponta p/ %s' % e,
                    '' if ('./' + e) in txt else 'sem <path>./%s' % e)
        if not ents:
            rep.add('FAIL', 'gamelist tem entry inexistente', 'ultrakill.sh sumiu do disco')
        bad = re.findall(r'<path>\./([^<]+\.sh)</path>', txt)
        miss = [b for b in bad if not os.path.isfile(os.path.join(port_parent, b.replace('/', os.sep)))]
        rep.add('OK' if not miss else 'WARN', 'todos os <path> do gamelist existem', ','.join(miss) or '-')
        try:
            import xml.etree.ElementTree as ET
            ET.parse(gl)
            rep.add('OK', 'gamelist.xml bem formado', '')
        except Exception as e:
            rep.add('FAIL', 'gamelist.xml bem formado', str(e))
    md = os.path.join(root, 'ultrakill.txt')
    if os.path.isfile(md) and ents:
        t = open(md, encoding='utf-8', errors='replace').read()
        m = re.search(r'button="([^"]*)"', t)
        btn = m.group(1) if m else ''
        ok = btn in ents
        rep.add('OK' if ok else 'FAIL', 'ultrakill.txt button= bate com o entry', btn or '(vazio)')
    a, b = os.path.join(root, 'ultrakill_start.sh'), os.path.join(root, 'ultrakill_start.bin')
    if os.path.isfile(a) and os.path.isfile(b):
        same = open(a, 'rb').read() == open(b, 'rb').read()
        rep.add('OK' if same else 'FAIL', 'launcher .sh == .bin (o device executa um dos dois)',
                'identicos' if same else 'DIFERENTES - copie .sh sobre .bin')


def check_elfs(root, rep):
    for rel in ['unity_probe.aarch64', 'gamedata/bin/ultrakill', 'gamedata/bin/ultrakill-arm64',
               'libs/arm64/libOpenSLES.so', 'libs/arm64/libEGL.so', 'libs/arm64/libandroid.so',
               'libs/arm64/liblog.so', 'libs/arm64/libmediandk.so', 'tools/egltest',
               'unity_payload/lib/arm64-v8a/libunity.so', 'unity_payload/lib/arm64-v8a/libil2cpp.so']:
        p = os.path.join(root, rel.replace('/', os.sep))
        if not os.path.isfile(p):
            rep.add('WARN', 'ELF: %s' % rel, 'ausente')
            continue
        ok = elf64_aarch64(p)
        sz = os.path.getsize(p)
        rep.add('OK' if ok else 'FAIL', 'ELF aarch64: %s' % rel,
                '%d bytes%s' % (sz, '' if ok else ' - NAO e ELF aarch64 (executavel no PC?!)'))


def check_crlf(root, port_parent, rep):
    for p in [os.path.join(root, f) for f in ('ultrakill_start.sh', 'ultrakill.conf', 'ultrakill.txt')] + \
             [os.path.join(port_parent, f) for f in ('ULTRAKIL.sh', 'ultrakill.sh')]:
        if not os.path.isfile(p):
            continue
        d = open(p, 'rb').read()
        n = d.count(b'\r\n')
        rep.add('OK' if n == 0 else 'FAIL', 'CRLF em %s' % os.path.basename(p),
                '%d linhas CRLF' % n if n else 'LF ok')


def check_set_u(root, port_parent, rep):
    """em arvore com 'set -u', $VAR lida sem default e sem atribuicao previa =
    abort silencioso ANTES de qualquer log (o pior 'nao rodou' que existe).
    Conservador: so reclama de nome que NUNCA aparece com default (:- :+ :=) em
    nenhum lugar do arquivo e NUNCA e atribuido."""
    import subprocess as sp
    bash = None
    for cand in (r'C:\Program Files\Git\bin\bash.exe', r'C:\Program Files\Git\usr\bin\bash.exe'):
        if os.path.isfile(cand):
            bash = cand
            break
    for p in [os.path.join(root, 'ultrakill_start.sh'), os.path.join(port_parent, 'ULTRAKIL.sh')]:
        if not os.path.isfile(p):
            continue
        name = os.path.basename(p)
        src = open(p, encoding='utf-8', errors='replace').read()
        if 'set -u' not in src:
            rep.add('WARN', '%s: sem set -u' % name, 'nada a checar')
            continue
        assigned = set()
        # atribuicao pode vir depois de ';', '&&', '||', '|', '(' e num for/read;
        # sem isso TODO var de 'case' vira falso positivo.
        for chunk in re.split(r'[;|&\n)]|\bthen\b|\bdo\b|\{', src):
            for m in re.finditer(r'^\s*(?:export\s+|local\s+|readonly\s+|declare\s+)([A-Za-z_]\w*)\b', chunk, re.M):
                assigned.add(m.group(1))
            for m in re.finditer(r'^\s*([A-Za-z_]\w*)=', chunk, re.M):
                assigned.add(m.group(1))
        assigned |= set(re.findall(r'\bfor\s+([A-Za-z_]\w*)\b', src))
        assigned |= set(re.findall(r'\bread\s+(?:-[A-Za-z]\s+)*([A-Za-z_][\w\s]*)', src) and
                        [w for grp in re.findall(r'\bread\s+(?:-[A-Za-z]\s+)*([A-Za-z_][\w\s]*)', src) for w in grp.split()])
        assigned |= set(re.findall(r'\$\(\s*(?:unset\s+)?([A-Za-z_]\w*)=', src))
        guarded = set(re.findall(r'\$\{\s*([A-Za-z_]\w*)\s*:?[-+=?]', src))
        used = set(re.findall(r'\$\{?([A-Za-z_]\w*)', src))
        sysv = {'?','#','0','PWD','HOME','IFS','UID','EUID','RANDOM','SECONDS','LINENO','PPID','SHLVL','OPTARG','OPTIND','BASH_VERSION','PATH','PWD','OLDPWD','_'}
        risky = sorted(n for n in used if n not in assigned and n not in guarded and n not in sysv
                       and not n.startswith(('SDL','DISPLAY','XDG','CUPHEAD','DEADSPACE','TERM','USER','LANG','SHELL','TMPDIR','OD','TD')))
        rep.add('OK' if not risky else 'WARN', '%s: $VAR sem default com set -u' % name,
                ' '.join(risky[:12]) or '-')
        if bash:
            r = sp.run([bash, '-n', p.replace('\\', '/')], capture_output=True, text=True)
            rep.add('OK' if r.returncode == 0 else 'FAIL', '%s: bash -n (sintaxe)' % name,
                    (r.stderr or '')[:200] or 'ok')


def check_conf_keys(root, rep):
    conf = os.path.join(root, 'ultrakill.conf')
    if not os.path.isfile(conf):
        return
    keys = []
    for line in open(conf, encoding='utf-8', errors='replace'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = re.match(r'([A-Za-z_][A-Za-z0-9_]*)=', line)
        if m:
            keys.append(m.group(1))
    launcher = open(os.path.join(root, 'ultrakill_start.sh'), encoding='utf-8', errors='replace').read() \
        if os.path.isfile(os.path.join(root, 'ultrakill_start.sh')) else ''
    blob = b''
    for b in ('unity_probe.aarch64', os.path.join('gamedata', 'bin', 'ultrakill')):
        p = os.path.join(root, b.replace('/', os.sep))
        if os.path.isfile(p):
            blob += open(p, 'rb').read()
    orphans = []
    for k in keys:
        if k in launcher or k.encode() in blob or k in ('BENDER_MOUSE_CLICK_B', 'BENDER_MOUSE_TOGGLE_START_R1'):
            continue
        orphans.append(k)
    rep.add('OK' if not orphans else 'FAIL', 'todas as chaves ativas do conf sao lidas em algum lugar',
            ','.join(orphans) or '%d chaves ok' % len(keys))
    # knobs que os binarios leem mas o conf nao documenta (informativo)
    read = sorted(set(re.findall(rb'BENDER_[A-Z0-9_]+|ULTRAKILL_[A-Z0-9_]+', blob)))
    documented = set()
    for line in open(conf, encoding='utf-8', errors='replace'):
        documented |= set(re.findall(r'[BU][A-Z0-9_]*LTRAKILL[A-Z0-9_]*|BENDER_[A-Z0-9_]+|ULTRAKILL_[A-Z0-9_]+', line))
    doc_all = set(re.findall(r'[A-Z][A-Z0-9_]{4,}', open(conf, encoding='utf-8', errors='replace').read()))
    hidden = [k.decode() for k in read if k.decode() not in doc_all and len(k) > 8]
    rep.add('WARN' if len(hidden) > 40 else 'OK', 'knobs dos binarios ausentes do conf (documentacao)',
            '%d (ex: %s)' % (len(hidden), ' '.join(hidden[:6])))


def check_python(root, rep):
    tdir = os.path.join(root, 'tools')
    if not os.path.isdir(tdir):
        return
    for f in sorted(os.listdir(tdir)):
        if not f.endswith('.py'):
            continue
        p = os.path.join(tdir, f)
        try:
            compile(open(p, 'rb').read().decode('utf-8'), p, 'exec')
            rep.add('OK', 'python compila: tools/%s' % f, '')
        except SyntaxError as e:
            rep.add('FAIL', 'python compila: tools/%s' % f, '%s (linha %s)' % (e.msg, e.lineno))
        except Exception as e:
            rep.add('FAIL', 'python compila: tools/%s' % f, str(e)[:160])


def check_house(root, port_parent, rep):
    free = shutil.disk_usage(port_parent).free / 2**30
    rep.add('OK' if free > 2 else 'FAIL', 'espaco livre no cartao (o Unity checa isso)', '%.1f GB' % free)
    tot = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(root) for f in fs) / 2**20
    rep.add('OK', 'tamanho do port', '%.0f MB' % tot)
    old = []
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(('.bak-revl', '.stub-noop.bak', '.bak-ark', '.old', '.orig')):
                old.append(os.path.relpath(os.path.join(r, f), root))
    rep.add('OK' if old else 'WARN', 'backups presentes (nao sao lidos por ninguem)',
            '%d arquivo(s)' % len(old) if old else 'nenhum backup - nada para reverter')
    # stub no-op encostado no lugar do bridge?
    s = os.path.join(root, 'libs', 'arm64', 'libOpenSLES.so')
    if os.path.isfile(s):
        d = open(s, 'rb').read()
        good = b'SL_IID_ENGINE' in d and b'SDL_QueueAudio' in d
        rep.add('OK' if good else 'FAIL', 'libs/arm64/libOpenSLES.so e o bridge real (nao o stub)',
                '%d bytes' % len(d))
    e = os.path.join(root, 'libs', 'arm64', 'libEGL.so.1')
    if os.path.isfile(e) and os.path.isfile(s):
        same = open(e, 'rb').read() == open(os.path.join(root, 'libs', 'arm64', 'libEGL.so'), 'rb').read()
        rep.add('WARN' if same else 'OK', 'libEGL.so.1 duplicado do shim',
                'launcher remove isso no boot' if same else 'ok')
    for junk in ('ci-artifact', '__pycache__', '.scratchpad'):
        p = os.path.join(root, junk)
        if os.path.isdir(p):
            rep.add('WARN', 'temporario no cartao: %s/' % junk, 'pode apagar')
    packs = ['UnityDataAssetPack', 'UnityStreamingAssetsPack']
    for pk in packs:
        dd = os.path.join(root, 'unity_payload', 'asset_packs', pk, 'assets')
        has = os.path.isdir(dd) and len(os.listdir(dd)) > 0
        rep.add('OK' if has else 'FAIL', 'asset pack montado: %s' % pk,
                '%d itens' % len(os.listdir(dd)) if os.path.isdir(dd) else 'FALTA (gera dialog de instalacao)')


class Rep:
    def __init__(self):
        self.n = {'OK': 0, 'WARN': 0, 'FAIL': 0}
        self.fails = []

    def add(self, lvl, what, detail=''):
        self.n[lvl] += 1
        if lvl != 'OK':
            self.fails.append((lvl, what, detail))
        mark = {'OK': ' ok ', 'WARN': 'WARN', 'FAIL': 'FAIL'}[lvl]
        print('[%s] %-58s %s' % (mark, what, detail))


def check_runtime_files(root, rep):
    """coisas que o jogo/loader leem no boot e que costumam estar erradas."""
    # 1) catalog.json do Addressables -> cada bundle precisa existir no disco
    cat = os.path.join(root, 'unity_payload', 'assets', 'aa', 'catalog.json')
    if os.path.isfile(cat):
        try:
            j = json.load(open(cat, encoding='utf-8'))
        except Exception as e:
            rep.add('FAIL', 'catalog.json legivel', str(e)[:120]); j = None
        if j:
            keys = set()
            for it in j.get('m_AllItems', {}).values() if isinstance(j.get('m_AllItems'), dict) else []:
                keys.add(it.get('m_SimpleKey'))
            raw = open(cat, encoding='utf-8', errors='replace').read()
            refs = sorted(set(re.findall(r'([A-Za-z0-9_]+_[a-f0-9]{32}\.bundle)', raw)))
            ad = os.path.join(root, 'unity_payload', 'assets', 'aa', 'Android')
            have = set(os.listdir(ad)) if os.path.isdir(ad) else set()
            miss = [r for r in refs if r not in have]
            rep.add('OK' if refs and not miss else ('FAIL' if miss else 'WARN'),
                    'bundles citados no catalog existem em aa/Android',
                    '%d citados, %d no disco%s' % (len(refs), len(have),
                                                   (' - FALTAM: ' + ', '.join(miss[:3])) if miss else ''))
    # 2) boot.config no lugar que o loader escreve/le
    bc = os.path.join(root, 'unity_payload', 'assets', 'bin', 'Data', 'boot.config')
    rep.add('OK' if os.path.isfile(bc) else 'WARN', 'boot.config presente (launcher gera no boot)',
            'ja existe' if os.path.isfile(bc) else 'sera criado em /bin/Data')
    # 3) volume do proprio jogo (mudo nao e bug do bridge de audio)
    pj = os.path.join(root, 'data', 'files', 'Preferences', 'Prefs.json')
    if os.path.isfile(pj):
        try:
            d = json.load(open(pj, encoding='utf-8'))
            bad = [k for k in ('allVolume', 'musicVolume') if float(d.get(k, 1) or 0) < 0.05]
            rep.add('OK' if not bad else 'FAIL', 'volumes do jogo nao estao zerados',
                    ','.join(bad) or 'allVolume=%s musicVolume=%s' % (d.get('allVolume'), d.get('musicVolume')))
        except Exception as e:
            rep.add('WARN', 'Prefs.json legivel', str(e)[:120])
    lp = os.path.join(root, 'data', 'files', 'Preferences', 'LocalPrefs.json')
    if os.path.isfile(lp):
        try:
            d = json.load(open(lp, encoding='utf-8'))
            fl = d.get('frameRateLimit')
            rep.add('OK' if (fl is None or float(fl) >= 30) else 'WARN',
                    'frameRateLimit do jogo nao trava em <30', str(fl))
        except Exception:
            pass
    # 4) pastas de escrita
    for d in ('home', 'data', 'data/files'):
        p = os.path.join(root, d.replace('/', os.sep))
        rep.add('OK' if os.path.isdir(p) else 'WARN', 'dir de persistencia: %s/' % d,
                '' if os.path.isdir(p) else 'criado no boot')
    # 5) metadata minima
    md = os.path.join(root, 'ultrakill.txt')
    if os.path.isfile(md):
        t = open(md, encoding='utf-8', errors='replace').read()
        for k in ('name', 'button', 'platforms', 'desc'):
            m = re.search(r'%s="([^"]*)"' % k, t)
            rep.add('OK' if (m and m.group(1).strip()) else 'WARN', 'ultrakill.txt %s="..."' % k,
                    m.group(1)[:40] if m else '(vazio)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    port_parent = os.path.dirname(root)
    print('port  : %s' % root)
    print('raiz  : %s\n' % port_parent)
    rep = Rep()
    for fn in (check_required, check_elfs, check_crlf, check_conf_keys, check_python,
               check_runtime_files, check_house):
        try:
            if fn is check_required or fn is check_crlf or fn is check_house:
                fn(root, port_parent, rep)
            elif fn is check_conf_keys or fn is check_runtime_files:
                fn(root, rep)
            else:
                fn(root, rep)
        except Exception as e:
            rep.add('WARN', 'o check %s quebrou' % fn.__name__, repr(e)[:160])
    try:
        check_set_u(root, port_parent, rep)
    except Exception as e:
        rep.add('WARN', 'o check set -u quebrou', repr(e)[:160])
    print('\n==== resumo: %d ok, %d WARN, %d FAIL ====' % (rep.n['OK'], rep.n['WARN'], rep.n['FAIL']))
    for lvl, what, detail in rep.fails:
        print('  %s  %s  %s' % (lvl, what, detail))
    return 1 if rep.n['FAIL'] else 0


if __name__ == '__main__':
    sys.exit(main())
