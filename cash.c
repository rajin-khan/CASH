#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // Core POSIX functions: fork, exec, pipe, chdir, access, ids, tty control
#include <sys/types.h>  // Basic system data types like pid_t
#include <sys/wait.h>   // For waitpid() and associated macros
#include <fcntl.h>      // For file control options used in open()
#include <signal.h>     // For signal handling (signal(), kill())
#include <errno.h>      // Defines errno and error constants
#include <termios.h>    // For terminal I/O control (tcsetpgrp)
#include <limits.h>     // Defines PATH_MAX

// --- Readline Headers ---
#include <readline/readline.h> // For reading input with editing/history
#include <readline/history.h>  // For history management functions

// --- Macros ---
#define MAX_INPUT 1024 // Max length of user input string
#define MAX_ARGS 100   // Max number of arguments per command
#define MAX_JOBS 32    // Max number of background jobs tracked
#define READ_END 0     // Index for the read end of a pipe fd array
#define WRITE_END 1    // Index for the write end of a pipe fd array

// --- History File ---
#define HISTORY_FILE ".cash_history" // History file name in user's home directory

// --- Job State Definitions ---
typedef enum {
    JOB_STATE_INVALID, // Indicates the job slot is empty
    JOB_STATE_RUNNING, // Job is currently running
    JOB_STATE_STOPPED, // Job is stopped (e.g., by SIGTSTP)
} job_state_t;

// --- Job Structure ---
// Holds information about a background job
typedef struct {
    int jid;           // Job ID (unique within the shell session)
    pid_t pgid;        // Process Group ID of the job
    job_state_t state; // Current state (Running, Stopped)
    char *command;     // The command string that started the job (allocated)
    int notified;      // Tracks if status change (Done/Stopped) was reported
} job_t;

// --- Global Job List and Shell Info ---
job_t job_list[MAX_JOBS];      // Array to store background job information
int next_jid = 1;              // Counter for assigning the next job ID
pid_t cash_pgid;               // Shell's own process group ID
int terminal_fd = STDIN_FILENO; // FD for the controlling terminal (usually stdin)
int shell_is_interactive;      // Set to 1 if running interactively, 0 otherwise

// --- Function Prototypes ---
// Core Shell Logic
void display_welcome_message();
int parse_command(char *command_str, char **args, char **inputFile, char **outputFile);
void execute_pipeline(char *input, const char *original_cmd_for_job);
void execute_single_command(char **args, int background, char *inputFile, char *outputFile, const char *original_cmd);
void handle_child_execution(char **args, char *inputFile, char *outputFile);
void handle_sigchld(int sig);

// Job Management
void init_jobs();
int add_job(pid_t pgid, const char* cmd, job_state_t state);
int remove_job_by_pgid(pid_t pgid);
job_t* get_job_by_jid(int jid);
job_t* get_job_by_pgid(pid_t pgid);
void list_jobs();
void wait_for_job(job_t *job);
void put_job_in_foreground(job_t *job, int cont);
void put_job_in_background(job_t *job, int cont);
void check_jobs_status();

// History file utility
char* get_history_filepath();


// --- Job Management Functions ---

/**
 * @brief Initialize the job list at shell startup.
 */
void init_jobs() {
    for (int i = 0; i < MAX_JOBS; i++) {
        job_list[i].state = JOB_STATE_INVALID; // Mark all slots as free
        job_list[i].command = NULL;
    }
    next_jid = 1; // Reset job ID counter
}

/**
 * @brief Find an empty slot in the job list.
 * @return Index of a free slot, or -1 if full.
 */
int find_free_job_slot() {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].state == JOB_STATE_INVALID) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Add a new job to the list. Duplicates the command string.
 * @param pgid Process group ID of the job.
 * @param cmd Command string (will be copied).
 * @param state Initial job state (Running/Stopped).
 * @return The new Job ID (jid) or -1 on failure.
 */
int add_job(pid_t pgid, const char* cmd, job_state_t state) {
    if (pgid <= 0) return -1;

    int slot = find_free_job_slot();
    if (slot == -1) {
        fprintf(stderr, "ca$h: Maximum jobs limit (%d) reached.\n", MAX_JOBS);
        return -1;
    }

    job_list[slot].jid = next_jid++;
    job_list[slot].pgid = pgid;
    job_list[slot].state = state;
    // OS Concept: Memory Allocation - Must free this later
    job_list[slot].command = strdup(cmd);
    job_list[slot].notified = (state == JOB_STATE_RUNNING); // Don't notify immediately for running

    if (job_list[slot].command == NULL) {
        fprintf(stderr, "ca$h: Failed memory allocation for job command.\n");
        job_list[slot].state = JOB_STATE_INVALID;
        return -1;
    }
    return job_list[slot].jid;
}

