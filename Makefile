# Copyright: see README and LICENSE under the project root directory.
# Author: @Leedehai
#
# File: Makefile
# ---------------------------
# The makefile.

CXX=g++ # on macOS it's actually clang++

ctimer : ctimer.cc
	$(CXX) -std=c++14 -Wall -Wpedantic -DNDEBUG -O3 $^ -pthread -o $@

check :
	./test.py

clean :
	rm -f ctimer

.PHONY : check clean
