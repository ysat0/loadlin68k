loadlin.x: loadlin.o jump.o jump030.o
	gcc -o loadlin.x loadlin.o jump.o jump030.o -ldos -liocs

loadlin.o: loadlin.c
	gcc -O2 -c -o loadlin.o loadlin.c

jump.o: jump.s
	gcc -c -o jump.o jump.s

jump030.o: jump030.s
	gcc -c -o jump030.o jump030.s
