all: read_a2d.c read_a2d.h
	gcc read_a2d.c -Wall -Wswitch -pthread -O2 -o read_a2d

clean:
	rm read_a2d
