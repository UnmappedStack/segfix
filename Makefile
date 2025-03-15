# This makefile is mostly for the sake of testing, so it's not a complete or half decent makefile.

test: build
	@echo " -> Building test program for segfix"
	$(CC) -c test.c -o test.o -fno-pie -no-pie -g
	$(CC) segfix.o test.o -o out -no-pie

build:
	@echo " -> Building segfix"
	$(CC) -c segfix.c -o segfix.o -fno-pie -no-pie -Wall -Werror -g -pedantic
