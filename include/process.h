/* Background job tracking structures and operations. */
#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

#define PROCESS_MAX_JOBS 128
#define PROCESS_MAX_COMMAND_TEXT 1024
#define PROCESS_MAX_JOB_PROCESSES 64

typedef enum JobStatus {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobStatus;

typedef struct Job {
    int id;
    pid_t pgid;
    pid_t pids[PROCESS_MAX_JOB_PROCESSES];
    int process_count;
    int active_processes;
    char command[PROCESS_MAX_COMMAND_TEXT];
    JobStatus status;
} Job;

typedef struct ProcessTable {
    Job jobs[PROCESS_MAX_JOBS];
    int next_job_id;
} ProcessTable;

/* Initialize the background job table. */
void process_table_init(ProcessTable* table);

/* Add a background job or pipeline. */
int process_add_job(
    ProcessTable* table,
    pid_t pgid,
    pid_t* pids,
    int process_count,
    const char* command,
    JobStatus status
);
/* Remove one job by id. */
int process_remove_job(ProcessTable* table, int job_id);

/* Find a job by id. */
Job* process_find_job(ProcessTable* table, int job_id);
/* Find a job by process group id. */
Job* process_find_job_by_pgid(ProcessTable* table, pid_t pgid);
/* Find a job containing a child pid. */
Job* process_find_job_by_pid(ProcessTable* table, pid_t pid);

/* Update a job status. */
void process_update_job_status(ProcessTable* table, pid_t pgid, JobStatus status);
/* Print all tracked jobs. */
void process_print_jobs(ProcessTable* table);
/* Reap finished background children without blocking. */
void process_reap_background_jobs(ProcessTable* table);

/* Convert a job status to display text. */
const char* process_job_status_string(JobStatus status);

#endif
