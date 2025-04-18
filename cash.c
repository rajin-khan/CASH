#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // Standard symbolic constants and types (fork, execvp, chdir, pipe, access, getcwd, pid_t)
#include <sys/types.h>  // Primitive system data types (pid_t)
#include <sys/wait.h>   // Waiting for process termination (waitpid)
#include <fcntl.h>      // File control options (open, O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC)
#include <errno.h>      // Error number definitions (errno)

#define MAX_INPUT 1024
#define MAX_ARGS 100
#define READ_END 0      // Index for the read end of a pipe
#define WRITE_END 1     // Index for the write end of a pipe

// --- Function Declarations ---
void display_welcome_message();
int parse_command(char *command_str, char **args, char **inputFile, char **outputFile);
void execute_pipeline(char *input);
void execute_single_command(char **args, int background, char *inputFile, char *outputFile);
void handle_child_execution(char **args, char *inputFile, char *outputFile);

// --- Main Function ---
int main() {
    char input[MAX_INPUT];

    display_welcome_message();

    while(1) {
        printf("ca$h> ");
        fflush(stdout); // Ensure prompt is displayed before waiting for input

        if (!fgets(input, MAX_INPUT, stdin)) {
            // Handle EOF (Ctrl+D) or read error
            printf("\nClosing ca$h...\n");
            break;
        }

        // Check for empty input line (contains only whitespace/newline)
        if (input[strspn(input, " \t\n\r")] == '\0') {
            continue; // Skip empty lines and show prompt again
        }

        // Process the command (could be single or pipeline)
        execute_pipeline(input);
    }

    return 0;
}

// --- Helper Functions ---

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
    printf("IPC via Pipes (|)\n");
    printf("Type 'exit' to quit.\n\n");
}

/**
 * @brief Parses a single command string (part of a potential pipeline).
 *
 * @param command_str The command string to parse. Should be mutable (strtok modifies it).
 * @param args Output array for arguments. args[0] will be NULL if no command found or error.
 * @param inputFile Output pointer for input redirection file.
 * @param outputFile Output pointer for output redirection file.
 * @return Returns 1 if parsing found a valid command structure (even if redirection failed later),
 *         0 on syntax error (e.g., missing filename after >) or if command_str is effectively empty.
 *
 * OS Concepts Illustrated:
 * - Input Parsing: Shells must break down user input into meaningful tokens.
 * - Tokenization: Identifying command names, arguments, and special shell operators (<, >, |).
 */
int parse_command(char *command_str, char **args, char **inputFile, char **outputFile) {
    int i = 0;
    *inputFile = NULL;
    *outputFile = NULL;
    args[0] = NULL; // Initialize to NULL: indicates no command found yet / error state

    // Check for NULL or effectively empty string after trimming initial whitespace
    char* trimmed_cmd = command_str + strspn(command_str, " \t\n\r\a");
    if (*trimmed_cmd == '\0') {
        return 0; // It's an empty command string
    }

    // Use strtok to break the command string into tokens
    char *token = strtok(command_str, " \t\n\r\a");
    while (token != NULL && i < MAX_ARGS - 1) {
        if (strcmp(token, "<") == 0) {
            // Input redirection token found
            token = strtok(NULL, " \t\n\r\a"); // Get the filename
            if (token == NULL || strcmp(token, ">") == 0 || strcmp(token, "<") == 0 || strcmp(token, "|") == 0 || strcmp(token, "&") == 0) {
                fprintf(stderr, "ca$h: syntax error near redirection `<'\n");
                args[0] = NULL; // Ensure command is marked invalid
                return 0; // Syntax error
            }
            *inputFile = token; // Store the input filename
        } else if (strcmp(token, ">") == 0) {
            // Output redirection token found
            token = strtok(NULL, " \t\n\r\a"); // Get the filename
            if (token == NULL || strcmp(token, ">") == 0 || strcmp(token, "<") == 0 || strcmp(token, "|") == 0 || strcmp(token, "&") == 0) {
                fprintf(stderr, "ca$h: syntax error near redirection `>'\n");
                args[0] = NULL; // Ensure command is marked invalid
                return 0; // Syntax error
            }
            *outputFile = token; // Store the output filename
        } else {
            // Regular argument or command name
            args[i++] = token;
        }
        token = strtok(NULL, " \t\n\r\a"); // Get next token
    }
    args[i] = NULL; // Null-terminate the argument list for execvp

    // Final check: If we only found redirection but no command word
    if (args[0] == NULL && (*inputFile != NULL || *outputFile != NULL)) {
        fprintf(stderr, "ca$h: syntax error: redirection without command\n");
        return 0; // Invalid syntax
    }

    // Return success if a command word was found (args[0] is not NULL)
    return (args[0] != NULL);
}


