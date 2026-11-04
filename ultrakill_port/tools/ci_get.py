#!/usr/bin/env python3
"""ci_get.py - espera o run do commit HEAD, lista artifacts e baixa/instala.

Uso:
  python3 tools/ci_get.py --repo <dir github-upload> --wait      # so espera
  python3 tools/ci_get.py --repo <d> --wait --dl --into <dir>    # espera + baixa
  python3 tools/ci_get.py --repo <d> --run 113 --dl --into /tmp/x

O endpoint de artifacts responde 302 para um URL assinado do blob storage; la o
header Authorization NAO pode ir (senao 403). Por isso o redirect e seguido a
mao, com um opener sem credencial.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

API = 'https://api.github.com'


def creds(repo_dir):
    url = subprocess.run(['git', '-C', repo_dir, 'config', '--get', 'remote.origin.url'],
                         capture_output=True, text=True).stdout.strip()
    m = re.match(r'https://(?:([^:@/]+)(?::([^@/]+))?@)?([^/]+)/(.+?)(?:\.git)?$', url)
    if not m:
        raise SystemExit('remote nao entendido: ' + repo_dir)
    return (m.group(2) or m.group(1)), m.group(4)


def req_raw(url, tok=None):
    h = {'User-Agent': 'ultrakill-ci'}
    if tok:
        h['Authorization'] = 'token ' + tok
    return urllib.request.Request(url, headers=h)


def api(tok, path):
    with urllib.request.urlopen(req_raw(API + path, tok), timeout=120) as r:
        return json.loads(r.read() or b'{}')


def download(tok, url, out):
    """segue o 302 do storage SEM levar o Authorization."""
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None
    opener = urllib.request.build_opener(NoRedirect)
    try:
        with opener.open(req_raw(url, tok), timeout=300) as r:
            data = r.read()
    except urllib.error.HTTPError as e:
        loc = e.headers.get('location') if e.code in (301, 302, 303, 307, 308) else None
        if not loc:
            raise
        with urllib.request.urlopen(req_raw(loc), timeout=300) as r2:
            data = r2.read()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'wb').write(data)
    print('   salvo %s (%d bytes)' % (out, len(data)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', required=True)
    ap.add_argument('--sha', default='HEAD')
    ap.add_argument('--run', type=int, default=0)
    ap.add_argument('--wait', action='store_true')
    ap.add_argument('--dl', action='store_true')
    ap.add_argument('--into', default=os.path.join(os.getcwd(), 'ci-artifact'))
    ap.add_argument('--tries', type=int, default=45)
    a = ap.parse_args()

    tok, project = creds(os.path.abspath(a.repo))
    sha = subprocess.run(['git', '-C', os.path.abspath(a.repo), 'rev-parse', a.sha],
                         capture_output=True, text=True).stdout.strip()
    run = None
    if a.run:
        for i in range(6):
            runs = api(tok, '/repos/%s/actions/runs?per_page=50' % project)['workflow_runs']
            run = next((r for r in runs if r['run_number'] == a.run), None)
            if run:
                break
            time.sleep(5)
    else:
        for i in range(a.tries):
            runs = api(tok, '/repos/%s/actions/runs?head_sha=%s' % (project, sha))['workflow_runs']
            if runs:
                run = runs[0]
                print('%s run #%s %s %s' % (time.strftime('%H:%M:%S'), run['run_number'],
                                            run['status'], run['conclusion']))
                if run['status'] == 'completed' or not a.wait:
                    break
            else:
                print('%s aguardando o run aparecer...' % time.strftime('%H:%M:%S'))
            time.sleep(20)
    if not run:
        raise SystemExit('nenhum run para %s' % sha[:7])
    print('run #%s  %s  conclusion=%s  %s' % (run['run_number'], run['name'],
                                              run['conclusion'], run['html_url']))
    arts = api(tok, '/repos/%s/actions/runs/%s/artifacts' % (project, run['id']))['artifacts']
    for x in arts:
        print('  artifact: %s (%d bytes)' % (x['name'], x['size_in_bytes']))
    if not a.dl:
        return 0
    for x in arts:
        z = os.path.join(a.into, x['name'] + '.zip')
        download(tok, x['archive_download_url'], z)
        with zipfile_extract(z, os.path.join(a.into, x['name'])) as _n:
            pass
    return 0


def zipfile_extract(zippath, dest):
    import contextlib
    import zipfile
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(zippath) as z:
        z.extractall(dest)
        print('   extraido em %s (%d arquivos)' % (dest, len(z.namelist())))
    class _N:
        pass
    return contextlib.nullcontext(_N())


if __name__ == '__main__':
    sys.exit(main())
