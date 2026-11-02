clear
echo
printf '\033[1;36m  BlackCore 0.1.0 Alpha\033[0m — этап 1: терминал работает\n\n'
printf 'Кириллица:  Привет, мир!  ЙЦУКЕН  ёЁ\n'
printf 'CJK:        日本語  中文  한국어\n'
printf '\033[4mподчёркнутый\033[0m  \033[1;33mжирный жёлтый\033[0m  \033[7mинверсия\033[0m\n'
printf 'Палитра:    '
for c in 1 2 3 4 5 6 7; do printf "\033[1;3${c}m████\033[0m"; done
printf '\n256 цветов: '
for i in 196 202 208 214 220 226 118 82 46 39 33 27 57 93 129 165; do printf "\033[48;5;${i}m  \033[0m"; done
printf '\n\n'
ls --color=always -d /usr/bin /etc /var
echo
