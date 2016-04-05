g++ -Wall -O2 -std=c++0x -g -c main.cpp -o main.o
g++ -o cmwax main.o -lpthread -ljack -lboost_program_options
