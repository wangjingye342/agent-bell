easyeda pcb modify --id 1bec93641a8fcbf1 --patch '{"rotation":180}' >/dev/null 2>&1
easyeda pcb modify --id 1bec93641a8fcbf1 --center --x 1560 --y 163 >/dev/null 2>&1 && echo 'R2 ok'
easyeda pcb modify --id 0299bbf2c39a658f --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id 0299bbf2c39a658f --center --x 80 --y 650 >/dev/null 2>&1 && echo 'R10 ok'
easyeda pcb modify --id 20b10b79440351e8 --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id 20b10b79440351e8 --center --x 235 --y 650 >/dev/null 2>&1 && echo 'R11 ok'
easyeda pcb modify --id 1ea98462518e798e --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 1ea98462518e798e --center --x 390 --y 650 >/dev/null 2>&1 && echo 'C6 ok'
