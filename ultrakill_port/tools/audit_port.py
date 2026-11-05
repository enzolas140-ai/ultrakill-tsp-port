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
  10. base.apk presente E identico ao payload extraido (causa raiz do 'nao abre')

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
        # comentarios NAO sao codigo: sem isto, uma linha tipo
        # '#   echo 0 | $SUDO tee ...' (citacao de outro port) vira 'VAR sem
        # default' falso-positiva e o auditor perde credibilidade.
        src_code = '\n'.join(l for l in src.splitlines() if not l.lstrip().startswith('#'))
        assigned = set()
        src = src_code
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
    launcher_txt = ''
    entry_txt = ''
    if os.path.isfile(os.path.join(root, 'ultrakill_start.sh')):
        launcher_txt = open(os.path.join(root, 'ultrakill_start.sh'), encoding='utf-8', errors='replace').read()
    for cand in ('ULTRAKIL.sh', 'ultrakill.sh'):
        p = os.path.join(os.path.dirname(root), cand)
        if os.path.isfile(p):
            entry_txt += open(p, encoding='utf-8', errors='replace').read()
    launcher = launcher_txt + entry_txt
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
            # o jogo REESCREVE as prefs ao sair (pegamos frameRateLimit voltando
            # de 60 para 7), entao o valor em disco nao e a palavra final: o
            # launcher impoe ULTRAKILL_TARGET_FPS a cada boot.
            enforced = False
            lau = os.path.join(root, 'ultrakill_start.sh')
            if os.path.isfile(lau):
                s = open(lau, encoding='utf-8', errors='replace').read()
                enforced = 'frameRateLimit' in s and 'ULTRAKILL_ENFORCE_FPS_PREF' in s
            ok = (fl is None) or float(fl) >= 30 or enforced
            note = str(fl)
            if enforced and fl is not None and float(fl) < 30:
                note += ' (valor salvo pelo jogo; o launcher impoe o alvo a cada boot)'
            rep.add('OK' if ok else 'WARN', 'frameRateLimit do jogo nao trava em <30', note)
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


def check_base_apk(root, rep):
    """A CAUSA RAIZ do 'nao abre'. O probe diz ao libunity que o APK do jogo e
    <app>/unity_payload/BENDER_RUNTIME_UNIFIED.apk ou <app>/base.apk, e o Unity
    EXTRAI recursos dai ('il2cpp failed to extract resources (%s => %s)'
    seguido de 'Not enough storage space to install required resources.'). Sem o
    arquivo, extracao falha -> AlertDialog -> cond wait eterno. Entao isto aqui
    e FAIL, nao aviso."""
    unified = os.path.join(root, 'unity_payload', 'BENDER_RUNTIME_UNIFIED.apk')
    base = os.path.join(root, 'base.apk')
    which = unified if os.path.isfile(unified) else base
    if not os.path.isfile(which):
        rep.add('FAIL', 'APK que o libunity vai extrair recursos existe',
                'FALTA %s - rode: python3 tools/install_base_apk.py --apk <ULTRADROID.apk>' % os.path.basename(which))
        return
    try:
        import zipfile
        z = zipfile.ZipFile(which)
        ent = {i.filename: i.file_size for i in z.infolist()}
    except Exception as e:
        rep.add('FAIL', 'APK e um zip legivel', repr(e)[:120])
        return
    rep.add('OK', 'APK de extracao: %s' % os.path.basename(which),
            '%.0f MB, %d entradas' % (os.path.getsize(which) / 2**20, len(ent)))
    # as libs que o payload usa tem que vir do MESMO apk, senao 'Referenced script is missing'
    import hashlib
    pairs = [('lib/arm64-v8a/libunity.so', 'unity_payload/lib/arm64-v8a/libunity.so'),
             ('lib/arm64-v8a/libil2cpp.so', 'unity_payload/lib/arm64-v8a/libil2cpp.so'),
             ('assets/bin/Data/Managed/Metadata/global-metadata.dat',
              'unity_payload/assets/bin/Data/Managed/Metadata/global-metadata.dat')]
    dif = []
    for key, rel in pairs:
        card = os.path.join(root, rel.replace('/', os.sep))
        if key not in ent or not os.path.isfile(card):
            dif.append(rel + ' (faltando de um dos lados)')
            continue
        if ent[key] != os.path.getsize(card):
            dif.append('%s (tamanho %d vs %d)' % (rel, ent[key], os.path.getsize(card)))
            continue
        ha = hashlib.md5(z.read(key)).hexdigest()
        hb = hashlib.md5(open(card, 'rb').read()).hexdigest()
        if ha != hb:
            dif.append(rel + ' (CONTEUDO difere)')
    rep.add('OK' if not dif else 'FAIL', 'payload extraido e do MESMO apk (libunity/libil2cpp/metadata)',
            'identico' if not dif else ' -> '.join(dif))
    # assets/ cobertos
    miss = [k for k in ent if k.startswith('assets/') and not k.endswith('/')
            and k != 'assets/bin/Data/boot.config'
            and not os.path.exists(os.path.join(root, 'unity_payload', 'assets', k[len('assets/'):].replace('/', os.sep)))]
    rep.add('OK' if not miss else 'WARN', 'todo assets/ do apk esta no payload',
            '%d faltando: %s' % (len(miss), ', '.join(sorted(miss)[:3])) if miss else 'completo')


