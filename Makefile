all:
	g++ -Wall -Wextra -std=c++20 -O3 -pedantic main.cpp -o matcher

clean:
	rm matcher
