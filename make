g++ -Wall -O2 -std=c++0x -g -c main.cpp -o main.o
g++ -Wall -O2 -std=c++0x -g -c xwax.cpp -o xwax.o
g++ -o cmwax main.o xwax.o -lpthread -ljack -lboost_program_options