/**
 * @brief Find the job list index associated with a Job ID.
 * @param jid The Job ID.
 * @return Index in job_list, or -1 if not found.
 */
int find_job_slot_by_jid(int jid) {
     for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].state != JOB_STATE_INVALID && job_list[i].jid == jid) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Find the job list index associated with a Process Group ID.
 * @param pgid The Process Group ID.
 * @return Index in job_list, or -1 if not found.
 */
int find_job_slot_by_pgid(pid_t pgid) {
     for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].state != JOB_STATE_INVALID && job_list[i].pgid == pgid) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Remove a job from the list using its PGID. Frees the command string memory.
 * @param pgid PGID of the job to remove.
 * @return 1 if removed, 0 if not found.
 */
int remove_job_by_pgid(pid_t pgid) {
     int slot = find_job_slot_by_pgid(pgid);
    if (slot == -1) return 0;

    if (job_list[slot].command) {
        // OS Concept: Memory Management - Freeing allocated memory
        free(job_list[slot].command);
        job_list[slot].command = NULL;
    }
    job_list[slot].state = JOB_STATE_INVALID; // Mark slot as free
    job_list[slot].jid = 0;
    job_list[slot].pgid = 0;
    job_list[slot].notified = 0;
    return 1;
}

/**
 * @brief Get a pointer to a job using its Job ID.
 * @param jid Job ID.
 * @return Pointer to job_t struct, or NULL if not found.
 */
job_t* get_job_by_jid(int jid) {
    int slot = find_job_slot_by_jid(jid);
    return (slot != -1) ? &job_list[slot] : NULL;
}

/**
 * @brief Get a pointer to a job using its Process Group ID.
 * @param pgid Process Group ID.
 * @return Pointer to job_t struct, or NULL if not found.
 */
job_t* get_job_by_pgid(pid_t pgid) {
     int slot = find_job_slot_by_pgid(pgid);
     return (slot != -1) ? &job_list[slot] : NULL;
}

/**
 * @brief Implements the 'jobs' built-in. Displays running/stopped background jobs.
 */
void list_jobs() {
    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].state != JOB_STATE_INVALID) {
            found = 1;
            const char *state_str = (job_list[i].state == JOB_STATE_RUNNING) ? "Running" : "Stopped";
            printf("[%d] %d %s\t%s\n",
                   job_list[i].jid,
                   job_list[i].pgid,
                   state_str,
                   job_list[i].command);
        }
    }
     if (!found && shell_is_interactive) {
          printf("No active jobs.\n");
     }
}

/**
 * @brief Checks for status changes in background jobs and prints notifications ("Done", "Stopped").
 * Called before showing the prompt. Relies on SIGCHLD handler having updated job states.
 */
void check_jobs_status() {
    int status_changed = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
         // Check jobs that were marked as finished by the SIGCHLD handler
         if (job_list[i].state == JOB_STATE_INVALID && job_list[i].command != NULL && !job_list[i].notified) {
             if (!status_changed) printf("\n");
             printf("[%d] Done\t%s\n", job_list[i].jid, job_list[i].command);
             free(job_list[i].command); // Clean up the command string
             job_list[i].command = NULL;
             job_list[i].notified = 1; // Should already be removed, but be sure
             job_list[i].jid = 0;
             job_list[i].pgid = 0;
             status_changed = 1;
         }
         // Check jobs marked as stopped by SIGCHLD handler or wait_for_job
         else if (job_list[i].state == JOB_STATE_STOPPED && !job_list[i].notified) {
              if (!status_changed) printf("\n");
              printf("[%d] Stopped\t%s\n", job_list[i].jid, job_list[i].command);
              job_list[i].notified = 1; // Mark as notified for this stop
              status_changed = 1;
         }
    }
}

/**
 * @brief Waits for a specific job (identified by job struct) to change state (stop or terminate).
 * This is used when bringing a job to the foreground (`fg`).
 * @param job Pointer to the job to wait for.
 */
void wait_for_job(job_t *job) {
    if (!job || job->pgid <= 0 || job->state == JOB_STATE_INVALID) return;

    int status = 0;
    pid_t result;

    // OS Concept: Waiting for Process Group - Use waitpid with negative PGID.
    // WUNTRACED: Report status if child stops (e.g., via SIGTSTP).
    do {
         result = waitpid(-(job->pgid), &status, WUNTRACED);
         // Loop handles EINTR (interruption by signals like SIGCHLD)
    } while (result < 0 && errno == EINTR);

    // OS Concept: Terminal Control - Give terminal control back to the shell.
    if (shell_is_interactive) {
        if (tcgetpgrp(terminal_fd) != cash_pgid) {
            tcsetpgrp(terminal_fd, cash_pgid);
        }
    }

    // Update job status based on why waitpid returned
    if (result > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            remove_job_by_pgid(job->pgid); // Job finished
        } else if (WIFSTOPPED(status)) {
            job->state = JOB_STATE_STOPPED; // Job stopped
            job->notified = 0; // Will be notified by check_jobs_status
        }
    } else if (result < 0) { // Error (ECHILD means already gone)
        if (errno != ECHILD) { perror("ca$h: waitpid error in wait_for_job"); }
        remove_job_by_pgid(job->pgid); // Clean up job entry if wait failed
    }
}

