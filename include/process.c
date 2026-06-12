/* Tracks background jobs and reaps completed child processes. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "process.h"
#include "colors.h"

/* Resets one job slot so it can be reused. */
static void process_clear_job(Job* job)
{
    if(job == NULL)
    {
        return;
    }

    job->id = 0;
    job->pgid = -1;
    job->process_count = 0;
    job->active_processes = 0;
    job->command[0] = '\0';
    job->status = JOB_DONE;

    int i = 0;
    while(i < PROCESS_MAX_JOB_PROCESSES)
    {
        job->pids[i] = -1;
        i++;
    }
}

/* Initializes the background job table. */
void process_table_init(ProcessTable* table)
{
    if(table == NULL)
    {
        return;
    }

    table->next_job_id = 1;

    int i = 0;
    while(i < PROCESS_MAX_JOBS)
    {
        process_clear_job(&table->jobs[i]);
        i++;
    }
}

/* Converts a job status enum into display text. */
const char* process_job_status_string(JobStatus status)
{
    if(status == JOB_RUNNING)
    {
        return "running";
    }

    if(status == JOB_STOPPED)
    {
        return "stopped";
    }

    if(status == JOB_DONE)
    {
        return "done";
    }

    return "unknown";
}

/* Adds a background command or pipeline to the jobs table. */
int process_add_job(ProcessTable* table,pid_t pgid, pid_t* pids, int process_count, const char* command, JobStatus status)
{
    if(table == NULL || pids == NULL || command == NULL || process_count <= 0)
    {
        return -1;
    }

    if(process_count > PROCESS_MAX_JOB_PROCESSES)
    {
        fprintf(stderr, "procsh: jobs: pipeline is too large to track\n");
        return -1;
    }

    int i = 0;
    while(i < PROCESS_MAX_JOBS)
    {
        if(table->jobs[i].id == 0)
        {
            Job* job = &table->jobs[i];
            job->id = table->next_job_id;
            table->next_job_id++;
            job->pgid = pgid;
            job->status = status;
            job->process_count = process_count;
            job->active_processes = process_count;

            int j = 0;
            while(j < process_count)
            {
                job->pids[j] = pids[j];
                j++;
            }

            snprintf(job->command, PROCESS_MAX_COMMAND_TEXT, "%s", command);

            /* Ctrl-Z is echoed by the terminal without a newline. Print one
               before the job line so stopped jobs do not appear as ^Z[1]. */
            if(status == JOB_STOPPED)
            {
                printf("\n");
            }

            printf(COLOR_YELLOW "[%d] %d" COLOR_RESET "\n", job->id, (int)job->pgid);

            return job->id;
        }

        i++;
    }

    fprintf(stderr, "procsh: jobs: too many background jobs\n");
    return -1;
}

/* Removes a job by id. */
int process_remove_job(ProcessTable* table, int job_id)
{
    if(table == NULL)
    {
        return -1;
    }

    Job* job = process_find_job(table, job_id);
    if(job == NULL)
    {
        return -1;
    }

    process_clear_job(job);
    return 0;
}

/* Finds a job by shell job id. */
Job* process_find_job(ProcessTable* table, int job_id)
{
    if(table == NULL)
    {
        return NULL;
    }

    int i = 0;
    while(i < PROCESS_MAX_JOBS)
    {
        if(table->jobs[i].id == job_id)
        {
            return &table->jobs[i];
        }

        i++;
    }

    return NULL;
}

/* Finds a job by process group id. */
Job* process_find_job_by_pgid(ProcessTable* table, pid_t pgid)
{
    if(table == NULL)
    {
        return NULL;
    }

    int i = 0;
    while(i < PROCESS_MAX_JOBS)
    {
        if(table->jobs[i].id != 0 && table->jobs[i].pgid == pgid)
        {
            return &table->jobs[i];
        }

        i++;
    }

    return NULL;
}

/* Finds the job that owns a child process id. */
Job* process_find_job_by_pid(ProcessTable* table, pid_t pid)
{
    if(table == NULL)
    {
        return NULL;
    }

    int i = 0;
    while(i < PROCESS_MAX_JOBS)
    {
        if(table->jobs[i].id != 0)
        {
            int j = 0;
            while(j < table->jobs[i].process_count)
            {
                if(table->jobs[i].pids[j] == pid)
                {
                    return &table->jobs[i];
                }

                j++;
            }
        }

        i++;
    }

    return NULL;
}

/* Updates a tracked job state when the process group changes state. */
void process_update_job_status(ProcessTable* table, pid_t pgid, JobStatus status)
{
    if(table == NULL)
    {
        return;
    }

    Job* job = process_find_job_by_pgid(table, pgid);
    if(job == NULL)
    {
        return;
    }

    job->status = status;
}

/* Prints visible jobs after first reaping completed children. */
void process_print_jobs(ProcessTable* table)
{
    if(table == NULL)
    {
        return;
    }

    process_reap_background_jobs(table);

    int i = 0;
    while(i < PROCESS_MAX_JOBS)
    {
        Job* job = &table->jobs[i];

        if(job->id != 0)
        {
            printf(COLOR_YELLOW "[%d]" COLOR_RESET " %-8s %d  %s\n", job->id, process_job_status_string(job->status), (int)job->pgid, job->command);
        }
        i++;
    }
}

/* Non-blockingly reaps background children and clears jobs when all children finish. */
void process_reap_background_jobs(ProcessTable* table)
{
    if(table == NULL)
    {
        return;
    }

    while(1)
    {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);

        if(pid > 0)
        {
            Job* job = process_find_job_by_pid(table, pid);

            if(job != NULL)
            {
                /* A pipeline job is done only after every tracked child has finished. */
                if(WIFEXITED(status) || WIFSIGNALED(status))
                {
                    job->active_processes--;

                    if(job->active_processes <= 0)
                    {
                        printf(COLOR_YELLOW "[%d] done     %d  %s" COLOR_RESET "\n", job->id, (int)job->pgid, job->command);
                        process_clear_job(job);
                    }
                }
                else if(WIFSTOPPED(status))
                {
                    job->status = JOB_STOPPED;
                }
                else if(WIFCONTINUED(status))
                {
                    job->status = JOB_RUNNING;
                }
            }

            continue;
        }

        if(pid == 0)
        {
            break;
        }

        if(pid == -1)
        {
            if(errno == ECHILD)
            {
                break;
            }

            perror("procsh: waitpid");
            break;
        }
    }
}