/**
 * @brief Handles the execution logic within a child process (redirection, execvp).
 *        This function is designed to be called *after* fork() in the child.
 *        It does not return on success (due to execvp), only on failure (via exit).
 *
 * @param args Command and arguments (null-terminated array).
 * @param inputFile Filename for input redirection (or NULL).
 * @param outputFile Filename for output redirection (or NULL).
 *
 * OS Concepts Illustrated: (Referenced from execute_single_command/execute_pipeline)
 * - Runs exclusively in the child process context.
 * - File Descriptor Manipulation: Uses `open`, `dup2`, `close` to set up redirection.
 *   These changes affect *only* the child process.
 * - Program Loading: `execvp` overlays the child process with the requested program.
 * - Process Termination: `exit()` is crucial if any step fails *before* `execvp`,
 *   or if `execvp` itself fails, preventing a zombie shell child.
 */
void handle_child_execution(char **args, char *inputFile, char *outputFile) {
    int fd_in = -1, fd_out = -1;

    // --- Input Redirection Setup (if specified) ---
    if (inputFile != NULL) {
        // OS Concept: `open` system call to get a file descriptor for reading.
        fd_in = open(inputFile, O_RDONLY);
        if (fd_in < 0) {
            perror("ca$h: Failed to open input file");
            exit(EXIT_FAILURE); // Child must exit if redirection fails
        }
        // OS Concept: `dup2` system call to duplicate fd_in onto standard input (fd 0).
        if (dup2(fd_in, STDIN_FILENO) < 0) {
            perror("ca$h: Failed to redirect standard input (dup2)");
            close(fd_in); // Close the file descriptor before exiting
            exit(EXIT_FAILURE);
        }
        // OS Concept: `close` system call. Close the original fd_in, it's no longer needed.
        close(fd_in);
    }

    // --- Output Redirection Setup (if specified) ---
    if (outputFile != NULL) {
        // OS Concept: `open` for writing (create if needed, truncate if exists).
        fd_out = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644); // 0644 permissions
        if (fd_out < 0) {
            perror("ca$h: Failed to open output file");
            exit(EXIT_FAILURE);
        }
        // OS Concept: `dup2` to duplicate fd_out onto standard output (fd 1).
        if (dup2(fd_out, STDOUT_FILENO) < 0) {
            perror("ca$h: Failed to redirect standard output (dup2)");
            close(fd_out); // Close the file descriptor before exiting
            exit(EXIT_FAILURE);
        }
        // OS Concept: `close` the original fd_out.
        close(fd_out);
    }

    // --- Execute the Command ---
    // OS Concept: `execvp`. Replaces the child process image. Searches PATH.
    if (execvp(args[0], args) == -1) {
        // execvp only returns on error
        fprintf(stderr, "ca$h: Command not found or execution failed: %s\n", args[0]);
        // errno might provide more details, could use perror("ca$h: execvp failed") too
        exit(EXIT_FAILURE); // Crucial: exit child if execvp fails
    }
    // If execvp succeeds, this point is never reached.
}


/**
 * @brief Executes a single command (no pipe involved).
 *
 * Handles built-ins or forks a child process for external commands.
 * Manages background execution and waits for foreground commands.
 *
 * @param args Null-terminated argument array. Must have args[0] != NULL.
 * @param background Flag indicating background execution (1 for background, 0 for foreground).
 * @param inputFile Filename for input redirection (or NULL).
 * @param outputFile Filename for output redirection (or NULL).
 */
