/***************************************
 * Name: Robert Rouleau
 * Date: 5/22/22
 * Program 3: Smallsh
***************************************/
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#define MAX_LENGTH 2048
#define ARG_SIZE 512

pid_t* backgroundProcesses;         // array of background process id's
int* numBackgroundProcesses;        // number of background processes started
pid_t* foregroundProcess;
int runBackground = 1;              // If runBackground is set to 1, background processes can be run. If 0, they are restricted to foreground.
int exitStatus = 0;

void smallsh();

struct command{
    char** args;
    int argc;
    char* inputFile;
    char* outputFile;
    int background;
};

/*Handle SIGINT in child foreground processes*/
void foreground_SIGINT(int num){
    exit(0);
}

/*Catch SIGTSTP and change value of runBackground*/
void smallsh_SIGTSTP(int num){
    char* msg1 = "Entering foreground-only mode (& is now ignored)\n";
    char* msg2 = "Exiting foreground-only mode\n";
    int pStatus;
    pid_t foregroundPid = waitpid(*foregroundProcess, &pStatus, 0);

    if(runBackground == 1){
        write(STDOUT_FILENO, msg1, 49);
        runBackground = 0;
    } else{
        write(STDOUT_FILENO, msg2, 29);
        runBackground = 1;
    }
    smallsh();
}

/****
 * Setup the signals for the parent smallsh process
 * This function references Exploration: Signal Handling API.
****/
void smallsh_signals(){
  struct sigaction SIGTSTP_action = {0}, SIGINT_action = {0};
  // Register smallsh_SIGTSTP as the signal handler
  SIGTSTP_action.sa_handler = smallsh_SIGTSTP;

  // Block all catchable signals while smallsh_SIGTSTP is running.
  sigemptyset(&SIGTSTP_action.sa_mask);
  // No flags set
  SIGTSTP_action.sa_flags = SA_NODEFER;
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);
  
  // Ignore SIGINT
  SIGINT_action.sa_handler = SIG_IGN;
  sigaction(SIGINT, &SIGINT_action, NULL);
}

/*
// Allocate memory and initialize all members of command struct
*/
struct command* initCommand(){
    int bufferSize = ARG_SIZE + 1;

    struct command *smallsh_command = malloc(sizeof(struct command));

    smallsh_command->args = calloc(bufferSize, sizeof(char*));
    smallsh_command->args[0] = calloc(MAX_LENGTH, sizeof(char));
    memset(smallsh_command->args[0], '\0', MAX_LENGTH);

    smallsh_command->argc = 0;

    smallsh_command->inputFile = calloc(MAX_LENGTH, sizeof(char));
    memset(smallsh_command->inputFile, '\0', MAX_LENGTH);

    smallsh_command->outputFile = calloc(MAX_LENGTH, sizeof(char));
    memset(smallsh_command->outputFile, '\0', MAX_LENGTH);

    smallsh_command->background = 0;

    return smallsh_command;
}

/*
// Free all memory associated with a command
*/
void freeCommand(struct command* smallsh_command){
    if(smallsh_command != NULL){
            int i;

            if(smallsh_command->args[0] != NULL){
                free(smallsh_command->args[0]);
            }

            for(i = 1; i < smallsh_command->argc; i++){
                free(smallsh_command->args[i]);
            }

            free(smallsh_command->args);
            free(smallsh_command->inputFile);
            free(smallsh_command->outputFile);
            free(smallsh_command);
        }
}

/*
// Get the user's input using getline().
// Takes no parameters and returns the user's input as a char*.
*/
char* getInput(){
    char* input = NULL;
    ssize_t len = 0;

    printf(":");
    fflush(stdout);
    
    // Read a line from stdin, if an error occurs or eof is reached without any bytes read, exit
    if(getline(&input, &len, stdin) == -1){
        return NULL;
    }

    return input;
}

