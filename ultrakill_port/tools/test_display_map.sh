#!/bin/bash
# test_display_map.sh - executa SO a funcao uk_display_map, extraida do launcher
# real do cartao, para responder uma pergunta boba: por que ela nao imprime nada?
# (o launcher no device roda como usuario sem privilegios; sysfs e /proc podem nao
# existir/nao poder ler, e 'set -u' esta ligado no script original.)
SRC="${1:-ultrakill_start.sh}"
OUT=/tmp/uk_map_test.log
rm -f "$OUT"

# extrai do 'uk_display_map() {' ate a linha que so fecha a funcao
awk '/^  uk_display_map\(\)/{f=1} f{print} f&&/^  uk_display_map$/{exit}' "$SRC" > /tmp/uk_map_fn.sh
echo "--- funcao extraida: $(wc -l < /tmp/uk_map_fn.sh) linhas ---"
tail -3 /tmp/uk_map_fn.sh

{
  echo "LOG=$OUT"
  echo 'set -u'
  echo 'LOG=/tmp/uk_map_test.log'
  cat /tmp/uk_map_fn.sh
  echo 'uk_display_map'
  echo 'echo "rc_da_funcao=$?"'
} > /tmp/uk_map_run.sh

echo "--- rodando ---"
bash /tmp/uk_map_run.sh
echo "--- saida produzida em \$LOG ---"
if [ -f "$OUT" ]; then sed 's/^/  | /' "$OUT"; else echo "  (arquivo NAO criado)"; fi
