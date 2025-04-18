#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // Standard symbolic constants and types (fork, execvp, chdir, access, getcwd, pid_t)
#include <sys/types.h>  // Primitive system data types (pid_t)
#include <sys/wait.h>   // Waiting for process termination (waitpid)
#include <fcntl.h>      // File control options (open, O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC)

#define MAX_INPUT 1024
#define MAX_ARGS 100

// --- Function Declarations ---
void display_welcome_message();
void execute_command(char *input);
void parse_command(char *input, char **args, int *background, char **inputFile, char **outputFile);

// --- Main Function ---
int main() {
    char input[MAX_INPUT];

    display_welcome_message();

    // The main shell loop
    while(1) {
        // Display the prompt
        printf("ca$h> ");
        fflush(stdout); // Ensure prompt is displayed before waiting for input

        // Read user input using fgets (safer than gets)
        if (!fgets(input, MAX_INPUT, stdin)) {
            // Handle End-of-File (Ctrl+D) or read error
            printf("\nClosing ca$h...\n");
            break;
        }

        // Execute the command entered by the user
        execute_command(input);
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
    printf("Waiting (waitpid), Background Jobs (&), File I/O Redirection (<, >)\n");
    printf("Type 'exit' to quit.\n\n");
}

/**
 * @brief Parses the input line into arguments and detects redirection/background symbols.
 *
 * @param input The raw command line input string.
 * @param args Output array to store command arguments.
 * @param background Output flag indicating if the command should run in the background.
 * @param inputFile Output pointer to store the input redirection filename (if any).
 * @param outputFile Output pointer to store the output redirection filename (if any).
 *
 * OS Concepts Illustrated:
 * - String manipulation is fundamental for interpreting user commands before interacting with the OS.
 * - This function preprocesses the command to identify special shell syntax (&, <, >)
 *   which determines how the OS will be asked to execute the command (e.g., background,
 *   with modified standard input/output).
 */
void parse_command(char *input, char **args, int *background, char **inputFile, char **outputFile) {
    int i = 0;
    *background = 0;
    *inputFile = NULL;
    *outputFile = NULL;

    // Tokenize the input string based on spaces and newline
    // OS Concept: Input processing - Shells must parse user input to understand commands and arguments.
    char *token = strtok(input, " \t\n\r\a"); // Use a wider range of delimiters
    while (token != NULL && i < MAX_ARGS - 1) {
        // Check for I/O redirection symbols
        if (strcmp(token, "<") == 0) {
            // Input redirection found
            token = strtok(NULL, " \t\n\r\a"); // Get the next token (the filename)
            if (token == NULL) {
                fprintf(stderr, "ca$h: syntax error near unexpected token `newline'\n");
                args[0] = NULL; // Mark as invalid command
                return;
            }
            *inputFile = token;
        } else if (strcmp(token, ">") == 0) {
            // Output redirection found
            token = strtok(NULL, " \t\n\r\a"); // Get the next token (the filename)
            if (token == NULL) {
                fprintf(stderr, "ca$h: syntax error near unexpected token `newline'\n");
                args[0] = NULL; // Mark as invalid command
                return;
            }
            *outputFile = token;
        } else {
            // Regular argument
            args[i++] = token;
        }
        token = strtok(NULL, " \t\n\r\a");
    }
    args[i] = NULL; // Null-terminate the argument list for execvp

    // Check for background execution symbol (&) *after* parsing redirection
    // It must be the very last token if present.
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        *background = 1;
        args[i - 1] = NULL; // Remove "&" from arguments list
        i--; // Decrement argument count
    }
}

/**
 * @brief Executes the parsed command.
 *
 * Handles built-in commands (exit, cd, clear) and external commands
 * via fork/execvp, including background execution and I/O redirection.
 *
 * @param input The raw command line input string.
 *
 * OS Concepts Illustrated:
 * - Built-in Commands: Some commands are handled directly by the shell process (e.g., `cd`, `exit`)
 *   because they need to modify the shell's own state (current directory, termination).
 *   `cd` uses the `chdir()` system call. `exit` uses the `exit()` system call.
 * - External Commands: Most commands are separate programs. The shell uses:
 *   - `fork()`: System call to create a new process (child process). This duplicates the parent shell.
 *   - `execvp()`: System call (within the child) to replace the child's memory space with a new program image.
 *     It searches the system's PATH environment variable to find the executable.
 *   - `waitpid()`: System call (within the parent) to wait for a child process to change state (e.g., terminate).
 *     Used for foreground commands.
 *   - Process IDs (PID): Unique identifiers for processes, returned by `fork()`.
 *   - Background Processes: If '&' is present, the parent shell does *not* wait for the child, allowing
 *     concurrent execution. (Note: Proper handling of terminated background children requires signals).
 *   - File Descriptors: Integers representing open files/devices (0=stdin, 1=stdout, 2=stderr).
 *   - `open()`: System call to open a file and get a file descriptor.
 *   - `dup2()`: System call to duplicate a file descriptor onto a specific number (0 or 1),
 *     effectively redirecting stdin or stdout *for the child process* before `execvp`.
 *   - `close()`: System call to release a file descriptor.
 *   - Error Handling (`perror`): Essential for diagnosing failed system calls.
 */
