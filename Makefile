CFLAGS = -g3 -Wall -Wextra -Wconversion -Wcast-qual -Wcast-align -g
CFLAGS += -Winline -Wfloat-equal -Wnested-externs
CFLAGS += -pedantic -std=gnu99 -Werror -D_GNU_SOURCE -std=gnu99
CC = gcc

PROMPT = -DPROMPT

all: 33sh 33noprompt

33sh: sh.c
	#TODO: compile your program, including the -DPROMPT macro
	$(CC) $(CFLAGS) $(PROMPT) $^ -o $@
33noprompt: sh.c
	#TODO: compile your program without the prompt macro
	$(CC) $(CFLAGS) $^ -o $@

jobs: jobs.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	#TODO: clean up any executable files that this Makefile has produced
	rm -f 33sh 33noprompt
