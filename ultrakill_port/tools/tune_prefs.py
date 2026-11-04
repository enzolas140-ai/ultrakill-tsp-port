#!/usr/bin/env python3
"""tune_prefs.py - garante que as preferencias do ULTRAKILL (mobile) nao
deixem o port mudo / travado em 5 FPS / em janela.

POR QUE ISTO EXISTE
-------------------
O build Android/Ultradroid guarda as opcoes do proprio jogo em
<external files>/Preferences/Prefs.json + LocalPrefs.json (o ULTRAKILL tem um
SaveSystem proprio, nao e o PlayerPrefs do Unity). No SD deste port os dois
arquivos estavam assim (verificado em 2026-11-03):

  Prefs.json      : "allVolume": 0.0, "musicVolume": 0.0     <- SOM ZERADO
  LocalPrefs.json : "frameRateLimit": 5, "fullscreen": false <- 5 FPS / janela

Ou seja: mesmo com o backend de audio consertado, o jogo nao tinha o que
tocar - e o cap de 5 fps faz qualquer boot parecer "tela parada". Este script
sao os tres valores acima, SEM tocar no resto (gameSpeed, damageTaken,
screenShake, hud* sao escolha do jogador e ficam intactos).

Reversivel: na primeira pasada grava Prefs.json.bak-ark / LocalPrefs.json.bak-ark
e nunca sobrescreve esse backup.

Uso:
  python3 tools/tune_prefs.py <dir-de-dados-do-jogo> [--dry]
  (o launcher chama em data/files/ a cada boot; e idempotente)
"""
import json
import os
import sys

# (arquivo, chave, condicao, valor novo, motivo)
RULES = [
    ("Prefs.json", "allVolume", lambda v: float(v or 0) < 0.05, 1.0,
     "volume master em 0 = port mudo mesmo com audio funcionando"),
    ("Prefs.json", "musicVolume", lambda v: float(v or 0) < 0.05, 1.0,
     "musica em 0"),
    ("LocalPrefs.json", "frameRateLimit", lambda v: float(v or 0) < 30, 60.0,
     "cap < 30 fps faz o boot parecer tela congelada"),
    ("LocalPrefs.json", "fullscreen", lambda v: v is not True, True,
     "em janela o SDL/ES nao assume o painel"),
]

BACKUP = ".bak-ark"


def main():
    args = [a for a in sys.argv[1:] if a != "--dry"]
    dry = "--dry" in sys.argv[1:]
    base = args[0] if args else "."
    prefdir = os.path.join(base, "Preferences")
    if not os.path.isdir(prefdir):
        print("Preferences/ nao existe em %s (primeiro boot? nada a fazer)" % base)
        return 0
    changed = 0
    for fname, key, bad, new, why in RULES:
        path = os.path.join(prefdir, fname)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as e:                      # arquivo corrompido: nao esbarra
            print("AVISO: %s ilegivel (%s) - deixando como esta" % (fname, e))
            continue
        if key not in data:
            continue
        cur = data[key]
        try:
            need = bad(cur)
        except Exception:
            need = False
        if not need:
            continue
        print("%s: %s = %r -> %r  (%s)" % (fname, key, cur, new, why))
        changed += 1
        if dry:
            continue
        bak = path + BACKUP
        if not os.path.exists(bak):
            try:
                with open(path, "rb") as src, open(bak, "wb") as dst:
                    dst.write(src.read())
            except Exception as e:
                print("  AVISO: backup falhou (%s)" % e)
        data[key] = new
        tmp = path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
            os.replace(tmp, path)
        except Exception as e:
            print("  ERRO escrevendo %s (%s)" % (fname, e))
    if not changed:
        print("preferencias ja estão ok (volumes > 0, fps cap, fullscreen)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
