#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <regex.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

// path executable minibash in $PATH, so that it can be executed from anywhere

#define MIN_ARGS 2
#define MAX_ARGS 16
#define SPECIAL_COMMANDS 6
#define SPECIAL_CHAR 10
#define MAX_PARAMETERS 3 // excluding the command

// some operations of minibash
char *custom_commands[7] = {"cd", "dter", "dtex", "addmb", "exit", "fore", "clear"};
// below variable maps to above array
int selected_custom_command = -1;

// special characters of minibash
char *special_chars[] = {
    "#",
    "+",
    "<",
    ">",
    ">>",
    "~",
    ";",
    "|",
    "&&",
    "||"};

// below variable maps to above array
int selected_special_char = -1;

// number of special characters
int special_char_num = 0;

// boolean to find out if special characters exist or not
bool is_special_char = false;

bool is_conditional = false; // used if && or || exist

bool is_multiple_conditional = false; // used if && || both exist in input

// to hold all the conditionals if multiple_conditional is true
// false value represents && and true value represents ||
bool multiple_conditionals_sequence[] = {false, false, false};

// default delimiters for tokenization in any given string
char *default_delimiters = "\n\t\r\v\f ";

// will hold arguments to all the commands in a program
char *all_commands[5]; // used when is_special_char = true
char *command_1[5];
char *command_2[5];
char *command_3[5];
char *command_4[5];

// to store all pointers in array
char ***all_commands_pointer;

// to keep track of background process ids
int *background_processes_pids;

// holds standard input's & output's file descriptor
int stdin_fd_backup, stdout_fd_backup;

// define some functions
int fork_and_run(char *command[], char *input);
int find_size();
int get_index_and_shift(int pid);
int get_index(int pid);

// SOME UTILITES
// kill all background processes
void kill_all_background_processes()
{
    int size = find_size();

    // loop and kill all background processes
    for (int i = 0; i < size; i++)
    {
        kill(background_processes_pids[i], SIGKILL);
    }
}

// after each iteration or error case, reset things
void reset(char *input)
{
    fflush(stdin);
    free(input);
    command_1[0] = NULL;
    command_2[0] = NULL;
    command_3[0] = NULL;
    command_4[0] = NULL;
    all_commands[5] = NULL;
    all_commands_pointer = NULL;
}

// handle SIGCONT, used in send_to_background in child to connect input to stdin when fore is called
void handle_sigcont()
{
    dup2(stdin_fd_backup, 0); // redirect input back to stdin
}

// handle signal SIGCHLD
// when a child background process is done, print process done
void handle_sigchld()
{
    int pid;
    int status;

    // loop to get all sigchld if all exit at the same time
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (WIFEXITED(status))
        {
            if (WEXITSTATUS(status) == 4)
            {
                printf("minibash: command not found\n");
            }
            else if (get_index(pid) != -1)
            {
                printf("Background Process [%d]+ with pid %d is done.\n", get_index_and_shift(pid), pid);
            }
        }
        else if (WIFSIGNALED(status))
        {
            printf("Background Process %d Exited with Signal Number:%d\n", pid, WTERMSIG(status));
        }
    }
}

// handle sigint
// kills all background processes
void handle_sigint()
{
    kill_all_background_processes();
    exit(0);
}

// PART 1: Take Input, Parse Input -  Functions
// removes trailing or leading whitespaces
// replaces tabspaces in between with whitespace
void fix_input(char *input)
{
    // remove leading whitespaces
    int i = 0, j = 0;

    // Skip leading whitespace and tabs
    while (input[i] != '\0' && input[i] == '\t')
    {
        i++;
    }

    if (strlen(input) > 0)
    {
        // Shift the string left
        while (input[i] != '\0')
        {
            input[j++] = input[i++];
        }
        input[j] = '\0';
    }

    if (strlen(input) > 0)
    {
        // replace tabspaces with whitespaces
        for (int i = 0; input[i] != '\0'; i++)
        {
            if (input[i] == '\t')
            {
                input[i] = ' ';
            }
        }
    }

    if (strlen(input) > 0)
    { // remove trailing whitespaces
        while (i >= 0 && input[i] == '\t')
        {
            input[i--] = '\0';
        }
    }
}

