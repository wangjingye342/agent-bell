easyeda pcb modify --id c642a7511c6ff6e4 --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id c642a7511c6ff6e4 --center --x 895 --y 215 >/dev/null 2>&1 && echo 'D2 ok'
easyeda pcb modify --id 8781e744a815ac5d --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 8781e744a815ac5d --center --x 1450 --y 120 >/dev/null 2>&1 && echo 'R7 ok'
easyeda pcb modify --id 20b10b79440351e8 --patch '{"rotation":270}' >/dev/null 2>&1
easyeda pcb modify --id 20b10b79440351e8 --center --x 240 --y 640 >/dev/null 2>&1 && echo 'R11 ok'
easyeda pcb modify --id c5d0e99f3dfc93a6 --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id c5d0e99f3dfc93a6 --center --x 310 --y 280 >/dev/null 2>&1 && echo 'R5 ok'