/*
// Expand $$
// Takes a char* as output parameter and replaces every instance of '$$' with the pid
// Referenced page: https://stackoverflow.com/questions/53311499/c-replace-in-string-with-id-from-getpid/53311604
*/
void expandVar(char* userInput){
    int len = strlen(userInput);
    int i;
    int finished = 0;

    // duplicate the input because it is immutable
    char* formattedInput = strdup(userInput);

    // create a buffer to print the formatted input to
    char *expandedInput = calloc(MAX_LENGTH, sizeof(char));

    // Loop until finished is set to 1
    while(!finished){
        // scan formattedInput for the first instance of '$$' and replace it with '%d" then break from for loop
        for (i = 0; i < len; i++) {
            if (formattedInput[i] == '$' && formattedInput[i+1] == '$') {
                formattedInput[i] = '%';
                formattedInput[i+1] = 'd';
                break;
            }
        }

        // If the previous for loop scanned the entired formattedInput set finished to 1 and do not loop again.
        if(i == len){
            finished = 1;
        }

        // write the formatted input to the buffer, replacing '%d' with the pid
        sprintf(expandedInput, formattedInput, getpid());

        // Free formatted input before strdup because strdup allocates new memory
        free(formattedInput);
        formattedInput = strdup(expandedInput);

        // update length to the new length of formattedInput that has a new pid inserted
        len = strlen(formattedInput);    
    }

    // copy the expandedInput to the output parameter userInput.
    strcpy(userInput, expandedInput);

    // free the memory that is no longer needed
    free(formattedInput);
    free(expandedInput);
}

/*
// Parse the user's input using strtok_r().
// The input should consists of a command followed by arguments.
// input can contain input and output redirection characters.
// If a redirection character is detected, the next argument is the file being redirected to.
// Input and output redirection can be combined in no specific order.
// If the last argument is '&' the process will be run in the background.
// If input is blank or begins with #, it is treated as a comment.
*/
struct command* parseInput(char* input){
    // setup for str_tok()
    char *saveptr;
    char *token;
    int charCount = 0;
    int lastChar = strlen(input) - 1;
    
    // Declare and initialize smallsh_command and its members
    struct command* smallsh_command;
    smallsh_command = initCommand();
    
    // If the first token starts with "#" or is '\n' (user entered blank line) then return
    if((strncmp(input, "#", 1) == 0) || (strncmp(input, "\n", 1) == 0)){
        return smallsh_command;
    } else {
        // remove the '\n' from the end of the input
        char *newInput = calloc(MAX_LENGTH, sizeof(char));
        memset(newInput, '\0', MAX_LENGTH);
        strncpy(newInput, input, strlen(input) - 1); 
        free(input);
        input = newInput;

        // expand '$$' to pid
        expandVar(input);

        // get the first token
        token = strtok_r(input, " ", &saveptr);
        charCount += strlen(token);

        // args[0] is allocated memory already from initCommand()
        strcpy(smallsh_command->args[0], token);
        smallsh_command->argc += 1;
    }

    // Get tokens until token is NULL.
    token = strtok_r(NULL, " ", &saveptr);
    if(token != NULL){
        charCount += strlen(token) + 1;
    }
    
    while(token != NULL){
        if(strncmp(token, "<", 1) == 0){                                // if the token is "<", get the next token and set it to input file
            token = strtok_r(NULL, " ", &saveptr);
            charCount += strlen(token) + 1;
            strcpy(smallsh_command->inputFile, token);        
        } else if(strncmp(token, ">", 1) == 0){                         // if the token is ">", get the next token and set it to output file            
            token = strtok_r(NULL, " ", &saveptr);
            charCount += strlen(token) + 1;
            strcpy(smallsh_command->outputFile, token);
        } else {                                        
            if((strcmp(token, "&") == 0) && (charCount == lastChar)){   // if the token is "&" and also the last char of input, set runBackground to 1
                if(runBackground == 1){
                    // flag the command to be run in the background unless background processes are restricted
                    smallsh_command->background = 1;
                } else {
                    smallsh_command->background = 0;
                }                
            } else{
                // if the token is not "<", ">", or "&", set it to the next index of args 
                smallsh_command->args[smallsh_command->argc] = calloc(MAX_LENGTH, sizeof(char));
                strcpy(smallsh_command->args[smallsh_command->argc], token);
                smallsh_command->argc += 1;
            }            
        }
        // get the next token in the input
        token = strtok_r(NULL, " ", &saveptr);
        if(token != NULL){
            charCount += strlen(token) + 1;
        }        
    }

    free(input);
    return smallsh_command; 
}

/*
// Exit smallsh
// frees dynamic memory
// calls exit() to end smallsh
*/
void smallsh_exit(struct command* smallsh_command){
    freeCommand(smallsh_command);
    free(numBackgroundProcesses);
    free(backgroundProcesses);
    free(foregroundProcess);
    exit(EXIT_SUCCESS);
}

