# Build ProcShell from the organized src/ and include/ folders.
# Object files are removed after linking so the project folder stays clean.

CC=gcc
CFLAGS=-c -Wall -g -Iinclude

# Default target: compile all modules, link procsh, then remove temporary objects.
all: main shell command parser exec process trace procfs
	$(CC) main.o shell.o command.o parser.o exec.o process.o trace.o procfs.o -o procsh -lreadline
	rm -f *.o

main: src/main.c include/shell.h
	$(CC) $(CFLAGS) src/main.c -o main.o

shell: src/shell.c include/shell.h include/command.h include/parser.h include/exec.h include/process.h include/colors.h
	$(CC) $(CFLAGS) src/shell.c -o shell.o

command: src/command.c include/command.h
	$(CC) $(CFLAGS) src/command.c -o command.o

parser: src/parser.c include/parser.h include/command.h include/colors.h
	$(CC) $(CFLAGS) src/parser.c -o parser.o

exec: src/exec.c include/exec.h include/shell.h include/command.h include/process.h include/trace.h include/procfs.h include/colors.h
	$(CC) $(CFLAGS) src/exec.c -o exec.o

process: src/process.c include/process.h include/colors.h
	$(CC) $(CFLAGS) src/process.c -o process.o

trace: src/trace.c include/trace.h include/shell.h include/command.h include/colors.h
	$(CC) $(CFLAGS) src/trace.c -o trace.o

procfs: src/procfs.c include/procfs.h include/colors.h
	$(CC) $(CFLAGS) src/procfs.c -o procfs.o

# Remove generated files.
clean:
	rm -f *.o procsh
