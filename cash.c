#include<stdio.h>
#include<stdlib.h>

#define MAX_INPUT 1024

int main() {

    char input[MAX_INPUT];

    while(1) {

        printf("ca$h> ");

        if (!fgets(input, MAX_INPUT, stdin)) {
            
            break;
        }

        printf("You entered: %s", input);
    }

    return 0;
}