# ProcShell

ProcShell is a Unix-like shell written in C. It can run normal Linux commands and supports built-in commands, pipelines, redirection, background jobs, command history, signals, trace output, and process inspection through `/proc`.

## Requirements

ProcShell is meant to run on Ubuntu or WSL Ubuntu.

You need:

- `gcc`
- `make`
- GNU Readline development headers

Install everything with:

```bash
sudo apt update
sudo apt install build-essential make libreadline-dev
```

## Build and Run

Go into the ProcShell project folder:

```bash
cd ProcShell
```

Build the project:

```bash
make clean
make
```

Run ProcShell:

```bash
./procsh
```

Clean the project:

```bash
make clean
```

## Built-in Commands

```text
cd [dir]        change the current directory
pwd             print the current working directory
exit            exit ProcShell
help            show the help message
jobs            list running and stopped jobs
fg %job         resume a job in the foreground
bg %job         resume a stopped job in the background
trace on        enable trace output
trace off       disable trace output
trace status    show whether trace mode is enabled
psx             show process information from /proc
psx [pid]       show process information for one process
```

## Keyboard Shortcuts

```text
Up Arrow        go to the previous command in history
Down Arrow      go forward in command history
Ctrl-C          stop the current foreground command
Ctrl-Z          stop the current foreground command and place it in the jobs list
```

## Supported Shell Features

```text
command execution        ls, cat, grep, sleep, etc.
pipelines                cat file.txt | grep hello | wc -l
input redirection        cat < input.txt
output redirection       echo hello > out.txt
append redirection       echo again >> out.txt
error redirection        badcommand 2> err.txt
background jobs          sleep 10 &
quoted strings           echo "hello world"
escaped spaces           echo hello\ world
```

## Example

```bash
make clean
make
./procsh
```

Inside ProcShell:

```bash
echo hello > out.txt
cat out.txt
sleep 10 &
jobs
trace on
echo hello | wc -c
trace off
exit
```
