break w1.cpp:9
run
echo == inner at 9\n
info args
info locals
bt
echo == middle\n
frame 1
info args
info locals
echo == outer\n
frame 2
info args
info locals
