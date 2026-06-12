/* Implements the psx built-in by reading process data from /proc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include "procfs.h"
#include "colors.h"

/* Returns true when a /proc directory name is only digits. */
static int procfs_is_pid_name(const char* name)
{
    if(name == NULL || name[0] == '\0')
    {
        return 0;
    }

    int i = 0;
    while(name[i] != '\0')
    {
        if(!isdigit((unsigned char)name[i]))
        {
            return 0;
        }

        i++;
    }

    return 1;
}

/* Removes the trailing newline from a line read from /proc. */
static void procfs_strip_newline(char* s)
{
    if(s == NULL)
    {
        return;
    }

    int i = 0;
    while(s[i] != '\0')
    {
        if(s[i] == '\n')
        {
            s[i] = '\0';
            return;
        }

        i++;
    }
}

/* Reads process name and state from /proc/<pid>/status. */
static int procfs_read_status(pid_t pid, ProcInfo* info)
{
    char path[128];
    char line[512];
    if(info == NULL)
    {
        return -1;
    }

    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);

    FILE* file = fopen(path, "r");
    if(file == NULL)
    {
        perror("procsh: fopen");
        return -1;
    }

    info->pid = pid;
    snprintf(info->name, sizeof(info->name), "%s", "?");
    snprintf(info->state, sizeof(info->state), "%s", "?");

    while(fgets(line, sizeof(line), file) != NULL)
    {
        if(strncmp(line, "Name:", 5) == 0)
        {
            sscanf(line, "Name:%255s", info->name);
        }
        else if(strncmp(line, "State:", 6) == 0)
        {
            procfs_strip_newline(line);
            snprintf(info->state, sizeof(info->state), "%s", line + 7);
        }
    }

    if(ferror(file))
    {
        perror("procsh: fgets");
        fclose(file);
        return -1;
    }

    if(fclose(file) == EOF)
    {
        perror("procsh: fclose");
        return -1;
    }

    return 0;
}

/* Reads and normalizes the command line from /proc/<pid>/cmdline. */
static int procfs_read_cmdline(pid_t pid, ProcInfo* info)
{
    char path[128];
    if(info == NULL)
    {
        return -1;
    }

    snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);

    FILE* file = fopen(path, "r");
    if(file == NULL)
    {
        snprintf(info->cmdline, sizeof(info->cmdline), "%s", info->name);
        return 0;
    }

    size_t bytes_read = fread(info->cmdline, 1, PROCFS_MAX_CMDLINE - 1, file);

    if(ferror(file))
    {
        perror("procsh: fread");
        fclose(file);
        return -1;
    }

    info->cmdline[bytes_read] = '\0';

    size_t i = 0;
    while(i < bytes_read)
    {
        if(info->cmdline[i] == '\0')
        {
            info->cmdline[i] = ' ';
        }

        i++;
    }

    if(bytes_read == 0)
    {
        snprintf(info->cmdline, sizeof(info->cmdline), "%s", info->name);
    }

    if(fclose(file) == EOF)
    {
        perror("procsh: fclose");
        return -1;
    }

    return 0;
}

/* Collects all process fields needed by psx. */
static int procfs_get_info(pid_t pid, ProcInfo* info)
{
    if(info == NULL)
    {
        return -1;
    }

    info->pid = pid;
    info->name[0] = '\0';
    info->state[0] = '\0';
    info->cmdline[0] = '\0';

    if(procfs_read_status(pid, info) == -1)
    {
        return -1;
    }

    if(procfs_read_cmdline(pid, info) == -1)
    {
        return -1;
    }

    return 0;
}

/* Prints the psx table header. */
static void procfs_print_header(void)
{
    printf(COLOR_BOLD COLOR_CYAN "%-8s %-18s %s" COLOR_RESET "\n", "PID", "STATE", "COMMAND");
}

/* Prints one psx table row. */
static void procfs_print_info(ProcInfo* info)
{
    if(info == NULL)
    {
        return;
    }

    printf(
        "%-8d %-18s %s\n",
        (int)info->pid,
        info->state,
        info->cmdline
    );
}

/* Prints /proc information for one PID. */
int procfs_print_pid(pid_t pid)
{
    ProcInfo info = {0};

    if(pid <= 0)
    {
        fprintf(stderr, COLOR_RED "procsh: psx: invalid pid" COLOR_RESET "\n");
        return 1;
    }

    if(procfs_get_info(pid, &info) == -1)
    {
        return 1;
    }

    procfs_print_header();
    procfs_print_info(&info);

    return 0;
}

/* Prints /proc information for every numeric process directory. */
int procfs_print_all(void)
{
    DIR* dir = opendir("/proc");
    if(dir == NULL)
    {
        perror("procsh: opendir");
        return 1;
    }

    procfs_print_header();

    /* /proc contains many non-process entries, so psx only accepts numeric names. */
    struct dirent* entry = NULL;
    while((entry = readdir(dir)) != NULL)
    {
        if(!procfs_is_pid_name(entry->d_name))
        {
            continue;
        }

        pid_t pid = (pid_t)atoi(entry->d_name);
        ProcInfo info = {0};

        if(procfs_get_info(pid, &info) == 0)
        {
            procfs_print_info(&info);
        }
    }

    if(closedir(dir) == -1)
    {
        perror("procsh: closedir");
        return 1;
    }

    return 0;
}