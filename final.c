#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_COMMAND 1024
#define MAX_ARGS 1024
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
  char *command_token = NULL;
  // Check to see if there are multiple commands
  string[strcspn(string, "\n")] = 0;
  if (string[0] == '&')
  {
    write(STDERR_FILENO, error_message, strlen(error_message));
    return;
  }
  // For & sign inbetween a word
  char *current = string; // starting from 0
  while (*current != '\0' && command_count < MAX_COMMAND - 1)
  {                                   // Don't go out of bound and not terminal
    char *end = strchr(current, '&'); // Search for '&'
    if (end == NULL)
    {
      command[command_count++] = current; // It means no '&'
    }
    *end = '\0';        // Got some result and change it
    if (end == current) // If they happened to be the same, then there's something wrong, zero input
    {
      write(STDERR_FILENO, error_message, strlen(error_message));
      return;
    }
    commands[command_count++] = current; // A valid result from start -----\0
    current = end + 1;
  }

  pid_t children[MAX_COMMAND] = {0}; // Children Arr
  int child_count = 0;
  // For every command, we execute them
  int cmd = 0;
  for (; cmd < command_count; cmd++)
  {
    int args_count = 0;
    char *args[MAX_ARGS];     // command  + arguments
    bool redirection = false; // If redirection
    char *output_file = NULL;

    char *current_arg = commands[cmd];                        // First Letter
    while (*current_arg != '\0' && args_count < MAX_ARGS - 1) // '\0' end of the string, and not out of bound
    {
      while (*current_arg == ' ' || *current_arg == ' \t') // Disregard all the spaces
      {
        current_arg++;
      }
      if (*current_arg == '>')
      {
        if (redirection || args_count == 0) // If there's a previous redirection '>>' or no src
        {
          write(STDERR_FILENO, error_message, strlen(error_message));
          return;
        }

        redirection = true;
        current_arg++;
        // looking for a dst
        while (*current_arg == ' ' || *current_arg == ' \t')
        {
          current_arg++;
        }
        // IF the first thing comes after blanks is terminate sign
        if (*current_arg == '\0')
        {
          write(STDERR_FILENO, error_message, strlen(error_message));
          return;
        }
        output_file = current_arg; // Redirect file starts from here
        break;
      }
      args[args_count++] = current_arg;

      // At this point, it is a command_+'>'+redirect_file
      // first part command
      while (*current_arg != ' ' && *current_arg != '\t' && *current_arg != '>' && *current_arg != '\0')
      {
        current_arg++;
      }
      if (*current_arg == '>') // Go back to the beginning and do redirect_file part
        continue;

      if (*current_arg != '\0')
      {
        *current_arg = '\0';
        current_arg++;
      }
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
      process_line(string, paths, &path_counter, false);
    }
    free(string);
    fclose(batch);
  }
  else
  {
    while (1)
    {
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