void execute_command(char *input) {
    char *args[MAX_ARGS];
    int background;
    char *inputFile = NULL;
    char *outputFile = NULL;

    // Parse the command line
    parse_command(input, args, &background, &inputFile, &outputFile);

    // If parsing resulted in an empty command (e.g., just whitespace or syntax error), return
    if (args[0] == NULL) {
        return;
    }

    // --- Handle Built-in Commands ---

    if (strcmp(args[0], "exit") == 0) {
        printf("Closing ca$h...\n");
        exit(0); // Terminate the shell process
    }

    if (strcmp(args[0], "cd") == 0) {
        // OS Concept: Modifying process state - 'cd' changes the current working directory
        // of the *shell process itself*. This is why it *must* be a built-in.
        const char *dir = args[1]; // Target directory is the second argument

        if (dir == NULL) {
            // No argument given, change to HOME directory
            dir = getenv("HOME"); // Get HOME environment variable
            if (dir == NULL) {
                fprintf(stderr, "ca$h: cd: HOME not set\n");
                return;
            }
        }

        // OS Concept: System Call `chdir` - Attempts to change the current working directory.
        if (chdir(dir) != 0) {
            perror("ca$h: cd failed"); // Use perror for system call errors
        }
        return; // Handled built-in, return to main loop
    }

    if (strcmp(args[0], "clear") == 0) {
        // Note: system() forks another shell, less ideal but simple.
        // A more direct approach would use terminal control sequences.
        system("clear");
        return;
    }

    // --- Handle External Commands ---

    // OS Concept: Process Creation - `fork()` creates a near-identical copy of the current (shell) process.
    // The child process gets its own memory space, but inherits things like open file descriptors.
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("ca$h: Fork failed");
        return; // Return to shell prompt
    } else if (pid == 0) {
        // --- Child Process ---
        // This code runs ONLY in the newly created child process.

        // OS Concept: I/O Redirection - Before running the new program,
        // we modify the child's standard input/output file descriptors if requested.
        int fd_in = -1, fd_out = -1; // File descriptors for redirection

        if (inputFile != NULL) {
            // OS Concept: `open()` System Call - Opens the specified file for reading.
            // Returns a file descriptor (a small integer).
            fd_in = open(inputFile, O_RDONLY);
            if (fd_in < 0) {
                perror("ca$h: Failed to open input file");
                exit(EXIT_FAILURE); // Child process must exit on redirection failure
            }
            // OS Concept: `dup2()` System Call - Duplicates fd_in onto STDIN_FILENO (0).
            // After this, any attempt by the process to read from standard input (fd 0)
            // will actually read from the opened file (fd_in).
            if (dup2(fd_in, STDIN_FILENO) < 0) {
                perror("ca$h: Failed to redirect standard input");
                exit(EXIT_FAILURE);
            }
            // OS Concept: `close()` System Call - Close the original file descriptor
            // as it's no longer needed; standard input (0) now points to the file.
            close(fd_in);
        }

        if (outputFile != NULL) {
            // OS Concept: `open()` System Call - Opens the specified file for writing.
            // O_CREAT: Create the file if it doesn't exist.
            // O_WRONLY: Open for writing only.
            // O_TRUNC: Truncate the file to zero length if it exists.
            // 0644: File permissions (read/write for owner, read for group/others).
            fd_out = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                perror("ca$h: Failed to open output file");
                exit(EXIT_FAILURE); // Child process must exit on redirection failure
            }
            // OS Concept: `dup2()` System Call - Duplicates fd_out onto STDOUT_FILENO (1).
            // Now, any attempt by the process to write to standard output (fd 1)
            // will actually write to the opened file (fd_out).
            if (dup2(fd_out, STDOUT_FILENO) < 0) {
                perror("ca$h: Failed to redirect standard output");
                exit(EXIT_FAILURE);
            }
            // OS Concept: `close()` System Call - Close the original file descriptor.
            close(fd_out);
        }

        // OS Concept: Program Execution - `execvp()` replaces the current process image
        // (the child shell's code) with the new program specified by args[0].
        // It searches the PATH for the executable. `v` means it takes an array of args,
        // `p` means it searches the PATH.
        // If execvp is successful, *it does not return*. The code below this line in the child
        // is only reached if execvp fails.
        if (execvp(args[0], args) == -1) {
            perror("ca$h: Execution failed");
            // Important: Child MUST exit if execvp fails, otherwise, you'll have
            // two shells running (the original parent, and the child that failed to exec).
            exit(EXIT_FAILURE);
        }
        // No code here should be reachable in the child if execvp succeeds.

    } else {
        // --- Parent Process ---
        // This code runs ONLY in the original shell process after fork.

        if (background) {
            // OS Concept: Background Process - The parent does *not* wait for the child.
            // It prints the child's PID and returns to the prompt immediately.
            // Note: This basic implementation doesn't handle reaping terminated background children (zombies).
            printf("[%d] %d\n", pid, pid); // Print job ID (using PID) and PID
        } else {
            // OS Concept: Foreground Process & Waiting - The parent uses `waitpid()`
            // to pause its own execution until the specified child process (pid) terminates.
            // This makes the shell wait for the command to complete before showing the prompt again.
            // The third argument (0) means wait for the specific pid. Status info is ignored (NULL).
            int status;
            waitpid(pid, &status, 0);
            // Could potentially check WIFEXITED(status) and WEXITSTATUS(status) here
            // to report the exit status of the child, but keeping it simple for now.
        }
    }
}