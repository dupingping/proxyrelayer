all: smallproxyrelayer

smallproxyrelayer: smallproxyrelayer.c
	gcc -o smallproxyrelayer smallproxyrelayer.c -pthread `pkg-config --libs --cflags libxml-2.0`

clean:
	rm -f smallproxyrelayer