/*
// Change the working directory of smallsh
// takes a path to the new directory as a char* argument
// If no arg is given, it changes working directory to the HOME directory
*/
void smallsh_cd(char **args){
    if(args[1] == NULL){
        chdir(getenv("HOME"));
    } else {
        chdir(args[1]);
    }
}

/*
// Print the exit status of the last foreground process that completed.
// If no processes have been run since smallsh was started, exitStatus will be 0.
*/
void smallsh_status(int exitStatus){
    if(exitStatus > 1){
        exitStatus = 1;
    }
    printf("exit value %d\n", exitStatus);
    fflush(stdout);
}

/*
// Runs the user's command, first checking if it is a builtin.
// If a command requests a builtin then it runs the function without forking and running exec().
// If a command is not a built in, it forks and executes the command in a new process.
// Takes a command as input and returns either 0 on success and 1 on failure.
// This function references and adapts code from Exploration: Signal Handling API, Exploration: Processes and I/O and https://brennan.io/2015/01/16/write-a-shell-in-c/.
*/
int runCommand(struct command* smallsh_command, pid_t* backgroundProcesses, int* numBackgroundProcesses){
    
    int i;
    char* builtins[] = {"exit", "cd", "status"};

    // If argc is 0, this is a comment or blank line and the function returns
    if(smallsh_command->argc == 0){
        return 0;
    }

    // Check if the command is a builtin function and call the function if it is, otherwise execute as a non-builtin command
    if(strncmp(smallsh_command->args[0], builtins[0], strlen(builtins[0])) == 0){
        smallsh_exit(smallsh_command);
        } else if(strncmp(smallsh_command->args[0], builtins[1], strlen(builtins[1])) == 0){
            smallsh_cd(smallsh_command->args);
            } else if(strncmp(smallsh_command->args[0], builtins[2], strlen(builtins[2])) == 0){
                smallsh_status(exitStatus);
            } else {
                
                int childStatus;
                int childPid;
                struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

                // size of newargv is number of arguments plus one more element to hold NULL where the first argument is the command being executed
                char **newargv = malloc((smallsh_command->argc + 1) * sizeof(char*)); 
                for(i = 0; i < (smallsh_command->argc + 1); i++){
                    newargv[i] = malloc(MAX_LENGTH * sizeof(char));
                }
                childPid = fork();                
                switch (childPid){
                    case -1:
                        // The fork failed and program will quit.
                        perror("fork() failed!");
                        exit(1);
                        break;
                    case 0:
                        // Case 0 is the child process
                        // Ignore SIGTSTP whether foreground or background
                        SIGTSTP_action.sa_handler = SIG_IGN;
                        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

                        if(smallsh_command->background == 0){
                            // Register foreground_SIGINT as the signal handler
                            SIGINT_action.sa_handler = foreground_SIGINT;
                            // block all catchable signals while foreground_SIGINT is running.
                            sigemptyset(&SIGINT_action.sa_mask);
                            // No flags set
                            SIGINT_action.sa_flags = 0;
                            sigaction(SIGINT, &SIGINT_action, NULL);

                        } else if(smallsh_command->background == 1){
                            // Ignore SIGINT
                            SIGINT_action.sa_handler = SIG_IGN;
                            sigaction(SIGINT, &SIGINT_action, NULL);

                            // if no input file was specified for the background process, redirect input to /dev/null
                            if(strlen(smallsh_command->inputFile) < 1){
                                int inputFd = open("/dev/null", O_RDONLY, 0640);
                                if(inputFd == -1){
                                    perror("open()");
                                    exitStatus = 1;
                                    for(i = 0; i < (smallsh_command->argc + 1); i++){
                                        free(newargv[i]);
                                    }
                                    free(newargv);
                                    exit(1);                                
                                }
                                int directInput = dup2(inputFd, 0);
                                fcntl(inputFd, F_SETFD, FD_CLOEXEC);
                            }
                            // if no output file was specified for the background process, redirect output to /dev/null
                            if(strlen(smallsh_command->outputFile) < 1){
                                int outputFd = open("/dev/null", O_RDONLY, 0640);
                                if(outputFd == -1){
                                    perror("open()");
                                    exitStatus = 1;
                                    for(i = 0; i < (smallsh_command->argc + 1); i++){
                                        free(newargv[i]);
                                    }
                                    free(newargv);
                                    exit(1);                                
                                }
                                int directOutput = dup2(outputFd, 0);
                                fcntl(outputFd, F_SETFD, FD_CLOEXEC);
                            }
                        }

                        // If there is a str assigned to inputFile, open or create the file and redirect stdin to the file
                        if(strlen(smallsh_command->inputFile) > 0){
                            int inputFd = open(smallsh_command->inputFile, O_RDONLY, 0640);
                            if(inputFd == -1){
                                perror("open()");
                                exitStatus = 1;
                                for(i = 0; i < (smallsh_command->argc + 1); i++){
                                    free(newargv[i]);
                                }
                                free(newargv);
                                exit(1);                                
                            }
                            int directInput = dup2(inputFd, 0);
                            fcntl(inputFd, F_SETFD, FD_CLOEXEC);
                        }

                        // If there is a str assigned to outputFile, open or create the file and redirect stdout to the file
                        if(strlen(smallsh_command->outputFile) > 0){
                            int outputFd = open(smallsh_command->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0640);
                            if(outputFd == -1){
                                perror("open()");
                                exitStatus = 1;
                                for(i = 0; i < (smallsh_command->argc + 1); i++){
                                    free(newargv[i]);
                                }
                                free(newargv);
                                exit(1);                                
                            }
                            int directOutput = dup2(outputFd, 1);
                            fcntl(outputFd, F_SETFD, FD_CLOEXEC);
                        }

                        // Fill an array with the smallsh arguments to pass as argument for execvp()
                        for (i = 0; i < smallsh_command->argc; i++){
                            newargv[i] = smallsh_command->args[i];
                        }

                        // execvp() requires the last element of the array to be NULL
                        newargv[smallsh_command->argc] = NULL;
                        execvp(smallsh_command->args[0], newargv);
                        perror(smallsh_command->args[0]);

                        // If execvp() returns there was an error
                        for(i = 0; i < (smallsh_command->argc + 1); i++){
                            free(newargv[i]);
                        }
                        free(newargv);

                        // exit with status code 1
                        exit(1);
                        break;
                    default:
                        // The fork was successfull. This is parent process.
                        // If background is false, wait for the child to finish, otherwise continue with process running in background
                        if(smallsh_command->background == 0){
                            *foregroundProcess = childPid;
                            pid_t pidNumb = waitpid(childPid, &childStatus, 0);
                            if(pidNumb > 0){
                                if(WIFSIGNALED(childStatus)){
                                    printf("foreground process %d terminated by signal: %d\n", pidNumb, WTERMSIG(childStatus));
                                }
                            }
                            exitStatus = childStatus; // set the exit status of the last foreground process that terminated
                        } else {
                            // Print the pid of the new background process
                            printf("background pid is %d\n", childPid);
                            fflush(stdout);
                            
                            exitStatus = 0;
                            backgroundProcesses[*numBackgroundProcesses] = childPid; // store the pid of the new background process so that its status can be tracked
                            (*numBackgroundProcesses)++;
                        }

                        // free the memory used in runCommand()
                        for(i = 0; i < (smallsh_command->argc + 1); i++){
                            free(newargv[i]);
                        }
                        free(newargv);
                        break;
                }
            }
    return 0;
}