def check_es_handoff(root, port_parent, rep):
    """Classe de erro 'travou na lista de jogos': o port mexe no display/console e
    nao devolve, ou bloqueia antes de escrever log. Checado por invariante de
    codigo, porque este bug NAO aparece em log nenhum (a ES e a vitima)."""
    lau = os.path.join(root, 'ultrakill_start.sh')
    ent = os.path.join(port_parent, 'ULTRAKIL.sh')
    if not os.path.isfile(lau):
        rep.add('WARN', 'launcher para checar handoff de ES', 'ausente'); return
    src = open(lau, encoding='utf-8', errors='replace').read()
    # unbind pode ser 'echo 0 > arquivo' (direto) ou 'echo 0 | tee arquivo'
    # (jeito do NFSU2, que funciona sem sudo no spruceOS). Os dois so sao
    # aceitos se houver um caminho de volta (bind 1) registrado.
    unbind = re.search(r'echo\s+["\']?0["\']?\s*(>|\|\s*\$?[A-Za-z_]*SUDO?\w*\s+tee)\s*[^\n]*vtconsole', src)
    rebind = re.search(r'echo\s+["\']?1["\']?\s*(>|\|\s*\$?[A-Za-z_]*SUDO?\w*\s+tee)\s*[^\n]*vtconsole', src)
    helper = 'uk_vt_set 1' in src
    guarded = 'ULTRAKILL_DISPLAY_TAKEOVER' in src
    rep.add('OK' if (unbind is None or (guarded and (rebind or helper))) else 'FAIL',
            'unbind de vtcon tem caminho de volta',
            'opt-in + re-bind (%s)' % ('uk_vt_set 1' if helper else 'echo 1') if (unbind and guarded and (rebind or helper))
            else ('sem unbind' if not unbind else 'FALTA: religar vtcon na saida congela a ES'))
    # o cleanup tem que devolver o console, nao so o devfreq
    m = re.search(r'uk_cleanup\(\)\s*\{(.{0,4000}?)\n  \}', src, re.S)
    body = m.group(1) if m else ''
    ok_clean = ('vtconsole' in body and ('echo 1' in body or 'echo "1"' in body)) or 'uk_vt_set 1' in body
    rep.add('OK' if ok_clean else 'FAIL',
            'uk_cleanup restaura o vtcon', 'ok' if ok_clean else 'FALTA: restaurar bind=1 na saida')
    rep.add('OK' if 'trap uk_cleanup' in src else 'FAIL', 'trap de limpeza registrado', '')
    if os.path.isfile(ent):
        e = open(ent, encoding='utf-8', errors='replace').read()
        rep.add('OK' if 'entry.log' in e else 'WARN',
                'entry tem log proprio (prova antes do launcher)', 'entry.log' if 'entry.log' in e else 'sem log no entry')
        rep.add('OK' if 'vtconsole' in e else 'WARN',
                'entry faz self-heal do console ao acionar o port',
                'ok' if 'vtconsole' in e else 'sem self-heal')
        rep.add('OK' if re.search(r'timeout[^\n]*"?\$BINSH|timeout -k', e) else 'WARN',
                'entry capa o launcher com timeout duro', 'ok' if re.search(r'timeout -k', e) else 'sem teto')
        # /dev/tty0 so com opt-in
        i = e.find('/dev/tty0')
        ok = (i < 0) or ('ULTRAKILL_TTY_FEEDBACK' in e) or ('UK_TTY_OK' in e)
        rep.add('OK' if ok else 'WARN', 'escrita em /dev/tty0 e opt-in', 'ok' if ok else 'tty0 sem guarda')
        for name in ('ULTRAKIL.sh',):
            d = open(os.path.join(port_parent, name), 'rb').read()
            rep.add('OK' if b'\r\n' not in d else 'FAIL', 'CRLF em %s' % name, 'LF ok' if b'\r\n' not in d else 'tem CRLF')
        if os.path.isfile(os.path.join(root, 'SAFE')):
            rep.add('WARN', 'MODO SEGURO ativo (ultrakill/SAFE existe)', 'sem takeover, teto 90s')
    # lock obsoleto de display pode travar um port que usa flock -n=nao, mas o
    # processo antigo ainda de pe deixa o lock em /tmp: avisamos.
    if re.search(r'flock -n 8', src):
        rep.add('OK', 'lock de display e nao-bloqueante (flock -n)', 'nao trava esperando outro port')