void execute_single_command(char **args, int background, char *inputFile, char *outputFile) {

    // --- Handle Built-in Commands ---
    // These modify the shell's state directly, so no fork/exec needed.

    if (strcmp(args[0], "exit") == 0) {
        // OS Concept: `exit` system call terminates the calling process (the shell).
        printf("Closing ca$h...\n");
        exit(0);
    }

    if (strcmp(args[0], "cd") == 0) {
        // OS Concept: `chdir` system call changes the current working directory
        // of the calling process (the shell). Must be built-in.
        if (inputFile || outputFile) {
             fprintf(stderr, "ca$h: warning: redirection does not apply to built-in 'cd'\n");
        }
        if (args[1] == NULL) { // 'cd' without arguments
            const char *home = getenv("HOME"); // OS Concept: Environment variables
            if (home == NULL) {
                fprintf(stderr, "ca$h: cd: HOME environment variable not set\n");
                return;
            }
            if (chdir(home) != 0) {
                perror("ca$h: cd to HOME failed");
            }
        } else if (args[2] != NULL) { // 'cd' with too many arguments
             fprintf(stderr, "ca$h: cd: too many arguments\n");
        } else { // 'cd' with one argument
            if (chdir(args[1]) != 0) {
                perror("ca$h: cd failed"); // perror prints the system error message
            }
        }
        return; // Built-in handled
    }

     if (strcmp(args[0], "clear") == 0) {
         // Using system() is a shortcut; it forks a subshell.
         // A more direct way involves terminal control escape sequences.
         if (inputFile || outputFile) {
             fprintf(stderr, "ca$h: warning: redirection does not apply to 'clear'\n");
         }
         system("clear"); // Forks a shell to run the 'clear' command
         return; // Built-in handled
     }


    // --- Handle External Commands via Fork/Exec ---

    // OS Concept: `fork()` - Creates a child process that's a copy of the parent (shell).
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("ca$h: Fork failed");
        return; // Stay in the shell
    } else if (pid == 0) {
        // --- Child Process ---
        // The child process will execute the command.
        // handle_child_execution sets up redirection and calls execvp.
        handle_child_execution(args, inputFile, outputFile);
        // handle_child_execution only returns if there was an error before/during execvp,
        // in which case it calls exit().
    } else {
        // --- Parent Process (Shell) ---
        // The shell decides whether to wait for the child or continue.
        if (background) {
            // OS Concept: Background Process - Parent doesn't wait.
            // Prints the child's PID and continues accepting commands.
            printf("[%d] %d\n", pid, pid);
            // NOTE: This creates a zombie process when the child finishes,
            //       as the parent isn't reaping it with waitpid.
            //       Requires SIGCHLD handling for proper cleanup.
        } else {
            // OS Concept: Foreground Process & Waiting - Parent waits for the child.
            // `waitpid` suspends the parent until the specified child (pid) changes state
            // (e.g., terminates). Status information can be optionally collected.
            int status;
            waitpid(pid, &status, 0); // 0 flag: wait specifically for pid
            // Could add checks here: WIFEXITED(status), WEXITSTATUS(status)
            // to see how the child terminated.
        }
    }
}


/**
 * @brief Parses the input line for a potential pipeline ('|') and executes it.
 *
 * Detects '|', splits the command, sets up the pipe, and forks children.
 * If no pipe is found, calls execute_single_command.
 * Handles background execution ('&') for the entire job (single command or pipeline).
 *
 * @param input The raw command line input string (will be modified by strtok).
 *
 * OS Concepts Illustrated:
 * - Pipeline Parsing: Finding '|' to separate commands.
 * - Background Jobs: Detecting '&' at the end of the line.
 * - IPC with `pipe()`: Creates a kernel-managed buffer connecting two file descriptors.
 * - Process Creation (`fork()`): Twice for a pipeline.
 * - File Descriptor Management (`dup2`, `close`): Essential for connecting children
 *   to the pipe and closing unused descriptors in parent and children. Prevents hangs.
 * - Process Synchronization (`waitpid`): Waiting for multiple children in foreground pipelines.
 */
