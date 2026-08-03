easyeda pcb modify --id c642a7511c6ff6e4 --patch '{"rotation":180}' >/dev/null 2>&1
easyeda pcb modify --id c642a7511c6ff6e4 --center --x 1210 --y 150 >/dev/null 2>&1 && echo 'D2 ok'
easyeda pcb modify --id 8781e744a815ac5d --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 8781e744a815ac5d --center --x 1450 --y 140 >/dev/null 2>&1 && echo 'R7 ok'
easyeda pcb modify --id c5d0e99f3dfc93a6 --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id c5d0e99f3dfc93a6 --center --x 300 --y 265 >/dev/null 2>&1 && echo 'R5 ok'
easyeda pcb modify --id 0299bbf2c39a658f --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 0299bbf2c39a658f --center --x 64 --y 640 >/dev/null 2>&1 && echo 'R10 ok'
easyeda pcb modify --id 20b10b79440351e8 --patch '{"rotation":270}' >/dev/null 2>&1
easyeda pcb modify --id 20b10b79440351e8 --center --x 211 --y 640 >/dev/null 2>&1 && echo 'R11 ok'
easyeda pcb modify --id c7915009145b92da --patch '{"rotation":180}' >/dev/null 2>&1
easyeda pcb modify --id c7915009145b92da --center --x 620 --y 240 >/dev/null 2>&1 && echo 'R9 ok'
