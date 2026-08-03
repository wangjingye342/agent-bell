easyeda pcb modify --id 999f9a9be3137d2b --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 999f9a9be3137d2b --center --x 545 --y 120 >/dev/null 2>&1 && echo 'LED1 ok'
easyeda pcb modify --id c7915009145b92da --patch '{"rotation":180}' >/dev/null 2>&1
easyeda pcb modify --id c7915009145b92da --center --x 630 --y 255 >/dev/null 2>&1 && echo 'R9 ok'
easyeda pcb modify --id c642a7511c6ff6e4 --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id c642a7511c6ff6e4 --center --x 770 --y 312 >/dev/null 2>&1 && echo 'D2 ok'
easyeda pcb modify --id a6531815f583b03f --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id a6531815f583b03f --center --x 363 --y 195 >/dev/null 2>&1 && echo 'R4 ok'
easyeda pcb modify --id c5d0e99f3dfc93a6 --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id c5d0e99f3dfc93a6 --center --x 370 --y 315 >/dev/null 2>&1 && echo 'R5 ok'
easyeda pcb modify --id 8781e744a815ac5d --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id 8781e744a815ac5d --center --x 1270 --y 880 >/dev/null 2>&1 && echo 'R7 ok'
easyeda pcb modify --id 20b10b79440351e8 --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id 20b10b79440351e8 --center --x 330 --y 775 >/dev/null 2>&1 && echo 'R11 ok'
easyeda pcb modify --id 1ea98462518e798e --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id 1ea98462518e798e --center --x 330 --y 690 >/dev/null 2>&1 && echo 'C6 ok'
