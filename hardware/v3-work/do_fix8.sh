easyeda pcb modify --id a6531815f583b03f --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id a6531815f583b03f --center --x 310 --y 180 >/dev/null 2>&1 && echo 'R4 ok'
easyeda pcb modify --id c5d0e99f3dfc93a6 --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id c5d0e99f3dfc93a6 --center --x 315 --y 335 >/dev/null 2>&1 && echo 'R5 ok'
easyeda pcb modify --id 999f9a9be3137d2b --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 999f9a9be3137d2b --center --x 615 --y 245 >/dev/null 2>&1 && echo 'LED1 ok'