/**
 * @brief Bring a background job to the foreground.
 * @param job The job to bring to the foreground.
 * @param cont 1 if the job should be sent SIGCONT (if stopped), 0 otherwise.
 */
void put_job_in_foreground(job_t *job, int cont) {
    if (!job || !shell_is_interactive || job->state == JOB_STATE_INVALID) return;

    job->state = JOB_STATE_RUNNING; // Assume it will be running
    job->notified = 1; // Don't need immediate notification

    // OS Concept: Terminal Control - Give terminal to the job's group.
    if (tcsetpgrp(terminal_fd, job->pgid) == -1) {
         perror("ca$h: tcsetpgrp error in foreground");
    }

    // OS Concept: Sending Signals - Send SIGCONT if job was stopped.
    if (cont) {
        if (kill(-(job->pgid), SIGCONT) < 0) {
            if (errno != ESRCH) { perror("ca$h: kill (SIGCONT) error in foreground"); }
        }
    }

    // Wait for this job to finish or stop again
    wait_for_job(job);
}

/**
 * @brief Resume a stopped job in the background.
 * @param job The job to resume.
 * @param cont 1 if SIGCONT should be sent (usually yes).
 */
void put_job_in_background(job_t *job, int cont) {
     if (!job || job->state == JOB_STATE_INVALID) return;

     if (job->state == JOB_STATE_RUNNING) {
         printf("ca$h: bg: job %d already running.\n", job->jid);
         return;
     }

     job->state = JOB_STATE_RUNNING;
     job->notified = 1; // Don't need immediate notification

     if (cont) {
         // OS Concept: Sending Signals - Resume the stopped job group.
         if (kill(-(job->pgid), SIGCONT) < 0) {
             perror("ca$h: kill (SIGCONT) error in background");
             job->state = JOB_STATE_STOPPED; // Revert state if signal failed
         }
     }
}

// --- Signal Handler ---

/**
 * @brief Handles SIGCHLD signal (sent when a child process stops or terminates).
 * Updates the status of jobs in the job list asynchronously.
 * @param sig The signal number received (unused).
 */
void handle_sigchld(int sig) {
    int saved_errno = errno; // Preserve errno around async signal handling
    pid_t pid;
    int status;

    // OS Concept: Non-blocking Wait - Check for any child status change without pausing.
    // WUNTRACED: Also detect stopped children.
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        // Find the job associated with the process group of the changed child
        pid_t job_pgid = getpgid(pid); // Note: May fail if pid exited instantly
        job_t *job = NULL;

        if (job_pgid > 0) {
             job = get_job_by_pgid(job_pgid);
        }

        if (job) { // If it's a tracked background job
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                 // Job terminated - mark for removal and notification later
                 job->state = JOB_STATE_INVALID;
                 job->notified = 0;
            } else if (WIFSTOPPED(status)) {
                 // Job stopped - update state if not already marked
                 if (job->state != JOB_STATE_STOPPED || !job->notified) {
                     job->state = JOB_STATE_STOPPED;
                     job->notified = 0;
                 }
            }
        }
        // If job is NULL, it might be a foreground process that we handle elsewhere,
        // or a process that terminated very quickly.
    }

    // ECHILD means no children left, which is fine. Other errors are unexpected.
    if (pid < 0 && errno != ECHILD) {
       // Avoid stdio in handlers: perror("ca$h: waitpid error in SIGCHLD handler");
    }

    errno = saved_errno; // Restore errno
}

// --- History File Path Helper ---
/**
 * @brief Construct the full path for the history file (~/.cash_history).
 * @return Allocated string with the path (must be freed), or NULL.
 */
char* get_history_filepath() {
    // OS Concept: Environment Variables - Accessing user's home directory.
    char *home_dir = getenv("HOME");
    if (!home_dir) {
        fprintf(stderr, "ca$h: Cannot find HOME directory for history file.\n");
        return NULL;
    }
    // OS Concept: Memory Allocation - Allocate space for the path string.
    char *filepath = malloc(PATH_MAX);
    if (!filepath) {
        perror("ca$h: malloc failed for history path");
        return NULL;
    }
    // OS Concept: File Path Construction - Create path string.
    snprintf(filepath, PATH_MAX, "%s/%s", home_dir, HISTORY_FILE);
    return filepath;
}


