/* opensles_stub.c - ARQUIVO APOSENTADO (nao e mais compilado).
 *
 * A partir da rev -L (2026-11-03) o libOpenSLES.so do port e construido a
 * partir de wrapper/audio/bender_opensles.c - a MESMA fonte do bridge que o
 * loader BENDER usa e que o CI ja compila (see .github/workflows/build-wrapper.yml:
 *   gcc -O2 -fPIC -shared bender/bender_opensles.c -o audio_bridge/libOpenSLES.so).
 *
 * Por que nao a implementation anterior deste arquivo: ela foi escrita de
 * memoria do header Khronos e errava CONSTANTES que o FMOD compara em runtime,
 * nomeadamente
 *   SL_PLAYSTATE_PLAYING            (real = 3;  aqui estava 2)
 *   SL_OBJECT_STATE_REALIZED        (real = 3;  aqui estava 2)
 *   SL_DATAFORMAT_PCM               (real = 2;  aqui estava 4)
 *   SL_DATALOCATOR_*                (real: BUFFERQUEUE=6, OUTPUTMIX=4,
 *                                    ANDROIDSIMPLEBUFFERQUEUE=0x800007BD)
 *   SL_RESULT_FEATURE_UNSUPPORTED   (real = 12; aqui estava 43)
 * Com PLAYSTATE errado, o SetPlayState(PLAYING) do FMOD nunca batia com o
 * "playing" interno -> nenhum callback de buffer queue -> audio morto mesmo
 * com o dispositivo aberto. Os valores corretos estao documentados (e usados)
 * em audio/bender_opensles.c e em include/sles_shim.h.
 *
 * Este arquivo fica vazio de proposito para nao virar fonte de verdade pela
 * metade no proximo refactor. Se precisar mexer no som, mexa em
 * audio/bender_opensles.c. */
typedef int ultrakill_opensles_stub_retired__;
