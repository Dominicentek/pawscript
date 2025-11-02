clang++ pawscript.cpp -g -O3 -c -o pawscript.o
clang interpreter.c -g -c -o interpreter.o

clang++ pawscript.o interpreter.o -g -o paws