// --- Main Function ---
int main() {
    char input_buffer[MAX_INPUT]; // Mutable buffer for parsing command line
    char original_input_for_job[MAX_INPUT]; // For storing job titles cleanly
    char *line_read = NULL; // Buffer allocated by readline()
    char *history_filepath = NULL;

    init_jobs(); // Initialize job control structures

    // --- Shell Initialization ---
    terminal_fd = STDIN_FILENO;
    // OS Concept: TTY Detection - Is the shell connected to a terminal?
    shell_is_interactive = isatty(terminal_fd);

    if (shell_is_interactive) {
        // OS Concept: Process Groups & Terminal Control - Take control of the terminal.
        // Loop until shell is in the foreground process group
        while (tcgetpgrp(terminal_fd) != (cash_pgid = getpgrp())) {
            kill(-cash_pgid, SIGKILL); // Safety kill if started in background
        }
        // Set shell's process group as the terminal's foreground group
        cash_pgid = getpgrp();
        if (tcsetpgrp(terminal_fd, cash_pgid) == -1) {
            perror("ca$h: Couldn't grab control of terminal");
            shell_is_interactive = 0; // Fallback to non-interactive?
        }

        // OS Concept: Signal Handling - Shell ignores job control/interrupt signals.
        signal(SIGINT, SIG_IGN);  // Ctrl+C
        signal(SIGQUIT, SIG_IGN); // Ctrl+backslash
        signal(SIGTSTP, SIG_IGN); // Ctrl+Z
        signal(SIGTTIN, SIG_IGN); // Background read attempt
        signal(SIGTTOU, SIG_IGN); // Background write attempt
        signal(SIGCHLD, handle_sigchld); // Handle child status changes

        // Initialize command history
        history_filepath = get_history_filepath();
        if (history_filepath) {
            // OS Concept: File I/O - Reading history from file.
            read_history(history_filepath);
            stifle_history(1000); // Limit history size (optional)
        }
    } else {
         printf("ca$h: Warning: Not running interactively. Job control/History disabled.\n");
    }

    display_welcome_message();

    // --- Main Shell Loop ---
    while(1) {
        if (shell_is_interactive) {
             check_jobs_status(); // Report background job status changes
        }

        // Free memory from previous readline call
        if (line_read) {
            // OS Concept: Memory Management - Freeing readline's buffer.
            free(line_read);
            line_read = NULL;
        }

        // OS Concept: Input Reading - Use readline for interactive input.
        line_read = readline("ca$h> ");

        // Handle EOF (Ctrl+D) or readline error
        if (line_read == NULL) {
            printf("\nClosing ca$h...\n");
            break;
        }

        // Skip empty input lines (just Enter or whitespace)
        char *trimmed_line = line_read + strspn(line_read, " \t\n\r");
        if (*trimmed_line == '\0') {
            continue;
        }

        // Add non-empty line to history (if interactive)
        if (shell_is_interactive && line_read && *line_read) {
             add_history(line_read);
        }

        // Prepare command for execution
        // 1. Store original (cleaned) command for job list title
        strncpy(original_input_for_job, line_read, MAX_INPUT - 1);
        original_input_for_job[MAX_INPUT - 1] = '\0';
        char *end_job_title = original_input_for_job + strlen(original_input_for_job) - 1;
        while (end_job_title >= original_input_for_job && strchr(" \t\n\r&", *end_job_title)) { *end_job_title-- = '\0'; }

        // 2. Copy to mutable buffer for parsing (strtok modifies)
        strncpy(input_buffer, line_read, MAX_INPUT - 1);
        input_buffer[MAX_INPUT - 1] = '\0';

        // Execute the command line (handles pipes, jobs, etc.)
        execute_pipeline(input_buffer, original_input_for_job);
    }

    // --- Shell Exit ---
    if (line_read) { free(line_read); } // Free last input line

    // Save history to file
    if (shell_is_interactive && history_filepath) {
        // OS Concept: File I/O - Writing history to file.
        write_history(history_filepath);
        free(history_filepath); // Free the path string
    }

    // Clean up any remaining job command strings
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].command != NULL) { free(job_list[i].command); }
    }

    printf("ca$h closed.\n");
    return 0;
}


// --- Core Shell Helper Functions ---

/**
 * @brief Display the shell's welcome banner.
 */
void display_welcome_message() {
    printf("\n");
    printf(" ██████╗ █████╗ ███████╗██╗  ██╗\n");
    printf("██╔════╝██╔══██╗██╔════╝██║  ██║\n");
    printf("██║     ███████║███████╗███████║\n");
    printf("██║     ██╔══██║╚════██║██╔══██║\n");
    printf("╚██████╗██║  ██║███████║██║  ██║\n");
    printf(" ╚═════╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝\n");
    printf("\n");
    printf("Welcome to ca$h - An Educational Command Shell!\n");
    printf("Demonstrates: Process Creation (fork), Program Execution (execvp),\n");
    printf("Waiting (waitpid), Background Jobs (&), File I/O Redirection (<, >),\n");
    printf("IPC via Pipes (|), Signal Handling (SIGCHLD), Job Control (jobs, fg, bg),\n");
    printf("History & Editing (readline)\n");
    printf("Type 'exit' to quit.\n\n");
}