def check_probe_markers(root, rep):
    """Os knobs novos tem que estar NO BINARIO instalado. Ja aconteceu de o cartao
    rodar um probe antigo (instalacao parcial), entao isso e checado como
    invariante: se o launcher exporta uma env e o probe nao a le, o tunino e
    decorativo."""
    p = os.path.join(root, 'unity_probe.aarch64')
    if not os.path.isfile(p):
        rep.add('FAIL', 'probe instalado', 'unity_probe.aarch64 ausente'); return
    d = open(p, 'rb').read()
    need = [b'BENDER_UK_FB', b'BENDER_UK_BLIT_SDL', b'BENDER_UK_WINDOW_W', b'BENDER_UK_PRESENT',
            b'BENDER_UK_FLUSH_PRESENT', b'BENDER_UK_SWAP_BRIDGE', b'UK_FLUSH_PRESENT]',
            b'fonte encontrada',
            b'BENDER_DIALOG_WAIT_RELEASE_SEC', b'BENDER_DIALOG_AUTODISMIS',
            b'BENDER_PLAYCORE_MISSING', b'BENDER_MOUSE_TOGGLE_START_R1', b'/dev/fb0']
    miss = [n.decode() for n in need if n not in d]
    # O rotulo dessa linha de log ja mudou de nome duas vezes (fonte encontrada ->
    # fonte escolhida -> escolhido fbo=). Aceita QUALQUER variante para o checker
    # nao reprovar um binario mais novo por ter melhorado.
    aliases = [b'fonte encontrada', b'fonte escolhida', b'escolhido fbo', b'varredura:']
    if 'fonte encontrada' in miss and any(a in d for a in aliases):
        miss.remove('fonte encontrada')
    rep.add('OK' if not miss else 'FAIL', 'probe instalado contem os knobs do conf',
            'todos presentes (%d bytes)' % len(d) if not miss else 'FALTAM: ' + ', '.join(miss))
    # O shim EGL e binario separado do probe: ja aconteceu de o CI compilar o shim
    # novo e o cartao continuar com o velho porque a lista de instalacao nao tinha
    # libEGL.so. Entao o conteudo do shim e checado como invariante.
    sh = os.path.join(root, 'libs', 'arm64', 'libEGL.so')
    if os.path.isfile(sh):
        sd = open(sh, 'rb').read()
        need_sh = [b'ULTRAKILL_SHIM_FB', b'shim/fb:', b'shim/libEGL: swap']
        ms = [n.decode() for n in need_sh if n not in sd]
        rep.add('OK' if not ms else 'FAIL', 'shim libEGL.so instalado tem o present por fb',
                'present no shim (%d bytes)' % len(sd) if not ms
                else 'FALTAM: %s - o cartao esta com shim ANTIGO (adicione libs/arm64/libEGL.so ao INSTALL do ci_roundtrip e rode --install-only)' % ', '.join(ms))
    lau = os.path.join(root, 'ultrakill_start.sh')
    if os.path.isfile(lau):
        s = open(lau, encoding='utf-8', errors='replace').read()
        gone = [n.decode() for n in need if n.decode().startswith(('BENDER_', 'ULTRAKILL_'))
                and n.decode() not in s and ('export ' + n.decode()) not in s]
        rep.add('OK' if not gone else 'WARN', 'launcher exporta os knobs que o probe le',
                'ok' if not gone else 'sem export: ' + ', '.join(gone))


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
               check_runtime_files, check_base_apk, check_es_handoff, check_probe_markers,
               check_house):
        try:
            if fn is check_required or fn is check_crlf or fn is check_house or fn is check_es_handoff:
                fn(root, port_parent, rep)
            elif fn is check_conf_keys or fn is check_runtime_files or fn is check_base_apk \
                    or fn is check_probe_markers:
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