void smallsh(){
    int loop = 1;
    int childStatus;
    int childPid;
    int i;

    // Loop until exit command is called
    smallsh_signals();
    while(loop){
        // Check if any of the background processes finished
        for(i = 0; i < *numBackgroundProcesses; i++){
            childPid = waitpid(backgroundProcesses[i], &childStatus, WNOHANG);
            if(childPid > 0){
                if(WIFEXITED(childStatus)){
                    printf("background process %d terminated: exit status %d\n", childPid, childStatus);
                } else {
                    printf("background process %d terminated by signal: %d\n", childPid, WTERMSIG(childStatus));
                }
                fflush(stdout);
            }
        }

        // get input
        char* input = getInput();
        
        // parse input
        struct command *newCommand = NULL;
        newCommand = parseInput(input);

        // execute command
        runCommand(newCommand, backgroundProcesses, numBackgroundProcesses);  

        // clean up memory
        freeCommand(newCommand);
    }
}

int main(){
    // Allocate memory for global variables used to hold background process pid and number of background processes
    backgroundProcesses = calloc(1000, sizeof(pid_t));
    numBackgroundProcesses = malloc(sizeof(int));
    foregroundProcess = malloc(sizeof(pid_t));
    
    // Call the main loop for smallsh
    smallsh();

    // Free memory allocated for global variables
    free(numBackgroundProcesses);
    free(backgroundProcesses);
    free(foregroundProcess);

    return EXIT_SUCCESS;
}