/**
 * @brief Parse a command string into arguments, handling simple redirection.
 * Modifies the input command_str using strtok.
 * @param command_str The command string to parse (will be modified).
 * @param args Output array for arguments.
 * @param inputFile Output pointer for input redirection filename.
 * @param outputFile Output pointer for output redirection filename.
 * @return 1 if a command was found, 0 on syntax error or empty command.
 */
int parse_command(char *command_str, char **args, char **inputFile, char **outputFile) {
    int i = 0;
    *inputFile = NULL;
    *outputFile = NULL;
    args[0] = NULL; // Initialize to NULL

    // Check for empty string after initial whitespace
    char* trimmed_cmd = command_str + strspn(command_str, " \t\n\r\a");
    if (*trimmed_cmd == '\0') { return 0; }

    // OS Concept: String Tokenization - Breaking input into words.
    char *token = strtok(command_str, " \t\n\r\a");
    while (token != NULL && i < MAX_ARGS - 1) {
        // OS Concept: Shell Syntax Parsing - Recognizing special characters.
        if (strcmp(token, "<") == 0) { // Input redirection
            token = strtok(NULL, " \t\n\r\a");
            if (token == NULL || strchr("<>|&", token[0])) { // Basic check for invalid filename
                fprintf(stderr, "ca$h: syntax error near redirection `<'\n"); args[0] = NULL; return 0;
            }
            *inputFile = token;
        } else if (strcmp(token, ">") == 0) { // Output redirection
            token = strtok(NULL, " \t\n\r\a");
            if (token == NULL || strchr("<>|&", token[0])) { // Basic check
                fprintf(stderr, "ca$h: syntax error near redirection `>'\n"); args[0] = NULL; return 0;
            }
            *outputFile = token;
        } else { // Normal argument
            args[i++] = token;
        }
        token = strtok(NULL, " \t\n\r\a"); // Get next token
    }
    args[i] = NULL; // Null-terminate argument list for execvp

    // Check for redirection without a command word
    if (args[0] == NULL && (*inputFile != NULL || *outputFile != NULL)) {
        fprintf(stderr, "ca$h: syntax error: redirection without command\n"); return 0;
    }

    return (args[0] != NULL); // Success if a command word was found
}

/**
 * @brief Code executed only by the child process after fork.
 * Sets up redirection, resets signal handlers, and executes the command.
 * Does not return if execvp succeeds.
 * @param args Command and arguments array.
 * @param inputFile Filename for input redirection (or NULL).
 * @param outputFile Filename for output redirection (or NULL).
 */
void handle_child_execution(char **args, char *inputFile, char *outputFile) {
    // OS Concept: Signal Handling - Child resets ignored signals to default behavior.
    signal(SIGINT, SIG_DFL); signal(SIGQUIT, SIG_DFL); signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL); signal(SIGTTOU, SIG_DFL); signal(SIGCHLD, SIG_DFL);

    int fd_in = -1, fd_out = -1;
    // OS Concept: File I/O & File Descriptors - Open files for redirection.
    if (inputFile != NULL) {
        fd_in = open(inputFile, O_RDONLY);
        if (fd_in < 0) { perror("ca$h: Failed to open input file"); exit(EXIT_FAILURE); }
        // OS Concept: File Descriptor Manipulation - Redirect stdin (fd 0).
        if (dup2(fd_in, STDIN_FILENO) < 0) { perror("ca$h: Failed input redirection (dup2)"); close(fd_in); exit(EXIT_FAILURE); }
        close(fd_in); // Close original fd
    }
    if (outputFile != NULL) {
        // Create/overwrite file for output, with standard permissions (rw-r--r--)
        fd_out = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) { perror("ca$h: Failed to open output file"); exit(EXIT_FAILURE); }
        // OS Concept: File Descriptor Manipulation - Redirect stdout (fd 1).
        if (dup2(fd_out, STDOUT_FILENO) < 0) { perror("ca$h: Failed output redirection (dup2)"); close(fd_out); exit(EXIT_FAILURE); }
        close(fd_out); // Close original fd
    }

    // OS Concept: Program Execution - Replace child process with the new command.
    if (execvp(args[0], args) == -1) {
        // execvp only returns on error
        fprintf(stderr, "ca$h: Command not found or execution failed: %s\n", args[0]);
        exit(EXIT_FAILURE); // Child MUST exit if exec fails
    }
}

