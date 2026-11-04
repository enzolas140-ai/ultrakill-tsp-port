import re
s = open('bender/unity_probe.c', encoding='utf-8', errors='replace').read()
s2 = re.sub(r'//[^\n]*', '', s)
s2 = re.sub(r'/\*.*?\*/', '', s2, flags=re.S)
s2 = re.sub(r'"(\\.|[^"\\])*"', '""', s2)
s2 = re.sub(r"'(\\.|[^'\\])*'", "''", s2)
print('unity_probe.c  braces=%+d parens=%+d' % (
    s2.count('{') - s2.count('}'), s2.count('(') - s2.count(')')))
print('patch START+R1 presente:', s.count('BENDER_MOUSE_TOGGLE_START_R1'), 'vezes')
print('patch clique B presente:', s.count('BENDER_SDL_CONTROLLER_BUTTON_B)||bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_A)'))
