#!/usr/bin/env python3
"""ci_log.py - baixa o log do run do GitHub Actions e mostra os erros de build.

O build do port e no runner arm64 do GitHub (nao ha gcc/cmake na maquina do
usuario), entao quando um run falha a unica evidencia viva e o log do job.
Este script le o token do proprio `git config remote.origin.url` do repo (nada
fica em arquivo e nada e impresso) e puxa os logs pela API.

Uso:
  python3 tools/ci_log.py --repo "C:/Users/<user>/.../github-upload"
  python3 tools/ci_log.py --repo <dir> --run 112 --full
  python3 tools/ci_log.py --repo <dir> --grep "error:"
"""
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

ERR_RE = re.compile(r'(error:|fatal error|undefined reference|collect2|'
                    r'cannot find|No such file|FAILED|Error [0-9]|\bFAILED\b)', re.I)


def api_token(repo_dir):
    url = subprocess.run(['git', '-C', repo_dir, 'config', '--get', 'remote.origin.url'],
                         capture_output=True, text=True).stdout.strip()
    m = re.match(r'https://(?:([^:@/]+)(?::([^@/]+))?@)?([^/]+)/(.+?)(?:\.git)?$', url)
    if not m:
        raise SystemExit('remote do repo nao entendido: %s' % repo_dir)
    return (m.group(2) or m.group(1)), m.group(4)


def get(tok, path):
    """GET na API. O endpoint de logs do Actions responde 302 para um URL
    assinado do blob storage - e LA o header Authorization tem que NAO ir,
    senao o storage devolve 403 'failed to authenticate'. Por isso o redirect
    e seguido a mao, sem o header."""
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request('https://api.github.com' + path,
                                 headers={'Authorization': 'token ' + tok,
                                          'User-Agent': 'ultrakill-ci-log',
                                          'Accept': 'application/vnd.github+json'})
    try:
        with opener.open(req, timeout=120) as r:
            raw, ctype = r.read(), (r.headers.get('content-type') or '')
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            loc = e.headers.get('location')
            if not loc:
                raise
            with urllib.request.build_opener().open(urllib.request.Request(
                    loc, headers={'User-Agent': 'ultrakill-ci-log'}), timeout=180) as r2:
                return r2.read().decode('utf-8', 'replace')
        raise
    if 'json' in ctype:
        return json.loads(raw or b'{}')
    return raw.decode('utf-8', 'replace')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', required=True, help='diretorio do git (github-upload)')
    ap.add_argument('--run', type=int, default=0, help='numero do run (0 = ultimo)')
    ap.add_argument('--grep', default=None)
    ap.add_argument('--full', action='store_true', help='salva o log em ci-last-*.log')
    a = ap.parse_args()

    tok, repo = api_token(os.path.abspath(a.repo))
    if a.run:
        runs = get(tok, '/repos/%s/actions/runs?per_page=30' % repo)
        cand = [r for r in runs.get('workflow_runs', []) if r['run_number'] == a.run]
        if not cand:
            raise SystemExit('run #%s nao achado' % a.run)
        run = cand[0]
    else:
        run = get(tok, '/repos/%s/actions/runs?per_page=1' % repo)['workflow_runs'][0]

    print('run #%s  %s  status=%s conclusion=%s' % (
        run['run_number'], run['name'], run['status'], run['conclusion']))
    print('  %s' % run['html_url'])

    for j in get(tok, '/repos/%s/actions/runs/%s/jobs' % (repo, run['id']))['jobs']:
        print('== job %s (%s): %s' % (j['name'], j['conclusion'], j['id']))
        for st in j.get('steps', []):
            if st.get('conclusion') in ('failure', 'cancelled'):
                print('   STEP QUE FALHOU: %s (%s)' % (st['name'], st['conclusion']))
        txt = get(tok, '/repos/%s/actions/jobs/%s/logs' % (repo, j['id']))
        if not isinstance(txt, str):
            txt = json.dumps(txt)
        if a.full:
            out = os.path.join(os.getcwd(), 'ci-last-%s-%s.log' % (run['run_number'], j['id']))
            open(out, 'w', encoding='utf-8').write(txt)
            print('   dump -> %s' % out)
        lines = txt.splitlines()
        shown, printed = set(), 0
        for i, l in enumerate(lines):
            if not ERR_RE.search(l):
                continue
            for k in range(max(0, i - 2), min(len(lines), i + 4)):
                if k in shown:
                    continue
                if a.grep and a.grep.lower() not in lines[k].lower():
                    continue
                shown.add(k)
                print('   | ' + lines[k][:300])
                printed += 1
            if printed > 150:
                break
        if not printed:
            print('   (sem erro obvio; cauda:)')
            for l in lines[-6:]:
                print('   > ' + l[:260])
    return 0


if __name__ == '__main__':
    sys.exit(main())