/**
 * @brief Executes a single command (part of execute_pipeline logic).
 * Handles built-ins and external commands (fork, exec, job setup).
 * @param args Command and arguments.
 * @param background 1 if job should run in background, 0 for foreground.
 * @param inputFile Input redirection file (or NULL).
 * @param outputFile Output redirection file (or NULL).
 * @param original_cmd The original command string (for job title).
 */
void execute_single_command(char **args, int background, char *inputFile, char *outputFile, const char *original_cmd) {
    if (args[0] == NULL) return; // Safety check

    // --- Handle Built-in Commands ---
    // These modify the shell's state directly, no fork needed.
    if (strcmp(args[0], "exit") == 0) { exit(0); } // Exit handled in main loop for history saving
    if (strcmp(args[0], "cd") == 0) {
        // OS Concept: Process State - Change shell's current working directory.
        if (inputFile || outputFile) { fprintf(stderr, "ca$h: warning: redirection does not apply to built-in 'cd'\n"); }
        const char *dir = args[1];
        if (dir == NULL) { dir = getenv("HOME"); if (!dir) {fprintf(stderr, "ca$h: cd: HOME not set\n"); return;} }
        else if (args[2] != NULL) { fprintf(stderr, "ca$h: cd: too many arguments\n"); return; }
        if (chdir(dir) != 0) { perror("ca$h: cd failed"); } // chdir system call
        return;
    }
     if (strcmp(args[0], "clear") == 0) {
         if (inputFile || outputFile) { fprintf(stderr, "ca$h: warning: redirection does not apply to 'clear'\n"); }
         system("clear"); // Forks a subshell to run /usr/bin/clear
         return;
     }
    // Job Control Built-ins
    if (strcmp(args[0], "jobs") == 0) { if (shell_is_interactive) check_jobs_status(); list_jobs(); return; }
    if (strcmp(args[0], "fg") == 0) {
         if (!shell_is_interactive) { fprintf(stderr, "ca$h: fg: No job control.\n"); return; }
         if (args[1] == NULL || args[1][0] != '%') { fprintf(stderr, "ca$h: fg: Usage: fg %%<job_id>\n"); return; }
         int jid = atoi(&args[1][1]); if (jid <= 0) { fprintf(stderr, "ca$h: fg: Invalid job ID: %s\n", args[1]); return; }
         job_t *job = get_job_by_jid(jid); if (!job) { fprintf(stderr, "ca$h: fg: No such job: %d\n", jid); return; }
         printf("%s\n", job->command);
         put_job_in_foreground(job, job->state == JOB_STATE_STOPPED);
         return;
    }
     if (strcmp(args[0], "bg") == 0) {
         if (!shell_is_interactive) { fprintf(stderr, "ca$h: bg: No job control.\n"); return; }
         if (args[1] == NULL || args[1][0] != '%') { fprintf(stderr, "ca$h: bg: Usage: bg %%<job_id>\n"); return; }
         int jid = atoi(&args[1][1]); if (jid <= 0) { fprintf(stderr, "ca$h: bg: Invalid job ID: %s\n", args[1]); return; }
         job_t *job = get_job_by_jid(jid); if (!job) { fprintf(stderr, "ca$h: bg: No such job: %d\n", jid); return; }
         printf("[%d] %s &\n", job->jid, job->command);
         put_job_in_background(job, 1);
         return;
    }

    // --- Handle External Commands ---
    // OS Concept: Process Creation - Create a child process.
    pid_t pid = fork();
    if (pid < 0) { perror("ca$h: Fork failed"); return; }

    if (pid == 0) { // Child Process
        // OS Concept: Process Groups - Child joins/creates its own group for job control.
        if (shell_is_interactive) {
             if (setpgid(0, 0) < 0) { perror("ca$h: child setpgid failed"); exit(EXIT_FAILURE); }
        }
        handle_child_execution(args, inputFile, outputFile); // Sets up IO, signals, then execs
    } else { // Parent Process
        pid_t child_pgid = pid; // Assume child becomes group leader with its own PID
        if (shell_is_interactive) {
            // OS Concept: Process Group Management - Parent sets child's PGID (carefully handles races)
            if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH) {
                perror("ca$h: parent setpgid failed"); // Problem if parent couldn't set it
                 child_pgid = getpgid(pid); // Try to recover PGID child might have set
                 if (child_pgid < 0) { perror("ca$h: parent getpgid failed"); return; }
            } else if (errno == EACCES || errno == ESRCH){ // Child set it first or exited
                 child_pgid = getpgid(pid); // Try to get it
                 if (child_pgid < 0 && errno == ESRCH) { child_pgid = pid; } // Exited: use PID for wait
                 else if (child_pgid < 0) { perror("ca$h: parent getpgid failed"); return; }
            } else { child_pgid = pid; } // Success or harmless error
        }

        if (background) { // Background job
             if (shell_is_interactive && child_pgid > 0) {
                 // OS Concept: Job Tracking - Add job to the shell's list.
                 int jid = add_job(child_pgid, original_cmd, JOB_STATE_RUNNING);
                 if (jid > 0) { printf("[%d] %d\n", jid, child_pgid); } // Print job info
             }
             // Parent does NOT wait for background jobs. SIGCHLD handler cleans up.
        } else { // Foreground job
             if (shell_is_interactive && child_pgid > 0) {
                 // OS Concept: Foreground Job Management - Use temporary struct for waiting logic.
                 char *cmd_copy = strdup(original_cmd ? original_cmd : "");
                 if (!cmd_copy) {perror("ca$h: strdup failed for fg job"); return;}
                 job_t fg_job = { .jid = 0, .pgid = child_pgid, .state = JOB_STATE_RUNNING, .command = cmd_copy };
                 put_job_in_foreground(&fg_job, 0); // Gives terminal control and waits
                 free(cmd_copy); // Free the temporary copy
             } else {
                 // Non-interactive shell: Simple blocking wait
                 waitpid(pid, NULL, 0);
             }
        }
    }
}


