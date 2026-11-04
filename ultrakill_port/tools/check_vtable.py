#!/usr/bin/env python3
"""check_vtable.py - acha slots da vtable JNI que nao tem funcao definida.

Motivo (hist real): o run #112 do Actions caiu em
  bridge_jni.c:1182: error: 'j_CallNonvirtualDoubleMethodV' undeclared here
porque a vtable `.Slot = func` referenciava uma funcao que os macros
(EV_NV_VA etc) nao geraram. Sem compilador na maquina, a checagem e por
contagem de ocorrencias: um nome `j_X` citado na vtable e que SO aparece ali
(1 vez no arquivo inteiro) nao foi definido nem declarado -> o gcc reclama.

Saida: linha por nome suspeito + contexto (em que macro/definicao ele aparece),
para saber se e gerado por macro (ok, o grep nao enxerga a expansao) ou
realmente ausente.

Uso:  python3 tools/check_vtable.py [caminho/bridge_jni.c]
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.normpath(os.path.join(HERE, os.pardir, 'wrapper', 'jni', 'bridge_jni.c'))

VTABLE_USE = re.compile(r'\.\w+\s*=\s*(j_[A-Za-z0-9_]+)\s*[,}]')
DEF_LIKE = re.compile(r'(?:static|__attribute__)[^;{]*?\b(j_[A-Za-z0-9_]+)\s*\(')
MACRO_GEN = re.compile(r'^(?:EV_CALL_INT_IMPL|EV_CALL_INT_IMPL_V|EV_CALL_INT_IMPL_A'
                       r'|EVF_V|EVF_A|EV_NV_INT|EV_NV_VA|FIELD_STUB_INT'
                       r'|FIELD_STUB_OBJ|FIELD_STUB_SET_I|STUB_STATIC_VA'
                       r'|NEW_PRIM_ARRAY|GET_ARRAY_ELEMENTS|RELEASE_ARRAY'
                       r'|GET_REGION|SET_REGION)(?:_V|_A)?\b')


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    if not os.path.isfile(path):
        raise SystemExit('nao achei %s' % path)
    txt = open(path, encoding='utf-8', errors='replace').read()
    lines = txt.splitlines()

    # nomes que os macros geram (a concatenacao ## acontece so no preprocessador)
    macro_targets = set()
    for l in lines:
        s = l.strip()
        if MACRO_GEN.match(s):
            for tok in re.findall(r'\b(j_[A-Za-z0-9_]+)\b', s):
                macro_targets.add(tok)
            # EV_NV_VA(core_x, ret, j_CallNonvirtualFloatMethod) gera <nome>V e <nome>A
            for tok in re.findall(r'\b(j_[A-Za-z0-9_]+)\b(?=\s*[,)])', s):
                if 'EV_NV_VA' in s or 'STUB_STATIC_VA' in s:
                    # esses macros colam 'V' e 'A' no nome (nm##V / fn##V): as
                    # variantes EXISTEM mesmo sem aparecer escritas no fonte.
                    macro_targets.add(tok + 'V')
                    macro_targets.add(tok + 'A')
                if s.startswith('EV_CALL_INT_IMPL('):
                    macro_targets.add(tok)

    used = []
    for m in VTABLE_USE.finditer(txt):
        if m.group(1) not in used:
            used.append(m.group(1))

    missing = []
    for name in used:
        if name == 'NULL':
            continue
        n = len(re.findall(r'\b%s\b' % re.escape(name), txt))
        if n <= 1 and name not in macro_targets:
            missing.append(name)
        elif n <= 1 and name in macro_targets:
            pass  # gerado por macro - ok

    print('vtable: %d slots com funcao; %d suspeitos de nao existir' % (len(used), len(missing)))
    for name in missing:
        print('  FALTA: %s' % name)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
