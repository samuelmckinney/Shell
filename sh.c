#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "jobs.h"
#include "jobs.c"
#include <signal.h>


#define INPUT_BUF_SIZE 1024 //define the size of our input buffer


job_list_t * job_list;
int jid;
void jobcheck(int);
//function to set signal to ignore behavior for when shell has control

void set_ign(int sig){
	if (signal(sig, SIG_IGN) == SIG_ERR){
		perror("signal");
		cleanup_job_list(job_list);
		exit(1);
	}
}

//function to set signals to their default behavior for foreground processes

void set_dft(int sig){
	if (signal(sig, SIG_DFL) == SIG_ERR){
		perror("signal");
		cleanup_job_list(job_list);
		exit(1);
	}
}

//builtin fg function that resumes a stopped process in the foreground if its in the background
//tokenizes string to get jid, sends that job a continue signal then waits for it to complete

void fg(char * job){
	char * tok = strtok(job, "%");
	int _jid = atoi(tok);
	int sta = 0;
	int pid = get_job_pid(job_list, _jid);
	if (pid == -1){
		fprintf(stderr, "%s\n", "job not found");
		return;
	}
	kill( -1 * pid, SIGCONT);
	tcsetpgrp(0, getpgid(pid));
	waitpid( -1 * pid, &sta, WUNTRACED);
	if (WIFSIGNALED(sta)){
		if (printf("%s%d%s%s%d%s","[", _jid, "]", "(", pid , ") " ) == -1){
			fprintf(stderr, "%s\n", "printf error" );
		}
		printf("%s", "terminated by signal " );
		printf("%d\n", WTERMSIG(sta));
		remove_job_pid(job_list, pid);

}
	if (WIFSTOPPED(sta)){

		update_job_jid(job_list, _jid, _STATE_STOPPED);
		printf("%s%d%s%s%d%s","[",_jid, "]", "(", pid, ") " );
		printf("%s", "suspended by signal " );
		printf("%d\n", WSTOPSIG(sta));
	}
	if (WIFEXITED(sta)){
		remove_job_pid(job_list, pid);
	}



}



/* func bg:

 Params: job - string containing %[jib] from shell input


extracts jib from string beginning with %
sends SIGCONT to procress group with corresponding pgid

*/
 void bg(char * job){
	 char * tok = strtok(job, "%");
	 int _jid = atoi(tok);
	 if (get_job_pid(job_list, _jid) == -1){
		 printf("%s\n", "job not found");
		 return;
	 }
	  (kill(-1 * getpgid(get_job_pid(job_list, _jid)), SIGCONT));
	 jobcheck(update_job_pid(job_list, getpgid(get_job_pid(job_list, _jid)), _STATE_RUNNING));

 }

//function to handle input/output redirection - takes in array containing redirection files in format: [inputfile, outputfile, appendfile]

void redirector(char ** redirection){
    if (strcmp(redirection[0], "0") != 0){
        if (close(0) == -1){ //close standard input
            perror("close");
			cleanup_job_list(job_list);
	        exit(1);
        }
        if (open(redirection[0], O_RDONLY) == -1){ //open specified file in read only mode
            perror("open");
			cleanup_job_list(job_list);
	        exit(1);
        }
    }
    if (strcmp(redirection[1], "0") != 0){
        if (close(1) == -1){ //close standard output
                perror("close");
				cleanup_job_list(job_list);
		        exit(1);
        }
        open(redirection[1], O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR); //open indicated output file with proper permissions
    }
    if (strcmp(redirection[2], "0") != 0){
        if (close(1) == -1){
                perror("close");
				cleanup_job_list(job_list);
		        exit(1);
        }
        if (open(redirection[2], O_WRONLY | O_APPEND | O_CREAT, 00600) == -1){ //same but use the O_APPEND flag to make sure it appends the output file
            perror("open");
			cleanup_job_list(job_list);
	        exit(1);
        }
    }
}


//builtin cd function

