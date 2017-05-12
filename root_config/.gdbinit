set can-use-hw-watchpoints 0
define asst3
dir /Users/Ck/Documents/Masters_UNSW/S1_2017/COMP3231-OS/ass1/asst1-src/kern/compile/ASST3

target remote localhost:16161
b panic
end