// this function will check input and validate that the string using regex
// returns -1 if not valid, returns 1 if valid, exits program if compilation of regex fails
int check_input(char *input)
{
    const char *pattern = "^[a-zA-Z0-9 .\"'#~|;>$*(){}^@!<&+-_=\\t]*$";

    regex_t regex;
    int is_input_valid;

    // Compile the regular expression
    is_input_valid = regcomp(&regex, pattern, REG_EXTENDED);
    if (is_input_valid)
    {
        fprintf(stderr, "Could not compile regex\n"); // print to stdout
        exit(-1);
    }
    is_input_valid = regexec(&regex, input, 0, NULL, 0);

    if (!is_input_valid)
    {
        return 1;
    }
    else if (is_input_valid == REG_NOMATCH)
    {
        return -1;
    }
    else
    {
        char msgbuf[100];
        regerror(is_input_valid, &regex, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
        exit(-1);
    }
}

// this function will validate that there's no different special characters before or after a ">>", "||" OR "&&" is found
// skip index will skip to check the index in special_char array along with selected_special_character to find special character
// returns -1 if there are different special characters than the one that has already been selected
// returns 1 if there's no other special characters found
int validate_special_char_singularity(char *input, int skip_index)
{ // special characters after selected_special_char
    // copy input and skip till first instance of selected_special_char
    char *input_1 = input;
    input_1 = strstr(input_1, special_chars[selected_special_char]); // find first instance of selected_special_char
    input_1 = input_1 + 2;                                           // skip 2 chars from the found index (2 because this function will only be used when selected_special_char == >> or && or ||)

    // loop over to find any special character element before selected_special_char
    for (int i = 0; i < 10 && i != selected_special_char && i != skip_index; i++)
    {
        char *p = strstr(input_1, special_chars[i]);

        if (p != NULL)
        {
            // i = 7 i.e. |, selected_special_char = 9 i.e. ||
            if (selected_special_char == 9 && i == 7)
            {

                int position = p - input_1;
                // if || is found after || allow it
                if (input_1[position + 1] == special_chars[i][0])
                {
                    continue;
                }
            }
            printf("Can't have '%s' before %s \n", special_chars[selected_special_char], special_chars[i]);
            return -1;
        }
    }

    // any special characters before selected_special_char
    char *input_2 = input;
    input_2 = strstr(input_2, special_chars[selected_special_char]); // find first instance of selected_special_char

    int position = input_2 - input; // get index of first instance of selected_special_char

    // get substring before the first apperance of selected_special_char
    char *input_3 = malloc(sizeof(char) * strlen(input));
    strncpy(input_3, input, position - 1);

    // loop over to find any other special character element after selected_special_char
    for (int i = 0; i < 10 && i != selected_special_char && i != skip_index; i++)
    {
        if (strstr(input_3, special_chars[i]) != NULL)
        {
            printf("Can't have special character '%s' after %s \n", special_chars[selected_special_char], special_chars[i]);
            return -1;
        }
    }
    return 1; // if program is here it means that no other special characters have been found
}

// find which special character is being used and checks that there are no other special characters except for conditionals
// associated with checking the syntax
// check if there aren't any 2 different special characters except for conditionals
// returns -1 if special char exists and has error
// returns 1 if special char does not exists
int find_special_char(char *input)
{
    // reset bools and vars
    selected_special_char = -1;
    is_conditional = false;
    is_special_char = false;
    is_multiple_conditional = false;

    int diff_special_char = 0;

    int length = strlen(input);

    // find selected_special_char
    for (int i = 0; i < 10; i++)
    {
        if (strstr(input, special_chars[i]) != NULL)
        {
            selected_special_char = i;
            is_special_char = true;
            diff_special_char += 1; // increment diff counter
        }
    }

    // if conditional i.e. && or || then set boolean to be true
    if (selected_special_char >= 8)
    {
        is_conditional = true;

        // validates that there is no other special characters than && || in any scenario
        if (selected_special_char == 8)
        {
            return validate_special_char_singularity(input, 9); // skip = 9 because for &&, || is allowed
        }
        else
        {
            return validate_special_char_singularity(input, 8); // skip = 8 because for ||, && is allowed
        }
    }

    // to check if any special characters before and after
    else if (selected_special_char == 4)
    {
        // validates that there is only 1 kind of special character
        return validate_special_char_singularity(input, selected_special_char);
    }

    // for non conditional statements, check if there are more than 1 different special characters
    else if (!is_conditional && diff_special_char > 1)
    {
        printf("Can't have more than 1 different special character in a statement\n");
        return -1;
    }

    return 1;
}

// used in validate_special_char
// find special_char_num for a selected special character
int find_all_special_chars(char *input)
{
    char *p = input;
    int index = 0;

    int length = strlen(special_chars[selected_special_char]);

    // reset
    special_char_num = 0;

    // keep incrementing p by special char length as it's being found
    while ((p = strstr(p, special_chars[selected_special_char])) != NULL)
    {

        special_char_num += 1;
        p += length;
    }

    return special_char_num;
}

// validate for each special character that other special characters doesn't exist and
// number of any given special characters are less than 3
// returns delimiter for tokenization
char *validate_special_char(char *input, char *delimiters)
{
    int ret_value;

    // according to selected_special_char, make delimiters
    if (selected_special_char >= 0 && selected_special_char <= 4)
    {
        // for #,+,<,>,>>
        ret_value = find_all_special_chars(input);
        if (ret_value == 1)
        {
            strcpy(delimiters, special_chars[selected_special_char]);
        }
        else
        {
            printf("Program Only supports upto 1 operation for %s special character\n", special_chars[selected_special_char]);
            return NULL;
        }
    }
    else if (selected_special_char >= 5 && selected_special_char <= 7)
    {
        // for ~ ; |

        ret_value = find_all_special_chars(input);

        if (ret_value >= 1 && ret_value <= 3)
        {
            strcat(delimiters, special_chars[selected_special_char]);
        }
        else
        {
            printf("Program Only supports upto 3 operations for %s special character\n", special_chars[selected_special_char]);
            return NULL;
        }
    }
    else
    {
        // for &&, ||

        // value 1 & 2 will have the number of times && and || occur in input

        // value 1 is the count of original selected character
        // value 2 is the count of the other special character which is allowed

        int value_1, value_2;
        value_1 = find_all_special_chars(input);

        // change special selected character
        if (selected_special_char == 8)
            selected_special_char = 9;
        else
            selected_special_char = 8;

        value_2 = find_all_special_chars(input);
        ret_value = value_1 + value_2;
        special_char_num = ret_value;

        // change it back to original
        if (selected_special_char == 8)
            selected_special_char = 9;
        else
            selected_special_char = 8;

        if (ret_value <= 3 && ret_value > 0)
        {
            strcpy(delimiters, special_chars[8]);
            strcat(delimiters, special_chars[9]);
        }
        else
        {
            printf("Program Only supports upto 3 operations for || and && special characters\n");
            return NULL;
        }

        if (value_2 > 0)
        {
            is_multiple_conditional = true;
            // if there are multiple conditions, there are at most 3 conditionals possible acc to rules
            // 2^3 = 8 permuations possible for 2 characters

            // becaus of is_special_char array order,
            // value 1 will always represent no of || characters
            // value 2 will always represent no of && characters

            char *input_1 = malloc(sizeof(char) * strlen(input));
            char *input_2 = malloc(sizeof(char) * strlen(input));

            strcpy(input_1, input);
            strcpy(input_2, input);

            int i = 0;
            while (i < ret_value)
            {
                // find index of && in input_1
                char *and = strstr(input_1, "&&");
                int and_position = and-input_1; // get index of first instance of &&

                // find index of || in input_2
                char * or = strstr(input_2, "||");
                int or_position = or -input_2; // get index of first instance of ||

                if (!and)
                {
                    and_position = or_position + 1;
                }
                if (! or)
                {
                    or_position = and_position + 1;
                }

                // compare results
                if (and_position < or_position)
                {
                    multiple_conditionals_sequence[i] = false;
                    and_position += 2;

                    int k, j;
                    for (k = and_position, j = 0; k < strlen(input_1) || j <= and_position; k++, j++)
                    {
                        input_1[j] = input_1[k];
                    }
                    input_1[j] = '\0';
                    strcpy(input_2, input_1);
                }
                else
                {
                    multiple_conditionals_sequence[i] = true;
                    or_position += 2;

                    int k, j;
                    for (k = or_position, j = 0; k < strlen(input_2) || j <= or_position; k++, j++)
                    {
                        input_2[j] = input_2[k];
                    }
                    input_2[j] = '\0';

                    strcpy(input_1, input_2);
                }
                i++;
            }
        }
    }

    return delimiters;
}

// parses input and validates that the input is according to the required rules
// returns null on error
// returns delimiters on success
char *input_parsing(char *input)
{
    if (strlen(input) <= 0)
    {
        return NULL;
    }

    fix_input(input); // fix input to fit regular expression

    if (strlen(input) <= 0)
    {
        return NULL;
    }

    // match with regex
    if (check_input(input) == -1)
    {
        printf("Invalid Input, try again!\n");
        return NULL;
    }

    // find what special character is being used
    if (find_special_char(input) == -1)
    {
        return NULL;
    }

    if (selected_special_char > -1)
    {

        // add delimiters according to the option
        int delimiter_length = strlen(special_chars[selected_special_char]) + 2;
        char *delimiters = malloc(sizeof(char) * delimiter_length);

        // validate input for each special character and return delimiters
        return validate_special_char(input, delimiters);
    }
    else
    {
        return default_delimiters;
    }
}

// PART 2: Tokenization & Identification of Commands & it's parameters -  Functions

// will find tokens for given delimiters and change result
// used in tokenize_commands
// index when called in a loop, when there are multiple commands
// returns the number of tokens found on success
// returns -1 if tokens go beyond
int find_tokens(char *string, char *delimiters, char *result[5], int index)
{
    int tokens_cnt = 0;
    char *token;

    // Tokenize via default Delimiters and find arguments of that command
    for (token = strtok(string, delimiters); token; token = strtok(NULL, delimiters))
    {
        // if at any point number of tokens are more than 4, return -1 as error
        if (tokens_cnt > 3)
        {
            return -1;
        }

        // // TODO: Remove later
        // printf("token=%s\n", token);
        // printf("tokenLength=%d\n", strlen(token));

        // allocate memory and copy token to command
        result[tokens_cnt] = malloc(sizeof(char) * strlen(token));
        strcpy(result[tokens_cnt], token);

        tokens_cnt++;
    }
    result[tokens_cnt] = NULL;

    // initalize all_command_pointer's index according to commands
    if (index != -1)
    {
        all_commands_pointer[index] = (char **)malloc(sizeof(char **) * 5);
        all_commands_pointer[index] = result;
    }

    return tokens_cnt;
}

// tokenizes commands and puts into command_1,2,3,4 according to number of special chars in input
// returns -1, if no of parameters exceed beyond 3
// returns 1 on success
int tokenize_commands(char *input, char *custom_delimiters)
{
    int ret_value;
    char *input_backup; // backup of input after parsing

    // input backup
    input_backup = malloc(strlen(input));
    strcpy(input_backup, input);

    if (is_special_char)
    {
        // find tokens according to default
        // get all commands in input string which are divided by custom_delimiters i.e. recieved from input_parsing indicating that special_char exists
        if (find_tokens(input_backup, custom_delimiters, all_commands, -1) == -1)
        {
            return -1;
        }

        // initalize all_commands_pointers
        all_commands_pointer = malloc(sizeof(char **) * 5);

        int i;
        // tokenize all commands into individual commands
        for (i = 0; i < special_char_num + 1; i++)
        {
            switch (i)
            {
            case 0:
                if (find_tokens(all_commands[i], default_delimiters, command_1, i) == -1)
                {
                    return -1;
                }

                break;
            case 1:
                if (find_tokens(all_commands[i], default_delimiters, command_2, i) == -1)
                {
                    return -1;
                }
                break;
            case 2:
                if (find_tokens(all_commands[i], default_delimiters, command_3, i) == -1)
                {
                    return -1;
                }
                break;
            case 3:
                if (find_tokens(all_commands[i], default_delimiters, command_4, i) == -1)
                {
                    return -1;
                }
                break;

            default:
                break;
            }
        }
        all_commands_pointer[i] = NULL;
    }
    else
    {
        // if no speical character is selected, only 1 command exists
        return find_tokens(input_backup, default_delimiters, command_1, -1);
    }

    return 1;
}

// PART 3: Perform Commands - Functions

// returns the number of parameter in a command
// returns length
int find_command_length(char *command[5])
{
    int len = 0;

    while (command[len] != NULL)
    {
        len++;
    }
    return len;
}

// this function will find selected option and verify that it exists
void find_custom_command(char *token)
{
    // reset
    selected_custom_command = -1;

    // find selected command
    for (int i = 0; i < SPECIAL_COMMANDS; i++)
    {
        if (strcmp(custom_commands[i], token) == 0)
        {
            selected_custom_command = i;
        }
    }
}

// since cd is a bash utility and not a command it won't run using exec.
int cd_command(char *input, char *default_delimiters)
{
    find_tokens(input, default_delimiters, command_1, -1); // save input in command_1

    // make path for user directory
    char *user = getenv("USER");
    char *path = "/home/";
    int user_dir_len = strlen(user) + strlen(path) + 1;

    char *user_dir = malloc(sizeof(char) * user_dir_len);
    strcpy(user_dir, path);
    strcat(user_dir, user);
    user_dir[user_dir_len] = '\0';

    // if command length is not 2 then not allowed to run this program
    if (find_command_length(command_1) <= 2)
    {
        // if only 1 argument then go to user directory
        if (command_1[1] == NULL)
        {
            chdir(user_dir);
        }
        else
        {
            int change_path_len = strlen(command_1[1]);
            char *change_path = malloc(sizeof(char) * change_path_len);

            // if ~, just go to home path
            if (strlen(command_1[1] + 1) == 0 && command_1[1][0] == '~')
            {
                if (chdir(user_dir) == -1)
                {
                    printf("No Such Directory %s\n", command_1[1]);
                    return -1;
                }
            }
            else
            {
                // expand shorthand character to /home/USER
                if (command_1[1][0] == '~')
                {
                    change_path_len = user_dir_len + 1 + strlen(command_1[1]);
                    change_path = malloc(sizeof(char) * change_path_len);
                    strcpy(change_path, user_dir);
                    strncat(change_path, command_1[1] + 1, strlen(command_1[1]) - 1); // copy after ~
                }
                else
                {
                    strcpy(change_path, command_1[1]);
                }

                change_path[change_path_len] = '\0';

                if (chdir(change_path) == -1)
                {
                    printf("No Such Directory %s\n", command_1[1]);
                    return -1;
                }
            }
        }
    }
    else
    {
        printf("cd: too many arguments\n");
        return -1;
    }
}

// performs command according to selected_command
// returns exit status of command
// 0 for success, -1 for error
int perform_custom_command(char *input)
{
    int child_pid, size;
    // make commands and store below to run
    char *dtex_command[] = {"pkill", "-9", "minibash", NULL};
    char *minibash_command[] = {"minibash", "minibash", NULL};

    switch (selected_custom_command)
    {
    case 0:
        // for cd
        return cd_command(input, default_delimiters);
        break;
    case 1:
        // for dter command, kill current bash
        if (kill(getpid(), SIGKILL) == -1)
            return -1;
        break;

    case 2:
        // for dtex command, kill all bash with name minibash using pkill
        // pkill stands for process kill
        return fork_and_run(dtex_command, input);
        break;

    case 3:
        return fork_and_run(minibash_command, NULL);
        break;

    case 4:
        // for exit command
        handle_sigint();
        break;

    case 5:
        // for fore command, bring background process to foreground
        // pop the last process
        size = find_size();
        if (size >= 1)
        {
            int pid = background_processes_pids[size - 1];
            get_index_and_shift(pid);                 // get index and shift array
            background_processes_pids[size - 1] = -1; // remove from process id
            printf("Process with pid:%d moved to foreground\n", pid);
            kill(pid, SIGCONT); // send sigcont signal to child who is in background
        }
        else
        {
            printf("fg: current: no such job\n");
        }
        break;

    case 6:
        // for clear command
        printf("\e[1;1H\e[2J"); // clear screen ascii
        break;

    default:
        break;
    }

    return 1;
}

// forks and runs process in child
int fork_and_run(char *command[], char *input)
{
    if (input)
    {
        // check if a command is a custom command
        find_custom_command(command[0]);

        // for custom commands
        if (selected_custom_command != -1)
        {
            return perform_custom_command(input);
        }
    }

    int child_pid = fork();

    if (child_pid > 0)
    {
        // parent process
        int status;
        wait(&status);
        return status;
    }
    else if (child_pid == 0)
    {
        // child process differentiate with given command
        if (execvp(command[0], command) == -1)
        {
            printf("minibash: %s: command not found\n", command[0]);
            exit(-1);
        }
    }
    else
    {
        printf("Fork Failed!\n");
        exit(-1);
    }
}

// for # [file.txt]
// performs wc -w [args from command_1]
// returns exit status of the child process which runs the command
int count_words()
{
    int command_length = find_command_length(command_1);

    // at most 1 parameter i.e. total argument == 2
    if (command_length > 2)
    {
        printf("minibash: #: Too many Arguments\n");
        return -1;
    }
    else if (command_length == 0)
    {
        printf("minibash: #: No Arguments Passed\n");
        return -1;
    }
    else
    {

        char **command;
        command = malloc(sizeof(char *) * (command_length + 3));
        int i = 0;
        command[i] = malloc(sizeof(char) * 2);
        command[i] = "wc";
        command[i + 1] = malloc(sizeof(char) * 2);
        command[i + 1] = "-w";

        // copy command 1 arguments
        while (i < command_length)
        {
            command[i + 2] = malloc(sizeof(char) * strlen(command_1[i]));
            command[i + 2] = command_1[i];
            command[i + 2][strlen(command_1[i])] = '\0';
            i++;
        }
        command[command_length + 2] = NULL;
        int ret_value = fork_and_run(command, NULL);

        free(command);
        return ret_value;
    }
}

// for + & fore
// returns size of background_processes_pids
int find_size()
{
    int i = 0;
    while (background_processes_pids[i] != -1)
    {
        i++;
    }
    return i;
}

// for fore
int get_index(int pid)
{

    int index = -1;
    int i = 0;
    int size = find_size();

    while (i < size)
    {
        if (pid == background_processes_pids[i])
        {
            index = i;
            break;
        }
        i++;
    }
    return index;
}

// for + and fore
// get index and shift left in background_processes_pids
// used in send_to_background & fore in perform_custom_command
int get_index_and_shift(int pid)
{
    int size = find_size();
    int i = 0;
    int index = 0;
    bool is_found = false;

    // loop in array
    while (i < size)
    {
        // if element is found, start shifting indexes
        if (is_found)
        {
            background_processes_pids[i] = background_processes_pids[i + 1]; // shift indexes from previous
        }
        else
        {
            if (pid == background_processes_pids[i])
            {
                index = i;
                is_found = true;
                background_processes_pids[i] = background_processes_pids[i + 1];
            }
        }
        i++;
    }
    return index + 1;
}

// for +
// sends a process to background, by making a named pipe
// returns -1 on error
int send_to_background()
{
    int command_1_len = find_command_length(command_1);

    // to ensure + is at the end of an argument
    // at most 1 parameter i.e. total argument == 2
    int i = 0;
    while (i < command_1_len)
    {
        int size = find_size();
        int child_pid;

        child_pid = fork();

        if (child_pid > 0)
        {
            // parent process
            background_processes_pids[size] = child_pid; // save child pid to enter
            background_processes_pids[size + 1] = -1;    // set next index to be -1
            printf("[%d] %d\n", size + 1, child_pid);    // print PID of child
        }
        else if (child_pid == 0)
        {
            // child process
            // sever input of this process and send to below file, temporarily
            int fd = open("/home/damlet/Desktop/asp_assignment/assignment_3/temp_file", O_CREAT | O_RDWR, 0777);
            dup2(fd, 0);
            signal(SIGCONT, handle_sigcont); // register sigcont

            int ret_value = execlp(command_1[i], command_1[i], NULL); // replace with command
            if (ret_value == -1)
            {
                exit(4);
            }
        }
        else
        {
            printf("Fork Failed\n");
            return -1;
        }
        i++;
    }
}

// for <
// take input from a file via redirection of input using dup and dup2
// returns exit status of the child process which runs the command
/// returns -1 on error
int input_from_file()
{
    // only 2 commands can exist, since only 1 < is allowed
    int command_1_len = find_command_length(command_1);
    int command_2_len = find_command_length(command_2);

    if (!command_2[0])
    {
        printf("Provide the file name you want to take input from after '<'\n");
        return -1;
    }

    // command 1 can have at most 4 args (which is handled in tokenization part)
    // command 2 will hold the file name from which input is to be taken
    if (command_1_len <= 4 && command_2_len <= 1)
    {
        // open file in read mode
        int fd = open(command_2[0], O_RDONLY);

        if (fd == -1)
        {
            printf("File %s Does not exist\n", command_2[0]);
            return -1;
        }

        // taking a backup of read descriptor (stdin)
        int read = dup(0);

        if (read == -1)
        {
            printf("Command Failed\n");
            return -1;
        }

        // changing read
        if (dup2(fd, 0) == -1)
        {
            printf("Reading from file failed\n");
            return -1;
        }

        // pass command 1 to execute
        int ret_value = fork_and_run(command_1, NULL);

        // reversal of redirection
        dup2(read, 0);

        // close read file
        close(fd);

        return ret_value;
    }
    else
    {
        printf("Can take Input from only 1 file\n");
        return -1;
    }
}

// for >
// write to output to a file via redirection of output using dup and dup2
// returns exit status of the child process which runs the command
/// returns -1 on error
int output_to_file()
{
    // only 2 commands can exist, since only 1 > is allowed
    int command_1_len = find_command_length(command_1);
    int command_2_len = find_command_length(command_2);

    if (!command_2[0])
    {
        printf("Provide the file name you want to put output in, after '>'\n");
        return -1;
    }

    // command 1 can have at most 4 args (which is handled in tokenization part)
    // command 2 will hold the file name from which input is to be taken
    if (command_1_len <= 4 && command_2_len <= 1)
    {
        // create file in read-write mode, since file might not exist, it needs to be created
        int fd = open(command_2[0], O_CREAT | O_RDWR, 0777);

        if (fd == -1)
        {
            printf("File making failed\n");
            return -1;
        }

        // taking a backup of write descriptor (stdout)
        int write = dup(1);
        if (write == -1)
        {
            printf("Command Failed\n");
            return -1;
        }

        // changing write
        if (dup2(fd, 1) == -1)
        {
            printf("Writing to file failed\n");
            return -1;
        }

        // pass command 1 to execute
        int ret_value = fork_and_run(command_1, NULL);

        // reversal of redirection
        dup2(write, 1);

        // close created/opened file
        close(fd);

        return ret_value;
    }
    else
    {
        printf("Can write output to only 1 file\n");
        return -1;
    }
}

// for >>
// append output of file via redirection of output using dup, dup2
// returns exit status of the child process which runs the command
/// returns -1 on error
int append_to_file()
{
    // only 2 commands can exist, since only 1 >> is allowed
    int command_1_len = find_command_length(command_1);
    int command_2_len = find_command_length(command_2);

    if (!command_2[0])
    {
        printf("Provide the file name you want to take input from after '>>'\n");
        return -1;
    }

    // command 1 can have at most 4 args (which is handled in tokenization part)
    // command 2 will hold the file name from which input is to be taken
    if (command_1_len <= 4 && command_2_len <= 1)
    {
        // open file in append mode
        int fd = open(command_2[0], O_RDWR);
        lseek(fd, 0, SEEK_END); // go to the end

        if (fd == -1)
        {
            printf("File %s Does not exist\n", command_2[0]);
            return -1;
        }

        // taking a backup of write descriptor (stdout)
        int write_1 = dup(1);
        if (write_1 == -1)
        {
            printf("Command Failed\n");
            return -1;
        }

        int pipe_fd[2];
        if (pipe(pipe_fd) == -1)
        {
            printf("Pipe Failed\n");
            return -1;
        }

        // changing write with pipe
        if (dup2(pipe_fd[1], 1) == -1)
        {
            printf("Appending to file failed\n");
            return -1;
        }

        // pass command 1 to execute
        int ret_value = fork_and_run(command_1, NULL);

        // child process will write in pipe & from here it'll be read, once command is execute
        char *buf = malloc(sizeof(char) * 5000); // can max append 5000 bytes to a file
        int bytes_read = read(pipe_fd[0], buf, 5000);
        if (bytes_read == -1)
        {
            printf("Reading from pipe failed\n");
            return -1;
        }

        // reversal of redirection
        dup2(write_1, 1);

        // append to file
        if (write(fd, buf, bytes_read) == -1)
        {
            printf("Write Failed");
            return -1;
        }

        // close file
        close(fd);
        free(buf);

        return ret_value;
    }
    else
    {
        printf("Can take Input from only 1 file\n");
        return -1;
    }
}

// for [file.txt] ~ [file.txt]
// performs cat [args from command_1,2,3,4]
// returns exit status of the child process which runs the command
/// returns -1 on error
int concatetnate_files()
{
    int command_1_len = find_command_length(command_1);
    int command_2_len = find_command_length(command_2);
    int command_3_len = find_command_length(command_3);
    int command_4_len = find_command_length(command_4);

    if (command_1_len <= 1 && command_2_len <= 1 && command_3_len <= 1 && command_4_len <= 1)
    {
        char **command;
        int command_length = special_char_num + 3;
        command = malloc(sizeof(char *) * command_length);
        int i = 0;
        command[i] = malloc(sizeof(char) * 2);
        command[i] = "cat";
        i++;

        // make commands according to number of special characters
        while (i < command_length - 1)
        {
            // i-1 because oth index stores command_1
            if (!all_commands_pointer[i - 1][0])
            {
                printf("Usage: [file1.txt] ~ [file2.txt]\n");
                return -1;
            }
            command[i] = malloc(sizeof(char) * strlen(all_commands_pointer[i - 1][0]));
            command[i] = all_commands_pointer[i - 1][0];

            i++;
        }
        command[i] = NULL;
        int ret_value = fork_and_run(command, NULL);

        free(command);
        return ret_value;
    }
    else
    {
        printf("Usage: [file1.txt] ~ [file2.txt]\n");
        return -1;
    }
}

// checks that all required commands exists
// returns -1 on error
// returns 1 on success
int check_all_commands_exist(char *custom_message)
{
    int i = 0;
    // check that all commands exists
    while (i <= special_char_num)
    {
        if (all_commands_pointer[i][0])
        {
            i++;
        }
        else
        {
            printf("%s\n", custom_message);
            return -1;
        }
    }
    return 1;
}

// for ;
// returns exit status of last command
// returns -1 on error
int run_sequentially(char *input)
{
    int i = 0;
    int ret_value;

    if (check_all_commands_exist("Syntax Error, Unexpected token near ';'") == -1)
    {
        return -1;
    }

    i = 0;
    // loop in all commands to run them one by one
    while (i <= special_char_num)
    {
        ret_value = fork_and_run(all_commands_pointer[i], input);
        i++;
    }
    return ret_value;
}

// run pipes
int run_pipes()
{
    // check that all commands exist
    if (check_all_commands_exist("Syntax Error, Unexpected token near '|'") == -1)
    {
        return -1;
    }

    int status = 0;
    int fd[2];
    int previous_read = 0;
    int previous_write = 1;

    for (int i = 0; i <= special_char_num; i++)
    {
        // if last command don't create pipe
        if (i != special_char_num)
        {
            if (pipe(fd) == -1)
            {
                printf("Pipe Failed\n");
                return -1;
            }
        }

        int child_pid = fork();

        if (child_pid > 0)
        {
            // parent process
            close(fd[1]);          // close write for pipe
            previous_read = fd[0]; // copy pipe's read so that next iteration, new command can read from it
            wait(&status);
            // dup2(stdin_fd_backup, 0); // redirect output back to stdout
        }
        else if (child_pid == 0)
        {
            // child process

            // printf("previous_fd:%d\n", previous_fd);
            // printf("input fd: %d\n", dup2(previous_fd, 0));

            dup2(previous_read, 0); // change input to pipe's read

            // check if it's not last command
            if (i == special_char_num)
            {
                dup2(stdout_fd_backup, 1); // redirect output back to stdout
            }
            else
            {
                close(fd[0]);   // close current pipe input
                dup2(fd[1], 1); // change output to pipe's write
            }

            int exec_fail = execvp(all_commands_pointer[i][0], all_commands_pointer[i]); // differentiate with execvp

            if (exec_fail == -1)
            {
                exit(4);
            }
        }
        else
        {
            printf("Fork Failed\n");
            return -1;
        }
    }
}

// for &&
// returns exit status of last command
// returns -1 on error
int run_and_command(char *input)
{
    int ret_value = 0;
    int i = 0;

    if (check_all_commands_exist("Syntax Error, Unexpected token near '&&'") == -1)
    {
        return -1;
    }

    i = 0;
    // pass command one by one and only if previous command's return status is 0(i.e. executes sucessfully), execute next command
    while (i <= special_char_num && ret_value == 0)
    {
        ret_value = fork_and_run(all_commands_pointer[i], input);
        i++;
    }

    return ret_value;
}

// for || and multiple conditionals
// returns exit status of last command
// returns -1 on error
int run_or_command(char *input)
{
    if (check_all_commands_exist("Syntax Error, Unexpected token near '||' ") == -1)
    {
        return -1;
    }

    if (is_multiple_conditional)
    {
        int ret_value = -1;
        // if there are multiple conditionals
        int i = 0;

        while (i <= special_char_num)
        {
            // check condtionals, after  1st command runs
            if (i >= 1)
            {
                // i-1 th because for at most 4 commands there will be only 3 condtionals
                if (multiple_conditionals_sequence[i - 1] == false)
                {
                    // false represents and
                    // if last command didn't run successfully, skip this command, else run
                    if (ret_value != 0)
                    {
                        ret_value = -1;
                    }
                    else
                    {
                        ret_value = fork_and_run(all_commands_pointer[i], input);
                    }
                }
                else
                {
                    // true represents or
                    // if last command didn't ran successfully, then run
                    if (ret_value != 0)
                    {
                        ret_value = fork_and_run(all_commands_pointer[i], input);
                    }
                }
            }
            else
            {
                // always run 1st command
                ret_value = fork_and_run(all_commands_pointer[i], input);
            }
            i++;
        }

        return ret_value;
    }
    else
    {
        int ret_value = -1;
        int i = 0;

        // pass command one by one and only if previous command's return status is -1 (i.e. fails) execute next command
        while (i <= special_char_num && ret_value != 0)
        {
            ret_value = fork_and_run(all_commands_pointer[i], input);
            i++;
        }

        return ret_value;
    }
}

// performs commands according to commands stored in command_1,2,3,4 according to selected special character
int perform_commands(char *input)
{
    // for cd command's ~ extension to work
    if (is_special_char && special_char_num == 1 && command_1[0] && (strcmp(command_1[0], "cd") == 0))
    {
        is_special_char = false;
    }

    // if special char, loop and perform commands
    if (is_special_char)
    {
        switch (selected_special_char)
        {
        case 0:
            // for #
            // if number of args > 2, show error
            return count_words();
            break;
        case 1:
            // for +
            return send_to_background();
            break;
        case 2:
            // for <
            return input_from_file();
            break;
        case 3:
            // for >
            return output_to_file();
            break;
        case 4:
            // for >>
            return append_to_file();
            break;
        case 5:
            // for ~
            return concatetnate_files();
            break;
        case 6:
            // for ;
            return run_sequentially(input);
            break;
        case 7:
            // for |
            return run_pipes();
            break;
        case 8:
            // for &&
            return run_and_command(input);
            break;
        case 9:
            // for ||
            return run_or_command(input);
            break;
        default:
            break;
        }
    }
    else
    {
        fork_and_run(command_1, input);
    }
}

// minibash program
void minibash(char *input_from_script)
{
    background_processes_pids = malloc(sizeof(int) * 1000); // can have max 1000 background processes
    background_processes_pids[0] = -1;
    stdin_fd_backup = dup(0);
    stdout_fd_backup = dup(1);
    signal(SIGCHLD, handle_sigchld);
    signal(SIGCONT, handle_sigint);

    // infinite loop for minibash
    while (true)
    {
        // PART 0: THE PROMPT
        // prompt string engineering (this is a joke, obviously)
        char cwd[1000];
        getcwd(cwd, 1000);
        char *minibash = "minibash$";
        int prompt_length = strlen(minibash) + strlen(cwd) + 2;
        char *prompt = malloc(sizeof(char) * prompt_length);
        strcpy(prompt, minibash);
        strcat(prompt, cwd);
        prompt[prompt_length - 2] = '$';
        prompt[prompt_length - 1] = '\0';

        // variables definition
        int input_size;
        long unsigned int size = 1000;
        char *input = malloc(sizeof(char) * size); // maximum 1000 bytes of data can be taken as input
        char *custom_delimiters;                   // will hold extra delimiters to add returned by input parsing

        // PART 1: Take Input, Parse Input

        // prompt and get input
        if (input_from_script)
        {
            input = strdup(input_from_script);
        }
        else
        {
            printf("%s", prompt);
            input_size = getline(&input, &size, stdin); // get complete line till 5000 chars
            input[input_size - 1] = '\0';               // last character replace with null character
        }

        custom_delimiters = input_parsing(input);
        if (custom_delimiters == NULL)
        {
            reset(input); // resets stuff

            // break if script
            if (input_from_script)
            {
                return;
            }
            continue;
        }

        // PART 2: Tokenization & Identification of Commands & it's parameters

        if (tokenize_commands(input, custom_delimiters) == -1)
        {
            reset(input); // resets stuff

            printf("Only 3 Parametes allowed for any Command\n");
            // break if script
            if (input_from_script)
            {
                return;
            }
            continue;
        }

        // PART 3: Perform Commands

        if (perform_commands(input) == -1)
        {
            reset(input); // resets stuff

            // break if script
            if (input_from_script)
            {
                return;
            }
            continue;
        }

        reset(input); // resets stuff

        // break if script
        if (input_from_script)
        {
            return;
        }
    }
}

// shows manual page
void show_docs()
{
    char *buffer = malloc(sizeof(char) * 2500);

    int man_fd = open("minibash_man_page.txt", O_RDONLY);

    if (man_fd == -1)
        exit(-1);

    read(man_fd, buffer, 2500);

    printf("%s\n", buffer);

    close(man_fd);
    free(buffer);
    exit(0);
}

// run a script line by line
// expects user to provide 2nd argument as a bash script file
void run_bash_script(char *argv[])
{
    char const *const file_name = argv[1];
    FILE *fd = fopen(file_name, "r");
    if (!fd)
    {
        printf("MiniBash Script Not Found\n");
        exit(-1);
    }

    long int size = 500;
    char *file_data = malloc(sizeof(char) * size);

    int offset;
    while (getline(&file_data, &size, fd) != -1)
    {
        fseek(fd, 0, SEEK_CUR); // move 0 from current position

        // printf("File Data:%s\n", file_data);
        if (file_data[0] == '#')
        {
            // skip comments
            continue;
        }
        int line_size = strlen(file_data);
        // remove new line character from end if exist
        if (file_data[line_size - 1] == '\n')
        {
            file_data[line_size - 1] = '\0';
        }

        if (line_size > 1)
        {
            // call the command again and again
            printf("\nCommand:%s\n", file_data);
            minibash(file_data);
        }
        free(file_data);
        file_data = malloc(sizeof(char) * size);
    }
    fclose(fd);
}

// Driver Function
int main(int argc, char *argv[])
{
    // show documentation if args > 2
    if (argc > MIN_ARGS)
    {
        show_docs();
    }
    else if (argc == 2)
    {

        if (strcmp("--help", argv[1]) == 0)
        {
            show_docs();
            exit(0);
        }

        run_bash_script(argv);
    }
    else
    {
        minibash(NULL);
    }
}