int cd(char ** dest){
    if (dest[1] == NULL){ //error to make sure there is a destination argument
        fprintf(stderr, "%s\n", "cd: syntax error");
        return 0;
    }
     if (strcmp(dest[1], "..") == 0){ //handle the parent directory case
         char buf[INPUT_BUF_SIZE];
         char * path = getcwd(buf, INPUT_BUF_SIZE); //find working directory
         char * end = strrchr(path, '/'); //find the last occurance of /
         char * tok = strtok(end-1, "/"); // tokenize beginning right before that / to replace it with a null pointer, thus making path the parent directory
         tok++; // this is probably unnecessary - so the compiler doesn't tag tok as an unused variable
         if (chdir(path) == -1){ //call chdir on the parent directory
             perror("chdir"); //error check
			 cleanup_job_list(job_list);
	         exit(1);
         }
         return 0;
     }
    if (chdir(dest[1]) == -1){ //regular call of chdir on the destination argument. ignores extraneous arguments
        perror("chdir");
		cleanup_job_list(job_list);
        exit(1);
    }
    return 0;

}

 //builtin ln command

int ln(char * src, char * dest){
    if (src == NULL || dest == NULL){ //error check for too few args
        fprintf(stderr, "%s\n", "Error: too few arguments to ln");
        return -1;
    }
    if (link(src, dest) == -1){ //use syscall link with appropriate error checking
        perror("link");
		cleanup_job_list(job_list);
        exit(1);
    }
    return 0;

}

//builtin rm command

int rm(char ** filename){ //remove file from filesystem by calling unlink
    if (unlink(filename[1]) == -1){
        perror("unlink"); //error check
		cleanup_job_list(job_list);
        exit(1);
    }
    return 0;
}

//handle_child function that executes command given to shell. gets filename then forks, setting control and signal handlers appropriately - after finishing forking handles foreground and background processes appropriately

int handle_child(char ** cmd_arg, char ** redirect, int is_bg){ //after eval has determined the first cmd_arg isn't a built in, handle the child process appropriately
    char * filename = cmd_arg[0]; //store the filename from the inputted array
    size_t len = strlen(filename); //get its length
    char  _filename[len];
    strcpy(_filename, filename); //copy it into a string so we can modify the original
    char * token = strtok(cmd_arg[0], "/\n"); //tokenize using / as a delim
    while (token != NULL){
        char * prev = token;
        token = strtok(NULL, "/\n");
        if ( token == NULL){
            cmd_arg[0] = prev; //replace with the final non-null token aka the command name
        }
    }
	int pid;
    if((pid = fork()) == 0){ //create child process

            if (setpgid(getpid(), getpid()) == -1){
                perror("setpgid");
				cleanup_job_list(job_list);
				exit(1);
            }
            if (tcsetpgrp(0, getpid()) == -1){
                perror("tcsetpgrp");
				cleanup_job_list(job_list);
				exit(1);
            }
            set_dft(SIGINT);
			set_dft(SIGQUIT);
			set_dft(SIGTSTP);
			set_dft(SIGTTOU);
            redirector(redirect); //handle redirection
            execv(_filename, cmd_arg); //execute child process
            perror("execv"); //error check
			cleanup_job_list(job_list);
            exit(1); //exit if it returns here (it shouldn't)
        }
		if (!is_bg){
		int sta = 0;
		if (waitpid(pid, &sta, WUNTRACED ) == -1){
			perror("waitpid");
			cleanup_job_list(job_list);
			exit(1);
		}
		if (WIFSIGNALED(sta)){
			add_job(job_list, jid, pid, _STATE_RUNNING, _filename);
			if (printf("%s%d%s%s%d%s%s%d\n","[", get_job_jid(job_list, pid), "]", "(", pid, ") ", "terminated by signal ", WTERMSIG(sta) ) == -1) {
				fprintf(stderr, "%s\n", "printf error" );
				cleanup_job_list(job_list);
				exit(1);
			}
			jobcheck(remove_job_pid(job_list, pid));
		}
		if (WIFSTOPPED(sta)){
			jobcheck(add_job(job_list, jid, pid, _STATE_STOPPED, _filename));
			jid++;
			jobcheck(update_job_pid(job_list, pid, _STATE_STOPPED));
			if (printf("%s%d%s%s%d%s%s%d\n","[",get_job_jid(job_list, pid), "]", "(", pid, ") ", "suspended by signal ", WSTOPSIG(sta)) == -1){
				fprintf(stderr, "%s\n", "printf error" );
				cleanup_job_list(job_list);
				exit(1);
			}


		}

	} else {
		add_job(job_list, jid, pid, _STATE_RUNNING, _filename);
		jid++;
		if (printf("%s%d%s%s%d%s\n", "[", get_job_jid(job_list, pid), "]", "(", pid, ")") == -1){
			fprintf(stderr, "%s\n", "printf error" );
			cleanup_job_list(job_list);
			exit(1);
		}


	}
	if (!is_bg){
		if (tcsetpgrp(0, getpid()) == -1){
			perror("tcsetpgrp");
			cleanup_job_list(job_list);
			exit(1);
		}
	}

    return 0; //then return

}

