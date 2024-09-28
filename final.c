#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_COMMAND 100
#define MAX_ARGS 100
#define MAX_PATH 100
#define MAX_PATH_LENGTH 1024
const char error_message[30] = "An error has occurred\n";
/*
paths: all the potential paths (could be invalid)
path_counter: number of paths
*/
// Function to clear the PATH
void clear_path(char *paths[], size_t *path_counter)
{
  int j;
  for (j = 0; j < *path_counter; j++)
  {
    free(paths[j]); // Free each allocated path
    paths[j] = NULL;
  }
  *path_counter = 0;
}
/*
paths: all the potential paths (could be invalid)
path_counter: number of paths
*/
// Function to find the executable in the provided paths
char *find_executable(char *command, char *paths[], size_t *path_counter)
{
  char full_path[MAX_PATH_LENGTH];
  int i = 0;
  for (; i < *path_counter; i++)
  {
    snprintf(full_path, sizeof(full_path), "%s/%s", paths[i], command); // Construct the full path
    if (access(full_path, X_OK) == 0)
    {                           // Check if the command is executable
      return strdup(full_path); // Return a copy of the full path
    }
  }
  return NULL; // Return NULL if not found
}
/*
args: Stack allocated arguments
paths: all the potential paths (could be invalid)
path_counter: number of paths
redirection: flag to check if redirect
output_file: file descriptor to redirect
*/
void execute_command(char **args, char *paths[], size_t *path_counter, bool redirection, char *output_file)
{
  char *executable = find_executable(args[0], paths, path_counter);
  if (executable == NULL)
  {
    write(STDERR_FILENO, error_message, strlen(error_message));
    return;
  }
  pid_t rc = fork();
  if (rc < 0)
  {
    write(STDERR_FILENO, error_message, strlen(error_message));
    return;
  }
  else if (rc == 0)
  {
    if (redirection)
    {
      int fd = open(output_file, O_WRONLY | O_TRUNC | O_CREAT, S_IRWXU);
      if (fd < 0)
      {
        write(STDERR_FILENO, error_message, strlen(error_message));
        return;
      }
      // Assign fd to stdout and stderr, so all messages will be redirected to the file,
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd); // Close the original fd
    }
    execv(executable, args);
    write(STDERR_FILENO, error_message, strlen(error_message));
    exit(1);
  }
  free(executable);
}
void builtin(char **args, int args_count, char *paths[], size_t *path_counter)
{
  if (strcmp(args[0], "exit") == 0)
  {
    if (args_count > 1)
    {
      write(STDERR_FILENO, error_message, strlen(error_message));
      return;
    }
    exit(0);
  }
  else if (strcmp(args[0], "cd") == 0)
  {
    if (args_count != 2)
    {
      write(STDERR_FILENO, error_message, strlen(error_message));
      return;
    }
    else if (chdir(args[1]) != 0) // Error in changing directory
    {
      write(STDERR_FILENO, error_message, strlen(error_message));
    }
  }
  else if (strcmp(args[0], "path") == 0)
  {
    clear_path(paths, path_counter);
    int i;
    for (i = 1; i < args_count; i++)
    {
      paths[(*path_counter)] = strdup(args[i]); // Don't check, that is for find_executable
      (*path_counter)++;
    }
  }
}
/*
string: Entire Line
paths: all the potential paths (could be invalid)
path_counter: number of paths

*/
void process_line(char *string, char *paths[], size_t *path_counter, bool interactive)
{
  char *commands[MAX_COMMAND];
  int command_count = 0;
  char *command_token;
  char *saveptr1;
  // Check to see if there are multiple commands
  string[strcspn(string, "\n")] = 0;
  command_token = strtok_r(string, "&", &saveptr1);
  while (command_token != NULL && command_count < MAX_COMMAND)
  {
    commands[command_count++] = command_token;
    command_token = strtok_r(NULL, "&", &saveptr1);
  }

  pid_t children[MAX_COMMAND] = {0}; // Children Arr
  int child_count = 0;

  // For every command, we execute them
  int cmd = 0;
  for (; cmd < command_count; cmd++)
  {
    int args_count = 0;
    char *args[MAX_ARGS]; // command  + arguments
    char *args_token;
    char *saveptr2;
    bool redirection = false; // If redirection
    char *output_file = NULL;

    args_token = strtok_r(commands[cmd], " \t\n", &saveptr2);
    while (args_token != NULL && args_count < MAX_ARGS - 1)
    {
      if (strcmp(args_token, ">") == 0) // In this very command, there is a redirection
      {
        // Need to check if there is another argument to serve as the output file
        redirection = true;
        args_token = strtok_r(NULL, " \t\n", &saveptr2);
        // If there's a file path
        if (args_token != NULL)
        {
          output_file = args_token;
          args_token = strtok_r(NULL, " \t\n", &saveptr2);
          if (args_token != NULL) // Additional Path??
          {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return;
          }
        }
        else
        {
          write(STDERR_FILENO, error_message, strlen(error_message));
          return;
        }
        // If redirection, then we are done with this command
        break;
      }
      //
      args[args_count++] = args_token;
      args_token = strtok_r(NULL, " \t\n", &saveptr2);
    }

    args[args_count] = NULL;
    // If there's zero arg, just go to the next round.
    if (args_count == 0)
      continue;
    if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "cd") == 0 || strcmp(args[0], "path") == 0)
    {
      builtin(args, args_count, paths, path_counter);
    }
    else
    {
      pid_t pid = fork();
      if (pid == 0)
      {
        if (redirection)
        {
          int fd = open(output_file, O_WRONLY | O_TRUNC | O_CREAT, S_IRWXU);
          if (fd < 0)
          {
            write(STDERR_FILENO, error_message, strlen(error_message));
            exit(1);
          }
          // Assign fd to stdout and stderr, so all messages will be redirected to the file,
          dup2(fd, STDOUT_FILENO);
          dup2(fd, STDERR_FILENO);
          close(fd); // Close the original fd
        }
        char *executable = find_executable(args[0], paths, path_counter);
        if (executable == NULL)
        {
          write(STDERR_FILENO, error_message, strlen(error_message));
          exit(1);
        }
        execv(executable, args);
        write(STDERR_FILENO, error_message, strlen(error_message));
        exit(1);
      }
      else if (pid > 0)
      {
        children[child_count++] = pid;
      }
      else
      {
        write(STDERR_FILENO, error_message, strlen(error_message));
      }
    }
  }
  // Waiting for all the children
  int i;
  for (i = 0; i < child_count; i++)
  {
    waitpid(children[i], NULL, 0);
  }
  if (interactive && strcmp(args[0], "exit") == 0)
  {
    printf("dash> ");
    fflush(stdout);
  }
}

int main(int argc, char *argv[])
{
  char *paths[MAX_PATH] = {strdup("/bin")}; // Initialize with /bin
  size_t path_counter = 1;                  // Path count

  if (argc > 2)
  {
    write(STDERR_FILENO, error_message, strlen(error_message));
    exit(1);
  }
  else if (argc == 2) // This is batch mode
  {
    FILE *batch = fopen(argv[1], "r");
    if (batch == NULL)
    {
      write(STDERR_FILENO, error_message, strlen(error_message));
      exit(1);
    }
    char *string = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&string, &len, batch)) != -1) // For every line, treat it as an keyboard input + ENTER
    {
      process_line(string, paths, &path_counter);
    }
    free(string);
    fclose(batch);
  }
  else
  {
    while (1)
    {
      printf("dash >");
      fflush(stdout);
      char *string = NULL;
      size_t len = 0;
      ssize_t read;

      if ((read = getline(&string, &len, stdin)) == -1)
      {
        if (feof(stdin))
        {
          printf("\n");
          exit(0);
        }

        write(STDERR_FILENO, error_message, strlen(error_message));
        exit(1);
      }
      process_line(string, paths, &path_counter, true);
      free(string);
    }
  }
  clear_path(paths, &path_counter);
  return 0;
}