void execute_pipeline(char *input) {
    char *args1[MAX_ARGS], *args2[MAX_ARGS];
    char *inputFile1 = NULL, *outputFile1 = NULL; // Redirection for cmd1
    char *inputFile2 = NULL, *outputFile2 = NULL; // Redirection for cmd2
    int background = 0;
    char *pipe_pos = NULL;
    char *cmd1_str = NULL;
    char *cmd2_str = NULL;

    // --- 1. Check for Background Execution (&) ---
    // Find the last non-whitespace character.
    char *end = input + strlen(input) - 1;
    while (end >= input && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0'; // Null-terminate, effectively trimming whitespace
        end--;
    }
    // Check if the last character is now '&'
    if (end >= input && *end == '&') {
        background = 1; // Mark as background job
        *end = '\0';    // Remove the '&' from the command string
        // Trim any potential whitespace just before the '&'
        end--;
         while (end >= input && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
    }

    // If the command is now empty after removing &, return
    if (input[strspn(input, " \t\n\r")] == '\0') {
        return;
    }


    // --- 2. Check for Pipe Symbol (|) ---
    pipe_pos = strchr(input, '|');

    if (pipe_pos == NULL) {
        // --- 3a. No Pipe: Execute as a Single Command ---
        cmd1_str = input; // The whole input is the single command string

        // Parse the single command
        if (!parse_command(cmd1_str, args1, &inputFile1, &outputFile1)) {
            // Parsing failed (syntax error reported by parse_command or empty)
            return;
        }

        // If parsing succeeded but somehow args[0] is null (shouldn't happen with current parse_command), exit
        if (args1[0] == NULL) {
             // This case should ideally be caught by parse_command returning 0
             fprintf(stderr, "ca$h: Internal error: parsed command is NULL\n");
             return;
        }

        // Execute the single command (handles built-ins and fork/exec)
        execute_single_command(args1, background, inputFile1, outputFile1);

    } else {
        // --- 3b. Pipe Found: Execute Pipeline ---

        *pipe_pos = '\0'; // Split the input string into two at the pipe symbol
        cmd1_str = input;
        cmd2_str = pipe_pos + 1;

        // Preliminary check: is there anything after the pipe?
         if (cmd2_str[strspn(cmd2_str, " \t\n\r")] == '\0') {
            fprintf(stderr, "ca$h: syntax error: missing command after pipe `|'\n");
            return;
        }
         // Preliminary check: is there anything before the pipe?
         if (cmd1_str[strspn(cmd1_str, " \t\n\r")] == '\0') {
            fprintf(stderr, "ca$h: syntax error: missing command before pipe `|'\n");
            return;
        }


        // Parse the first command (left side of pipe)
        int parse1_ok = parse_command(cmd1_str, args1, &inputFile1, &outputFile1);
        // Parse the second command (right side of pipe)
        int parse2_ok = parse_command(cmd2_str, args2, &inputFile2, &outputFile2);

        // Check if both commands parsed successfully AND resulted in a command word
        if (!parse1_ok || !parse2_ok || args1[0] == NULL || args2[0] == NULL) {
            // An error message should have been printed by parse_command if !parseX_ok
            // If parse was ok but args[0] is null, it means redirection without command, also an error.
             if (parse1_ok && args1[0] == NULL) fprintf(stderr, "ca$h: Error parsing command before pipe.\n");
             if (parse2_ok && args2[0] == NULL) fprintf(stderr, "ca$h: Error parsing command after pipe.\n");
            return; // Don't proceed if parsing failed for either side
        }


        // --- Pipeline specific redirection validation ---
        // Output redirection for the command *before* the pipe is meaningless.
        if (outputFile1 != NULL) {
             fprintf(stderr, "ca$h: warning: output redirection ('>') ignored for command preceding pipe.\n");
             // We don't actually need to set outputFile1 = NULL, as it won't be passed
             // to handle_child_execution for child 1's output anyway.
        }
        // Input redirection for the command *after* the pipe is meaningless.
        if (inputFile2 != NULL) {
             fprintf(stderr, "ca$h: warning: input redirection ('<') ignored for command succeeding pipe.\n");
             // Similarly, no need to set inputFile2 = NULL.
        }


        // OS Concept: `pipe()` - Create the pipe (IPC channel).
        int pipefd[2]; // pipefd[0] = read end, pipefd[1] = write end
        if (pipe(pipefd) == -1) {
            perror("ca$h: Pipe creation failed");
            return;
        }

        // --- Fork Child 1 (for command 1) ---
        pid_t pid1 = fork();
        if (pid1 < 0) {
            perror("ca$h: Fork failed (child 1)");
            close(pipefd[READ_END]); // Clean up pipe fds on failure
            close(pipefd[WRITE_END]);
            return;
        }

        if (pid1 == 0) {
            // --- Child 1 Code (Left side of Pipe - Writer) ---

            // OS Concept: Close unused pipe end. Child 1 writes, doesn't read.
            close(pipefd[READ_END]);

            // OS Concept: Redirect stdout to pipe write end.
            // If dup2 fails, report error and exit child.
            if (dup2(pipefd[WRITE_END], STDOUT_FILENO) < 0) {
                 perror("ca$h: dup2 failed for pipe write end (child 1)");
                 close(pipefd[WRITE_END]); // Close the remaining pipe fd before exiting
                 exit(EXIT_FAILURE);
            }
            // OS Concept: Close original pipe write fd. It's now duplicated to stdout.
            close(pipefd[WRITE_END]);

            // Execute command 1. Input redirection (inputFile1) is handled inside.
            // Output redirection (outputFile1) is explicitly ignored here (passed as NULL)
            // because output *must* go to the pipe.
            handle_child_execution(args1, inputFile1, NULL);
            // handle_child_execution exits child on success (execvp) or failure.
        }

        // --- Fork Child 2 (for command 2) ---
        pid_t pid2 = fork();
         if (pid2 < 0) {
            perror("ca$h: Fork failed (child 2)");
            // Need to clean up pipe and potentially child 1
            close(pipefd[READ_END]);
            close(pipefd[WRITE_END]);
            // Send kill signal to child 1? Or just wait? Let's wait for simplicity.
            if (!background) {
                waitpid(pid1, NULL, 0); // Wait for child 1 if it was successfully forked
            } else {
                // If background, maybe print an error that pipeline failed mid-creation
                fprintf(stderr, "ca$h: Failed to fork second child for pipeline.\n");
            }
            return;
        }


        if (pid2 == 0) {
            // --- Child 2 Code (Right side of Pipe - Reader) ---

            // OS Concept: Close unused pipe end. Child 2 reads, doesn't write.
            close(pipefd[WRITE_END]);

            // OS Concept: Redirect stdin to pipe read end.
            if (dup2(pipefd[READ_END], STDIN_FILENO) < 0) {
                 perror("ca$h: dup2 failed for pipe read end (child 2)");
                 close(pipefd[READ_END]); // Close remaining pipe fd before exit
                 exit(EXIT_FAILURE);
            }
            // OS Concept: Close original pipe read fd. Now duplicated to stdin.
            close(pipefd[READ_END]);

            // Execute command 2. Output redirection (outputFile2) is handled inside.
            // Input redirection (inputFile2) is explicitly ignored (passed as NULL)
            // because input *must* come from the pipe.
            handle_child_execution(args2, NULL, outputFile2);
            // handle_child_execution exits child on success or failure.
        }

        // --- Parent Process Code (after forking both children) ---

        // OS Concept: Parent *must* close both ends of the pipe in its own process.
        // If the parent keeps the write end open, child 2 might never see EOF on the read end.
        // If the parent keeps the read end open, resources are leaked.
        close(pipefd[READ_END]);
        close(pipefd[WRITE_END]);

        // --- Wait for Children (if not background) ---
        if (!background) {
            int status1, status2;
            // OS Concept: Wait for both children to complete. Order doesn't strictly matter
            // unless you care about which one finished first/last.
            waitpid(pid1, &status1, 0);
            waitpid(pid2, &status2, 0);
            // Could check statuses if needed.
        } else {
            // For a background pipeline, report the PID. Often the PID of the *last*
            // command in the pipeline is reported by shells.
             printf("[%d] %d\n", pid2, pid2);
             // NOTE: Both children will become zombies when they finish.
             //       Requires SIGCHLD handling later.
        }
    }
}