/* eval - returns 0 on success
Params: 1. cmd_arg - array of argument strings 2. redirect - array of redirect file names 3. is_bg - boolean representing whether & was part of command arg */

int eval(char ** cmd_arg, char ** redirect, int is_bg){
    if (strcmp(cmd_arg[0], "cd") != 0 && strcmp(cmd_arg[0], "ln") != 0 && strcmp(cmd_arg[0], "jobs") != 0 && strcmp(cmd_arg[0], "rm") != 0 && strcmp(cmd_arg[0], "exit") != 0 && strcmp(cmd_arg[0], "bg") != 0 && strcmp(cmd_arg[0], "fg") != 0){
        return handle_child(cmd_arg, redirect, is_bg);
    } else if (strcmp(cmd_arg[0], "cd") == 0){
        return cd(cmd_arg);
    } else if (strcmp(cmd_arg[0], "ln") == 0){
        return ln(cmd_arg[1], cmd_arg[2]);
    } else if (strcmp(cmd_arg[0] , "rm") == 0){
        return rm(cmd_arg);
    } else if (strcmp(cmd_arg[0] , "exit") == 0){ //handle exit command right here
		cleanup_job_list(job_list);
		exit(1);
    } else if (strcmp(cmd_arg[0], "fg") == 0){
        fg(cmd_arg[1]);
    } else if (strcmp(cmd_arg[0], "bg") == 0){
        bg(cmd_arg[1]);
    } else if (strcmp(cmd_arg[0], "jobs") == 0){
        jobs(job_list);


    }
    return 0;
}


//function to parse input buffer. Removes extraneous material like redirection symbols and background instructions and \n


int parse(char *input){ //parse input
    if (input[0] == '\n'){ //blank line handle
        return 0;
    }
    int i = 0;
    int words = 1;
    while (input[i] != '\n'){
        if (isspace(input[i]) != 0){
            words++; //count number of white spaces to size of cmd_arg array
        }
        i++;
    }
    i = 0;
    while (input[i]){
        i++;
    } //get size of buffer in bytes
    input[i-1] = '\0'; //replace newline symbol with null pointer
    char * cmd_arg[words+1]; //create argument array
    i = 0;
    char * redirect[3] = {"0", "0", "0"}; // initialize redirect array
    char * token = strtok(input, "\t "); //tokenize using space and tab to delim
    while (token != NULL){
        if (strcmp(token, "<") ==0){ //catch redirection symbols and add the following arg to appropriate redirect index instead of cmd_arg
            token = strtok(NULL, " \t");
            redirect[0] = token;
        } else if (strcmp(token, ">") == 0){
            token = strtok(NULL, " \t");
            redirect[1] = token;
        } else if (strcmp(token, ">>") == 0){
            token = strtok(NULL, " \t");
            redirect[2] = token;
            }
         else {
            cmd_arg[i] = token; //otherwise add to cmd_arg
            i++;
        }

        token = strtok(NULL, " \t");

    }
    cmd_arg[i] = NULL; //set the last index of cmd_arg to be null so execv can handle
	int is_bg = 0;
	if (strcmp(cmd_arg[i-1], "&") == 0){
		is_bg = 1;
		cmd_arg[i-1] = NULL;
	}
    return eval(cmd_arg, redirect, is_bg); //evaluate input
}

void jobcheck(int error){
	if (error != 0){
		fprintf(stderr, "%s\n", "Job Error" );
		cleanup_job_list(job_list);
		exit(1);
	}
}


