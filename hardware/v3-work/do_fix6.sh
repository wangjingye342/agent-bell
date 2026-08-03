easyeda pcb modify --id 999f9a9be3137d2b --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 999f9a9be3137d2b --center --x 415 --y 120 >/dev/null 2>&1 && echo 'LED1 ok'
easyeda pcb modify --id c7915009145b92da --patch '{"rotation":180}' >/dev/null 2>&1
easyeda pcb modify --id c7915009145b92da --center --x 630 --y 290 >/dev/null 2>&1 && echo 'R9 ok'
easyeda pcb modify --id 31e9d8f8e9d3b407 --patch '{"rotation":90}' >/dev/null 2>&1
easyeda pcb modify --id 31e9d8f8e9d3b407 --center --x 690 --y 430 >/dev/null 2>&1 && echo 'D1 ok'
easyeda pcb modify --id a6531815f583b03f --patch '{"rotation":0}' >/dev/null 2>&1
easyeda pcb modify --id a6531815f583b03f --center --x 363 --y 210 >/dev/null 2>&1 && echo 'R4 ok'
