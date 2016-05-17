CC=gcc
FLAGS=-pg -g --std=gnu99
LFLAGS=-pthread -lrt
DISTNAME=lab3a-604480880.tar.gz

default: lab3a 

lab3a: lab3a.c 
	$(CC) $(FLAGS) -o $@ $^ $(LFLAGS)
	
dist: $(DISTNAME)

$(DISTNAME) : Makefile lab2c.c README SortedList.h SortedList.c timePerOperation.png
	tar -cvzf $(DISTNAME) $^