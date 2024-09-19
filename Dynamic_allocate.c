#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>

// Function to clear the PATH
void clearPath(char* paths[], size_t* path_counter) {
  int j = 0;
  for (; j < *path_counter; j++) {
    free(paths[j]);  // Free each allocated path
    paths[j] = NULL;
  }
  *path_counter = 0;
}

// Function to find the executable in the provided paths
char* find_executable(char* command, char* paths[], size_t counter) {
  char full_path[100];
  int i = 0;
  for (; i < counter; i++) {
    snprintf(full_path, sizeof(full_path), "%s/%s", paths[i], command);  // Construct the full path
    if (access(full_path, X_OK) == 0) {  // Check if the command is executable
      return strdup(full_path);  // Return a copy of the full path
    }
  }
  return NULL;  // Return NULL if not found
}


int main(int argc, char* argv[]) {
  char* paths[20] = { strdup("/bin"), NULL };  // Initialize with /bin
  size_t path_counter = 1;  // Path count

  while (1) {
    printf("Dash> ");
    char* string = NULL;
    size_t len = 0;
    ssize_t read;
    char* savePointer1;
    char* command_token;
    const char* command_delim = "\n \t";
    //char* myArgs[20];
    int i = 0;
    //For redirection
    bool redirection = false;
    size_t redirection_pos;
    char* file_path;

    // Read user input
    read = getline(&string, &len, stdin);
    if (read == -1) {
      perror("Error reading input");
      exit(-1);
    }

    //Token counter
    char* cpy = strdup(string);
    command_token = strtok_r(cpy, command_delim, &savePointer1);
    while (command_token != NULL) {
      i++;
      command_token = strtok_r(NULL, command_delim, &savePointer1);
    }
    free(cpy);
    //Dynamic Allocate
    char** myArgs = malloc((i+1) * sizeof(char*)); 
    int counter = 0;

    command_token = strtok_r(string, command_delim, &savePointer1);
    while (command_token != NULL) {
      if (counter < i) {
        if(strcmp(command_token, ">") == 0){
          redirection = true;
          redirection_pos = counter;
        }
        myArgs[counter++] = command_token;
      }
      command_token = strtok_r(NULL, command_delim, &savePointer1);
    }
    myArgs[counter] = NULL;
//--------------------------------------------------------------------------------
    if (i == 0) {
      free(string);
      continue;  // No command entered, continue
    }

    // Handle exit command
    if (strcmp(myArgs[0], "exit") == 0 && i == 1) {
      free(string);
      clearPath(paths, &path_counter);  // Clear PATH
      exit(0);
    }

    // Handle cd command (built-in)
    if (strcmp(myArgs[0], "cd") == 0) {
      if (i != 2) {
        perror("Incorrect cd args");
        free(string);
        continue;
      }
      if (chdir(myArgs[1]) != 0) {  // Use chdir() to change directory
        perror("Change directory failed");
      }
      free(string);
      continue;
    }

    // Handle path command
    if (strcmp(myArgs[0], "path") == 0) {
      if (i == 1) {
        clearPath(paths, &path_counter);  // Clear all paths
      } else {
        char* temp[20];
        int j = 0;
        bool good = true;

        while (j < i - 1) {
          temp[j] = strdup(myArgs[j + 1]);  // Dynamically allocate each path
          if (access(temp[j], F_OK) != 0) {  // Check if the path exists
            perror("Invalid path");
            good = false;
            break;
          }
          j++;
        }

        if (good) {
          clearPath(paths, &path_counter);  // Clear old paths
          int k = 0;
          for (; k < j; k++) {
            paths[k] = temp[k];  // Add new paths
            path_counter++;
          }
        } else {
          int k = 0;
          for (; k < j; k++) {
            free(temp[k]);  // Free allocated paths on error
          }
        }
      }
      free(string);
      continue;
    }


    // Find the executable
    char* executable = find_executable(myArgs[0], paths, path_counter);
    int rc = fork();
    if (rc < 0) {
      perror("Fork failed");
      exit(1);
    } else if (rc == 0) {  // Child process
      int fd;
      if(redirection){
        if(myArgs[redirection_pos + 1] != NULL){
          fd = open(myArgs[redirection_pos + 1], O_WRONLY|O_TRUNC|O_CREAT, S_IRWXU);
          if(fd < 0){
            perror("Error in opening file");
            exit(1);
          }
          dup2(fd,STDOUT_FILENO);
          close(fd);
          myArgs[redirection_pos] = NULL;
          execv(executable, myArgs);
        }
        else
        {
          perror("No file");
          exit(1);
        }
      }

      if (executable != NULL) {
        execv(executable, myArgs);  // Execute the command
        perror("execv failed");  // If execv fails
      } else {
        printf("Command not found in provided paths\n");
      }
      exit(1);
    } else {
      wait(NULL);  // Wait for child process to complete
    }

    if (executable != NULL) {
      free(executable);
    }
    free(string);
    free(myArgs);
  }

  return 0;
}
