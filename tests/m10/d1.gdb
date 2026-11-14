break d1.cpp:7
ignore 1 2
run
echo == square at 7, third call\n
info args
info locals
bt
echo == sum\n
frame 1
info args
info locals
echo == main\n
frame 2
info locals
delete
break d1.cpp:18
continue
echo == sum at 18\n
info args
info locals