/* func reap - cycles through job list and updates status/job_list as necessary

*/

void reap(){
	pid_t pid;
	while ( (pid = get_next_pid(job_list)) != -1){
		int sta = 0;
		int term = waitpid(pid, &sta, WNOHANG | WUNTRACED | WCONTINUED);
		if (term == -1){
			perror("waitpid");
			cleanup_job_list(job_list);
			exit(1);
		}
		if (term != 0 && !WIFSTOPPED(sta) && !WIFSIGNALED(sta)){
		if (WIFEXITED(sta)){
			if (printf("%s%d%s%s%d%s%s%d","[",get_job_jid(job_list, pid), "]", "(", pid, ") ", " terminated with exit status ", WEXITSTATUS(sta) ) == -1) {
				fprintf(stderr, "%s\n", "printf error" );
				cleanup_job_list(job_list);
				exit(1);
			}

			jobcheck(remove_job_pid(job_list, pid));
		}
	}
		if (WIFSIGNALED(sta)){
			if (printf("%s%d%s%s%d%s%s%d\n","[",get_job_jid(job_list, pid), "]", "(", pid, ") ", "terminated by signal ", WTERMSIG(sta) ) == -1){
				fprintf(stderr, "%s\n", "printf error" );
				cleanup_job_list(job_list);
				exit(1);
			}

			jobcheck(remove_job_pid(job_list, pid));
		}

		if (WIFSTOPPED(sta)){
			if (printf("%s%d%s%s%d%s%s%d\n","[",get_job_jid(job_list, pid), "]", "(", pid, ") ", "suspended by signal ",  WSTOPSIG(sta)  ) == -1){
			fprintf(stderr, "%s\n", "printf error" );
			cleanup_job_list(job_list);
			exit(1);
		}
			if (get_job_jid(job_list, pid) == -1){
				jobcheck(add_job(job_list, jid, pid, _STATE_STOPPED, "re-added"));
				jid++;
			}
			jobcheck(update_job_pid(job_list, pid, _STATE_STOPPED));
		}
		if (WIFCONTINUED(sta)){
			if (printf("%s%d%s%s%d%s%s\n","[",get_job_jid(job_list, pid), "]", "(", pid, ") ", " resumed" ) == -1){
				fprintf(stderr, "%s\n", "printf error" );
				cleanup_job_list(job_list);
				exit(1);
						}

			jobcheck(update_job_pid(job_list, pid, _STATE_RUNNING));
		}
	}
}

//abstract function for setting signals to be ignored by shell when it has control

void sigsetup(){
	set_ign(SIGINT);
	set_ign(SIGQUIT);
	set_ign(SIGTSTP);
	set_ign(SIGTTOU);
}

//main line with REPL, calls reap, sigsetup, and parse.
int main() {

	job_list = init_job_list();
	jid = 1;
    while (1){ //REPL
		if (tcsetpgrp(0, getpid()) == -1){
			perror("tcsetpgrp");
			cleanup_job_list(job_list);
			exit(1);
		}
		reap();
		sigsetup();
        char input_buf[INPUT_BUF_SIZE];
        memset(input_buf, 0, INPUT_BUF_SIZE); //there's no error value for memset - this clears the input_buf to make sure it is refreshed each time the shell listens for input
        ssize_t n;
        #ifdef PROMPT
        if ((printf("33sh> ") < 0) ){ //error check printf
            fprintf(stderr, "%s\n", "A printf error occurred");
        }
        #endif
        if (fflush(stdout) != 0){ //error handle the newline from fflush
            perror("fflush");
			cleanup_job_list(job_list);
			exit(1);
        }
        n = read(STDIN_FILENO, input_buf, INPUT_BUF_SIZE);
        if (n != 0){
            if (parse(input_buf) < 0){ //call and error check parse() which returns -1 on bad input
                fprintf(stderr, "%s\n", "Error: Bad Input");
            }

        } else if (n == -1){ //error check read
            fprintf(stderr, "%s\n", "A read error occurred");
        } else{
			cleanup_job_list(job_list);
            exit(1); //if user inputs ctrl d, exit the shell
        }


    }
    return 0;
}
