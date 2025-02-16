#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>

#define MAX_INPUT 1024
#define MAX_ARGS 100

void display_welcome_message() {
    printf("\n");
    printf(" ██████╗ █████╗ ███████╗██╗  ██╗\n");
    printf("██╔════╝██╔══██╗██╔════╝██║  ██║\n");
    printf("██║     ███████║███████╗███████║\n");
    printf("██║     ██╔══██║╚════██║██╔══██║\n");
    printf("╚██████╗██║  ██║███████║██║  ██║\n");
    printf(" ╚═════╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝\n");
    printf("\n");
    printf("Welcome to ca$h - the simplest command and script shell! \n");
    printf("Type 'exit' to quit.\n\n");
}


void execute_command(char *input) {

    char *args[MAX_ARGS];
    int i = 0;

    args[i] = strtok(input, " \n");
    while (args[i] != NULL) {
        i++;
        args[i] = strtok(NULL, " \n");
    }

    if (args[0] == NULL) return;

    if (strcmp(args[0], "clear") == 0) {

        system("clear");
    }

    if (strcmp(args[0], "exit") == 0) {

        printf("Closing ca$h...\n");
        exit(0);
    }

    if (strcmp(args[0], "cd") == 0) {

        const char *home = getenv("HOME");
        const char *dir = (args[1] == NULL) ? home : args[1];

        if (dir== NULL) {

            fprintf(stderr, "cd: HOME path not set\n");
        } else if (chdir(dir) != 0) {

            perror("cd failed");
        }

        return;
    }

    pid_t pid = fork();

    if (pid < 0) {

        perror("Fork failed");
    } else if (pid == 0) {

        if (execvp(args[0], args) == -1) {

            perror("Execution failed");
        }

        exit(EXIT_FAILURE);
    } else {

        wait(NULL);
    }

}

int main() {

    char input[MAX_INPUT];

    display_welcome_message();

    while(1) {

        printf("ca$h> ");

        if (!fgets(input, MAX_INPUT, stdin)) {

            break;
        }

        execute_command(input);
    }

    return 0;
}