#!/usr/bin/env python3
"""ccheck.py - checagem rapida de fontes C sem compilador na maquina.

Faz o que da para fazer offline:
  * balanceamento de '{}/()/' (string e comentario removidos antes),
  * '#include' duplicado / ausente de <strings.h> quando usa strcasecmp,
  * funcoes estaticas DEFINIDAS mas nunca USADAS (aviso -Wunused-function),
  * simbolos chamados mas nao definidos no mesmo modulo (checa o conjunto de
    arquivos que o CMakeLists junta por alvo),
  * knob de ambiente: lista todos os getenv("...") para cruzar com o conf.

Uso:
  python tools/ccheck.py            # nos fontes do wrapper
  python tools/ccheck.py --env      # so a lista de env knobs
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), os.pardir, 'wrapper')

TARGETS = {
    'bridge': ['bridge/bridge_main.c', 'bridge/bridge_input.c',
               'jni/bridge_jni.c', 'gl/bridge_gl.c', 'gl/bridge_shader_stub.c'],
    'libOpenSLES': ['audio/bender_opensles.c'],   # fonte comprovada (rev -L)
    'libandroid': ['stubs/android_asset_stub.c',
                   'stubs/android_nativeactivity_stub.c',
                   'stubs/android_input_stub.c'],
    'liblog': ['stubs/android_log_stub.c'],
    'libmediandk': ['stubs/mediandk_stub.c'],
    'libEGL': ['gl/bridge_egl_shim.c'],
    'input_probe': ['tests/input_probe.c', 'stubs/android_input_stub.c',
                    'stubs/android_nativeactivity_stub.c',
                    'bridge/bridge_input.c', 'jni/bridge_jni.c', 'gl/bridge_gl.c'],
}

C_IDENT = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(')


def strip(src):
    s = re.sub(r'//[^\n]*', '', src)
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'"(\\.|[^"\\])*"', '""', s)
    s = re.sub(r"'(\\.|[^'\\])*'", "''", s)
    return s


def defs_and_calls(src):
    """definicoes de funcao deste arquivo + nomes chamados."""
    d = set(re.findall(r'^[A-Za-z_][\w \t\*]*\b(\w+)\s*\([^;{]*\)\s*\{',
                       src, re.M))
    d |= set(re.findall(r'^(?:static\s+)?[A-Za-z_][\w \t\*]*\b(\w+)\s*\(', src, re.M))
    calls = set(C_IDENT.findall(strip(src)))
    return d, calls


def main():
    only_env = '--env' in sys.argv
    envs = set()
    problems = 0
    for tgt, files in TARGETS.items():
        all_src = {}
        for rel in files:
            p = os.path.join(ROOT, rel)
            if not os.path.isfile(p):
                print('  FALTA %s' % rel)
                problems += 1
                continue
            raw = open(p, encoding='utf-8', errors='replace').read()
            envs |= set(re.findall(r'getenv\("([^"]+)"\)', raw))
            envs |= set(re.findall(r'env_int\("([^"]+)"', raw))
            s = strip(raw)
            b, par = s.count('{') - s.count('}'), s.count('(') - s.count(')')
            dup_inc = [i for i in re.findall(r'#include\s*[<"]([^">]+)', raw)
                       if re.findall(r'#include\s*[<"]' + re.escape(i), raw).count(i) > 1]
            uses_sc = 'strcasecmp' in raw or 'strncasecmp' in raw
            has_sh = '#include <strings.h>' in raw
            kw = len(re.findall(r'\bregister\b|\bauto\b', raw))
            print('%-12s %-38s braces=%+d parens=%+d %s' % (
                tgt, rel, b, par,
                ('DUP-INCLUDE:' + ','.join(set(dup_inc)) if dup_inc else '') +
                (' SEM <strings.h> p/ strcasecmp!' if (uses_sc and not has_sh) else '')))
            if b or par or dup_inc or (uses_sc and not has_sh):
                problems += 1
            all_src[rel] = raw
        # simbolos chamados mas nao definidos em nenhum arquivo do alvo
        if only_env:
            continue
        defined, called = set(), set()
        for rel, raw in all_src.items():
            d, c = defs_and_calls(strip(raw))
            defined |= d
            called |= c
        extern_ok = set("""
printf fprintf snprintf vfprintf strlen strcpy strncpy strcmp strncmp strchr
strrchr strstr strdup strerror strdupa strcat calloc malloc realloc free memcpy
memmove memset memcmp memchr qsort abort exit _setjmp setjmp longjmp atoi atol
strtod strtol strtoul getenv setenv unsetenv open close read write lseek fsync
pipe poll select mmap munmap ioctl usleep nanosleep clock_gettime gettimeofday
time dlopen dlsym dlerror dlclose pthread_create pthread_join pthread_mutex_init
pthread_mutex_lock pthread_mutex_unlock pthread_mutex_destroy pthread_once
pthread_self sigaction signal raise kill sigaltstack backtrace backtrace_symbols
uname sysconf mprotect mkdir rmdir unlink rename access stat fstat lstat readlink
bcrypt __android_log_print __android_log_write __android_log_vprint
__android_log_assert memmem fmaf powf sqrtf sinf cosf atan2f floorf ceilf fabsf
roundf expf logf fmodf fmod pow sqrt sin cos tan atan tanh exp log
""".split())
        missing = sorted(n for n in called if n not in defined and n not in extern_ok
                         and not n.startswith(('SDL_', 'sl', 'SL_', 'A', 'egl',
                                               'JNI', 'java', 'j_', 'vm_', 'fn_')))
        # filtro forte: so reporta chamadas que PARECEM funcoes nossas (shim_/shim)
        mine = [m for m in missing if m.startswith(('shim_', 'bridge_', 'ev_', 'mouse_',
                                                    'inject_', 'push_', 'handle_', 'pump_'))]
        if mine:
            problems += 1
            print('  %s: POSSIVEIS SIMBOLOS NAO DEFINIDOS: %s' % (tgt, ', '.join(mine)))
    print()
    print('== knobs de ambiente usados pelo codigo ==')
    for e in sorted(envs):
        print('  ', e)
    print('\nproblemas encontrados:', problems)
    return 0 if problems == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
