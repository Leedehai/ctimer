# Copyright: see README and LICENSE under the project root directory.
# Author: @Leedehai
#
# File: Makefile
# ---------------------------
# The makefile.

CC=g++ # on macOS it's actually clang++

ctimer : ctimer.cc
	$(CC) -std=c++14 -Wall -Wpedantic -DNDEBUG -O3 $^ -pthread -o $@

clean :
	rm -f ctimer

.PHONY : clean