/**
 * @brief Execute a command line, handling pipes and background execution.
 * @param input The command line string (will be modified by parsing).
 * @param original_cmd_for_job The original, unmodified command string for job titles.
 */
void execute_pipeline(char *input, const char *original_cmd_for_job) {
    char *args1[MAX_ARGS], *args2[MAX_ARGS];
    char *inputFile1 = NULL, *outputFile1 = NULL;
    char *inputFile2 = NULL, *outputFile2 = NULL;
    int background = 0;
    char *pipe_pos = NULL;
    char *cmd1_str = NULL;
    char *cmd2_str = NULL;
    pid_t pipeline_pgid = 0; // PGID for the entire pipeline

    // Check for & at the end for background execution
    char *end = input + strlen(input) - 1;
    while (end >= input && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) { *end-- = '\0'; } // Trim trailing whitespace
    if (end >= input && *end == '&') { background = 1; *end-- = '\0'; // Found &
         while (end >= input && (*end == ' ' || *end == '\t')) { *end-- = '\0'; } // Trim space before &
    }
    if (input[strspn(input, " \t\n\r")] == '\0') { return; } // Ignore empty line after trimming &

    // Check if first command is a built-in that cannot be piped
    char temp_input_check[MAX_INPUT]; strncpy(temp_input_check, input, MAX_INPUT - 1); temp_input_check[MAX_INPUT - 1] = '\0';
    char *first_cmd_token = strtok(temp_input_check, " \t\n\r\a|");
    int is_builtin = 0;
    if (first_cmd_token) {
        if (strcmp(first_cmd_token, "jobs") == 0 || strcmp(first_cmd_token, "fg") == 0 ||
            strcmp(first_cmd_token, "bg") == 0 || strcmp(first_cmd_token, "exit") == 0 ||
            strcmp(first_cmd_token, "cd") == 0 || strcmp(first_cmd_token, "clear") == 0 ) {
            is_builtin = 1;
        }
    }

    // Find pipe symbol
    pipe_pos = strchr(input, '|');

    // If it's a known built-in AND there's no pipe, handle as single command
    if (is_builtin && pipe_pos == NULL) {
        // Need to parse the original input again as strtok modified temp_input_check
        if (!parse_command(input, args1, &inputFile1, &outputFile1)) return;
        if (args1[0] == NULL) return;
        execute_single_command(args1, background, inputFile1, outputFile1, original_cmd_for_job);
        return;
    }

    if (pipe_pos == NULL) {
        // --- No Pipe ---
        cmd1_str = input;
        if (!parse_command(cmd1_str, args1, &inputFile1, &outputFile1)) return;
        if (args1[0] == NULL) return;
        execute_single_command(args1, background, inputFile1, outputFile1, original_cmd_for_job);

    } else {
        // --- Pipe Found ---
        if (is_builtin) { fprintf(stderr, "ca$h: Error: Builtin command '%s' cannot be piped.\n", first_cmd_token); return; }

        *pipe_pos = '\0'; // Split string at pipe
        cmd1_str = input;
        cmd2_str = pipe_pos + 1;

        // Check for empty commands around the pipe
        if (cmd1_str[strspn(cmd1_str, " \t\n\r")] == '\0') { fprintf(stderr, "ca$h: syntax error: missing command before pipe `|'\n"); return; }
        if (cmd2_str[strspn(cmd2_str, " \t\n\r")] == '\0') { fprintf(stderr, "ca$h: syntax error: missing command after pipe `|'\n"); return; }

        // Parse the two commands
        int parse1_ok = parse_command(cmd1_str, args1, &inputFile1, &outputFile1);
        int parse2_ok = parse_command(cmd2_str, args2, &inputFile2, &outputFile2);
        if (!parse1_ok || !parse2_ok || args1[0] == NULL || args2[0] == NULL) return; // Parse error occurred

        // Warn about ignored redirection within a pipe
        if (outputFile1 != NULL) fprintf(stderr, "ca$h: warning: output redirection ('>') ignored for command preceding pipe.\n");
        if (inputFile2 != NULL) fprintf(stderr, "ca$h: warning: input redirection ('<') ignored for command succeeding pipe.\n");

        // OS Concept: Inter-Process Communication (IPC) - Create a pipe.
        int pipefd[2];
        if (pipe(pipefd) == -1) { perror("ca$h: Pipe creation failed"); return; }

        pid_t pid1, pid2;

        // --- Fork Child 1 (Left side of pipe) ---
        pid1 = fork();
        if (pid1 < 0) { perror("ca$h: Fork failed (child 1)"); close(pipefd[READ_END]); close(pipefd[WRITE_END]); return; }

        if (pid1 == 0) { // Child 1 Code
             // OS Concept: Process Groups - Child 1 becomes leader of pipeline group.
             if (shell_is_interactive) { if (setpgid(0, 0) < 0) { perror("ca$h: setpgid failed child 1"); exit(EXIT_FAILURE); } }
             // OS Concept: Pipe Redirection - Connect pipe write end to stdout.
             close(pipefd[READ_END]); // Close unused read end
             if (dup2(pipefd[WRITE_END], STDOUT_FILENO) < 0) { perror("ca$h: dup2 failed child 1"); close(pipefd[WRITE_END]); exit(EXIT_FAILURE); }
             close(pipefd[WRITE_END]); // Close original write end
             handle_child_execution(args1, inputFile1, NULL); // Output goes to pipe
        }
        pipeline_pgid = pid1; // PGID for the pipeline is the first child's PID


        // --- Fork Child 2 (Right side of pipe) ---
        pid2 = fork();
         if (pid2 < 0) { // Fork failed for second child
             perror("ca$h: Fork failed (child 2)"); close(pipefd[READ_END]); close(pipefd[WRITE_END]);
             // Clean up first child if fork failed
             if (pipeline_pgid > 0 && shell_is_interactive) kill(-pipeline_pgid, SIGKILL);
             waitpid(pid1, NULL, 0);
             return;
         }
         if (pid2 == 0) { // Child 2 Code
             // OS Concept: Process Groups - Child 2 joins the pipeline group.
             if (shell_is_interactive) { if (setpgid(0, pipeline_pgid) < 0) { perror("ca$h: setpgid failed child 2"); exit(EXIT_FAILURE); } }
             // OS Concept: Pipe Redirection - Connect pipe read end to stdin.
             close(pipefd[WRITE_END]); // Close unused write end
             if (dup2(pipefd[READ_END], STDIN_FILENO) < 0) { perror("ca$h: dup2 failed child 2"); close(pipefd[READ_END]); exit(EXIT_FAILURE); }
             close(pipefd[READ_END]); // Close original read end
             handle_child_execution(args2, NULL, outputFile2); // Input comes from pipe
         }

        // --- Parent Process (after both forks) ---
        // OS Concept: File Descriptor Management - Parent closes its copies of pipe ends.
        close(pipefd[READ_END]);
        close(pipefd[WRITE_END]);

        // Parent ensures child 2 joins the group (handle potential races)
        if (shell_is_interactive) {
            if (setpgid(pid2, pipeline_pgid) < 0 && errno != EACCES && errno != ESRCH) {
                perror("ca$h: parent setpgid for child 2 failed");
            }
        }

        // Handle foreground/background for the pipeline
        if (background) {
            if (shell_is_interactive && pipeline_pgid > 0) {
                 int jid = add_job(pipeline_pgid, original_cmd_for_job, JOB_STATE_RUNNING);
                 if (jid > 0) printf("[%d] %d\n", jid, pipeline_pgid); // Report pipeline PGID
            }
        } else { // Foreground pipeline
             if (shell_is_interactive && pipeline_pgid > 0) {
                  // Wait for the entire pipeline process group
                  char *cmd_copy = strdup(original_cmd_for_job ? original_cmd_for_job : "");
                  if (!cmd_copy) {perror("ca$h: strdup failed for fg pipeline"); return;}
                  job_t fg_job = { .jid = 0, .pgid = pipeline_pgid, .state = JOB_STATE_RUNNING, .command = cmd_copy };
                  put_job_in_foreground(&fg_job, 0);
                  free(cmd_copy);
             } else { // Non-interactive: Wait for both children individually
                  waitpid(pid1, NULL, 0);
                  waitpid(pid2, NULL, 0);
             }
        }
    }
}

//compiling the shell on mac: gcc cash.c -o cash -I/opt/homebrew/include -L/opt/homebrew/lib -lreadline -Wall
//then execute with ./cash