#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_PATHS 20
#define MAX_COMMANDS 10
#define ERROR_MESSAGE "An error has occurred\n"

// Function to clear the PATH
void clearPath(char* paths[], size_t* path_counter) {
    int j;
    for (j = 0; j < *path_counter; j++) {
        free(paths[j]);
        paths[j] = NULL;
    }
    *path_counter = 0;
}

// Function to find the executable in the provided paths
char* find_executable(char* command, char* paths[], size_t counter) {
    char full_path[100];
    int i;
    for (i = 0; i < counter; i++) {
        snprintf(full_path, sizeof(full_path), "%s/%s", paths[i], command);
        if (access(full_path, X_OK) == 0) {
            return strdup(full_path);
        }
    }
    return NULL;
}

// Function to execute a single command
void execute_command(char** args, char* paths[], size_t path_counter, bool redirection, char* output_file) {
    char* executable = find_executable(args[0], paths, path_counter);
    if (executable == NULL) {
        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
        return;
    }

    int rc = fork();
    if (rc < 0) {
        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
        exit(1);
    } else if (rc == 0) {  // Child process
        if (redirection) {
            int fd = open(output_file, O_WRONLY|O_TRUNC|O_CREAT, S_IRWXU);
            if (fd < 0) {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execv(executable, args);
        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
        exit(1);
    }
    free(executable);
}

// Function to process a line of input (either from interactive mode or batch file)
void process_line(char* line, char* paths[], size_t* path_counter, bool batch_mode) {
    char* commands[MAX_COMMANDS];
    int command_count = 0;
    char* command_token;
    char* saveptr1;

    // Split the line into multiple commands (if any)
    command_token = strtok_r(line, "&", &saveptr1);
    while (command_token != NULL && command_count < MAX_COMMANDS) {
        commands[command_count++] = command_token;
        command_token = strtok_r(NULL, "&", &saveptr1);
    }

    int cmd;
    for (cmd = 0; cmd < command_count; cmd++) {
        char* args[20];
        int arg_count = 0;
        char* arg_token;
        char* saveptr2;
        bool redirection = false;
        char* output_file = NULL;

        // Parse the command and its arguments
        arg_token = strtok_r(commands[cmd], " \t\n", &saveptr2);
        while (arg_token != NULL && arg_count < 19) {
            if (strcmp(arg_token, ">") == 0) {
                redirection = true;
                arg_token = strtok_r(NULL, " \t\n", &saveptr2);
                if (arg_token != NULL) {
                    output_file = arg_token;
                } else {
                    write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                    return;
                }
                break;
            }
            args[arg_count++] = arg_token;
            arg_token = strtok_r(NULL, " \t\n", &saveptr2);
        }
        args[arg_count] = NULL;

        if (arg_count == 0) continue;  // Skip empty commands

        // Handle built-in commands
        if (strcmp(args[0], "exit") == 0) {
            if (arg_count > 1 || batch_mode) {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
                if (!batch_mode) continue;
            }
            exit(0);
        } else if (strcmp(args[0], "cd") == 0) {
            if (arg_count != 2) {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
            } else if (chdir(args[1]) != 0) {
                write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
            }
        } else if (strcmp(args[0], "path") == 0) {
            clearPath(paths, path_counter);
            int i;
            for (i = 1; i < arg_count; i++) {
                paths[*path_counter] = strdup(args[i]);
                (*path_counter)++;
            }
        } else {
            execute_command(args, paths, *path_counter, redirection, output_file);
        }
    }

    // Wait for all child processes to complete
    while (wait(NULL) > 0);
}

int main(int argc, char* argv[]) {
    char* paths[MAX_PATHS] = { strdup("/bin"), NULL };
    size_t path_counter = 1;

    if (argc > 2) {
        write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
        exit(1);
    }

    if (argc == 2) {
        // Batch mode
        FILE* batch_file = fopen(argv[1], "r");
        if (batch_file == NULL) {
            write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
            exit(1);
        }

        char* line = NULL;
        size_t len = 0;
        ssize_t read;

        while ((read = getline(&line, &len, batch_file)) != -1) {
            process_line(line, paths, &path_counter, true);
        }

        free(line);
        fclose(batch_file);
    } else {
        // Interactive mode
        while (1) {
            printf("dash> ");
            char* line = NULL;
            size_t len = 0;
            ssize_t read = getline(&line, &len, stdin);

            if (read == -1) {
                clearPath(paths, &path_counter);
                exit(0);
            }

            process_line(line, paths, &path_counter, false);
            free(line);
        }
    }

    clearPath(paths, &path_counter);
    return 0;
}
