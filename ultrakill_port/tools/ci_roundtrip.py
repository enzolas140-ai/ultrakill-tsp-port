#!/usr/bin/env python3
"""ci_roundtrip.py - fecha o ciclo fonte -> Actions -> cartao num comando so.

Faz, nesta ordem:
  1. sincroniza os fontes do port (cartao) para o repo do GitHub Actions
     (bender/, wrapper/, ultrakill_start.sh/.bin, ultrakill.conf, tools)
  2. git add + commit (mensagem que voce passa com -m) + push
  3. espera o run daquele commit acabar
  4. baixa o artifact e instala os binarios NO PORT DO CARTAO
     (unity_probe.aarch64, gamedata/bin/ultrakill, libs/arm64/*.so,
      audio_bridge/libOpenSLES.so, tools/egltest), com backup .bak-revl
  5. apaga a pasta temporaria do artifact (nao enche o SD)

Uso (na raiz do port, com o repo do git existente):
  python3 tools/ci_roundtrip.py --repo "C:/Users/voce/.../github-upload" -m "probe: mexi no X"
  python3 tools/ci_roundtrip.py --repo <d> --no-push      # so sync + build do que ja esta commitado
  python3 tools/ci_roundtrip.py --repo <d> --install-only # so baixa/instala o ultimo run

Pre-requisitos: git no PATH e o remote origin do repo ja com o token (como esta
hoje). O token NUNCA e impresso por este script.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, os.pardir))     # /roms/ports/ultrakill
API = 'https://api.github.com'

# o que vai do cartao para o repo (fonte)
SYNC = [('bender', 'ultrakill_port/bender'),
        ('wrapper', 'ultrakill_port/wrapper'),
        ('tools', 'ultrakill_port/tools'),
        ('ultrakill_start.sh', 'ultrakill_port/ultrakill_start.sh'),
        ('ultrakill_start.bin', 'ultrakill_port/ultrakill_start.bin'),
        ('ultrakill.conf', 'ultrakill_port/ultrakill.conf'),
        ('ultrakill.txt', 'ultrakill_port/ultrakill.txt'),
        ('LEIA-ESTE-PRIMEIRO.txt', 'ultrakill_port/LEIA-ESTE-PRIMEIRO.txt'),
        ('.github/workflows/build-wrapper.yml', 'ultrakill_port/.github/workflows/build-wrapper.yml')]
SKIP_DIR = ('build-arm64', 'build-host', '__pycache__', '.git', '.backups')

# o que volta do artifact para o cartao (binario)
INSTALL = [('libs/arm64/libOpenSLES.so', 'libs/arm64/libOpenSLES.so'),
           ('libs/arm64/libandroid.so', 'libs/arm64/libandroid.so'),
           ('libs/arm64/liblog.so', 'libs/arm64/liblog.so'),
           ('libs/arm64/libmediandk.so', 'libs/arm64/libmediandk.so'),
           ('libs/arm64/libz.so.1', 'libs/arm64/libz.so.1'),
           ('gamedata/bin/ultrakill', 'gamedata/bin/ultrakill'),
           ('gamedata/bin/ultrakill', 'gamedata/bin/ultrakill-arm64'),
           ('bender/out/unity_probe.aarch64', 'unity_probe.aarch64'),
           ('audio_bridge/libOpenSLES.so', 'audio_bridge/libOpenSLES.so'),
           ('wrapper/build-arm64/egltest', 'tools/egltest'),
           ('wrapper/build-arm64/input_probe', 'tools/input_probe'),
           ('wrapper/build-arm64/symbol_probe', 'tools/symbol_probe')]


def sh(cmd, cwd=None, show=False):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if show and r.stdout.strip():
        print('  $ ' + ' '.join(cmd) + '\n' + r.stdout.strip()[:600])
    return r


def creds(repo_dir):
    url = sh(['git', 'config', '--get', 'remote.origin.url'], cwd=repo_dir).stdout.strip()
    m = re.match(r'https://(?:([^:@/]+)(?::([^@/]+))?@)?([^/]+)/(.+?)(?:\.git)?$', url)
    if not m:
        raise SystemExit('remote do repo nao entendido: ' + repo_dir)
    return (m.group(2) or m.group(1)), m.group(4)


def api(tok, path):
    req = urllib.request.Request(API + path, headers={'Authorization': 'token ' + tok,
                                                      'User-Agent': 'ultrakill-ci'})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read() or b'{}')


def download(tok, url, out):
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None
    opener = urllib.request.build_opener(NoRedirect)
    hdr = {'User-Agent': 'ultrakill-ci', 'Authorization': 'token ' + tok}
    try:
        with opener.open(urllib.request.Request(url, headers=hdr), timeout=600) as r:
            data = r.read()
    except urllib.error.HTTPError as e:
        loc = e.headers.get('location') if e.code in (301, 302, 303, 307, 308) else None
        if not loc:
            raise
        with urllib.request.urlopen(urllib.request.Request(
                loc, headers={'User-Agent': 'ultrakill-ci'}), timeout=600) as r2:
            data = r2.read()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'wb').write(data)
    return len(data)


def sync_to_repo(repo):
    n = 0
    for src, dst in SYNC:
        s = os.path.join(PORT, src.replace('/', os.sep))
        d = os.path.join(repo, dst.replace('/', os.sep))
        if not os.path.exists(s):
            print('  (sem %s)' % src); continue
        if os.path.isdir(s):
            # merge (nao apaga o destino inteiro): assim arquivos novos do cartao
            # entram e o que so existe no repo (ex.: README do CI) nao e perdido.
            os.makedirs(d, exist_ok=True)
            for root_, dirs, files in os.walk(s):
                rel = os.path.relpath(root_, s)
                td = os.path.normpath(os.path.join(d, rel))
                os.makedirs(td, exist_ok=True)
                for x in files:
                    if x in SKIP_DIR or x.endswith(('.bak-revl', '.stub-noop.bak', '.pyc')):
                        continue
                    shutil.copy2(os.path.join(root_, x), os.path.join(td, x))
                    n += 1
            continue
        else:
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d); n += 1
    print('  %d arquivo(s) sincronizado(s) cartao -> repo' % n)


def wait_run(tok, project, sha, tries=90):
    run = None
    for i in range(tries):
        runs = api(tok, '/repos/%s/actions/runs?head_sha=%s' % (project, sha))['workflow_runs']
        if runs:
            run = runs[0]
            print('  %s run #%s %s %s' % (time.strftime('%H:%M:%S'), run['run_number'],
                                          run['status'], run['conclusion']))
            if run['status'] == 'completed':
                return run
        time.sleep(20)
    return run


def install_from(tok, project, run, into):
    arts = api(tok, '/repos/%s/actions/runs/%s/artifacts' % (project, run['id']))['artifacts']
    if not arts:
        raise SystemExit('run sem artifact (o build falhou? veja tools/ci_log.py)')
    z = os.path.join(into, 'artifact.zip')
    print('  baixado %d bytes' % download(tok, arts[0]['archive_download_url'], z))
    root = os.path.join(into, 'x')
    shutil.rmtree(root, ignore_errors=True)
    with __import__('zipfile').ZipFile(z) as zf:
        zf.extractall(root)
    base = os.path.join(root, 'ultrakill_port')
    if not os.path.isdir(base):
        base = root
    n = 0
    for s, d in INSTALL:
        sp = os.path.join(base, s.replace('/', os.sep))
        dp = os.path.join(PORT, d.replace('/', os.sep))
        if not os.path.exists(sp):
            continue
        if os.path.exists(dp):
            b = dp + '.bak-revl'
            if not os.path.exists(b):
                shutil.copy2(dp, b)
        os.makedirs(os.path.dirname(dp), exist_ok=True)
        shutil.copy2(sp, dp)
        print('  %-38s -> %-32s %9d' % (s, d, os.path.getsize(dp)))
        n += 1
    print('  %d binario(s) instalados no port' % n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', required=True)
    ap.add_argument('-m', '--message', default='rev -L: binarios novos do port ULTRAKILL (TSP)')
    ap.add_argument('--no-push', action='store_true')
    ap.add_argument('--install-only', action='store_true')
    ap.add_argument('--keep', action='store_true', help='nao apaga ci-artifact/')
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)
    if not os.path.isdir(os.path.join(repo, '.git')):
        raise SystemExit('%s nao e um repo git' % repo)
    tok, project = creds(repo)
    into = os.path.join(PORT, 'ci-artifact')

    if not a.install_only:
        print('[1/5] sincronizando fontes')
        sync_to_repo(repo)
        st = sh(['git', 'status', '--porcelain'], cwd=repo).stdout.strip()
        if st and not a.no_push:
            print('[2/5] commit + push (%d linhas de diff)' % len(st.splitlines()))
            sh(['git', 'add', '-A'], cwd=repo)
            r = sh(['git', '-c', 'core.autocrlf=false', 'commit', '-m', a.message], cwd=repo)
            if r.returncode:
                print(r.stdout[-500:], r.stderr[-500:]); return 1
            p = sh(['git', 'push', 'origin', 'HEAD:main'], cwd=repo)
            if p.returncode:
                print('push falhou:', p.stderr[-800:]); return 1
        elif a.no_push:
            print('[2/5] push pulado (--no-push)')
        else:
            print('[2/5] nada para commitar')
    else:
        print('[1-2/5] pulado (--install-only)')

    sha = sh(['git', 'rev-parse', 'HEAD'], cwd=repo).stdout.strip()
    print('[3/5] esperando o run de ' + sha[:7])
    run = wait_run(tok, project, sha) if not a.install_only else \
        api(tok, '/repos/%s/actions/runs?per_page=1' % project)['workflow_runs'][0]
    if not run or run['status'] != 'completed':
        print('run nao completou'); return 1
    if run['conclusion'] != 'success':
        print('BUILD FALHOU (#%s). Erros:' % run['run_number'])
        sh([sys.executable, os.path.join(HERE, 'ci_log.py'), '--repo', repo,
            '--run', str(run['run_number'])])
        return 1
    print('  build #%s OK  %s' % (run['run_number'], run['html_url']))
    print('[4/5] baixando e instalando no port')
    os.makedirs(into, exist_ok=True)
    install_from(tok, project, run, into)
    if not a.keep:
        print('[5/5] limpando ' + into)
        shutil.rmtree(into, ignore_errors=True)
    else:
        print('[5/5] artifact mantido em ' + into)
    print('\nPRONTO. Ejete o cartao direito e de Play. Para ver os erros de um '
          'run:\n  python tools/ci_log.py --repo "%s" --run <n>' % repo)
    return 0


if __name__ == '__main__':
    sys.exit(main())
