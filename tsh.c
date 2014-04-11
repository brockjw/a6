/* 
 * tsh - A tiny shell program with job control
 * 
 * <H. Antonio Cardenas - hcardena> <Put your name and login ID here>
 * <Jacob W. Brock - jbrock>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
  pid_t pid;              /* job PID */
  int jid;                /* job ID [1, 2, ...] */
  int state;              /* UNDEF, BG, FG, or ST */
  char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
  /*Added by Jake
  //Print Environment Variables
  int i = 0;
  while(environ[i]) {
    printf("%s\n", environ[i++]);
  }
  */
  sigset_t sa_mask;
  sigemptyset(&sa_mask);
  sigaddset(&sa_mask, SIGTTOU);
  sigprocmask(SIG_BLOCK, &sa_mask, NULL);

  // Debug
  //  printf("terimal gid: %d\n", getgid());

  char c;
  char cmdline[MAXLINE];
  int emit_prompt = 1; /* emit prompt (default) */

  /* Redirect stderr to stdout (so that driver will get all output
   * on the pipe connected to stdout) */
  dup2(1, 2);

  /* Parse the command line */
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             /* print help message */
      usage();
	    break;
    case 'v':             /* emit additional diagnostic info */
      verbose = 1;
	    break;
    case 'p':             /* don't print a prompt */
      emit_prompt = 0;  /* handy for automatic testing */
	    break;
    default:
      usage();
    }
  }

  /* Install the signal handlers */
  
  /* These are the ones you will need to implement */
  Signal(SIGINT,  sigint_handler);   /* ctrl-c */
  Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
  Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
  
  /* This one provides a clean way to kill the shell */
  Signal(SIGQUIT, sigquit_handler); 
  
  /* Initialize the job list */
  initjobs(jobs);
  
  /* Execute the shell's read/eval loop */
  while (1) {
    /* Read command line */
    if (emit_prompt) {
      printf("%s", prompt);
	    fflush(stdout);
    }
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
      app_error("fgets error");
    if (feof(stdin)) { /* End of file (ctrl-d) */
      fflush(stdout);
	    exit(0);
    }
    
    /* Evaluate the command line */
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 
  
  exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) //Antonio
{
  char *argv[MAXARGS]; //Argument list execve()
  //char buf[MAXLINE]; //Holds modified command line
  int bg; //Should run the job in background or not
  pid_t pid; //Process ID
  int status; //satus for waitpid
  
  
  //just replaced buf with cmdline in bg = ... and commented out the delcaration of buf above
  //strcpy(buf, cmdline); //from now on, modifying buf instead of the original cmdline
  bg = parseline(cmdline, argv);

  if(argv[0] == NULL)
    return; //ignore empty lines
  
  if(!builtin_cmd(argv)){
    if((pid = fork()) == 0) { //Child runs user job
      setpgid(0, 0);
      if(!bg)
        // Get the gid from stdin, and then set that as the grpid for this process.
        // see https://support.sas.com/documentation/onlinedoc/sasc/doc/lr2/tsetpgp.htm for help
        // and assertions to make it robust.
        tcsetpgrp(STDIN_FILENO, tcgetpgrp(STDIN_FILENO));
      if(execve(argv[0], argv, environ) < 0) { //environ is a global variable defined above
        printf("%s: Command not found. \n", argv[0]);
        exit(0);
      }
    }

    //    note_to_self_do_tcsetpgrp_stuff_explained_in_assn_site();

    if(!bg){ //foreground job. Shell waits for the job to complete
      addjob(jobs, pid, FG, cmdline);
      if(waitpid(pid, &status, 0) < 0) {
        unix_error("waitfg: waitpid error");
      }
      deletejob(jobs, pid); // Reap when fg job is done :-/
    }
    else{ //background job. Shell does not wait for the job.
      addjob(jobs, pid, BG, cmdline);
      printf("[%d] (%d) %s", getjobpid(jobs, pid)->jid,  pid, cmdline);
    }
  }
  
  return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
  static char array[MAXLINE]; /* holds local copy of command line */
  char *buf = array;          /* ptr that traverses command line */
  char *delim;                /* points to first space delimiter */
  int argc;                   /* number of args */
  int bg;                     /* background job? */

  strcpy(buf, cmdline);
  buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  if (*buf == '\'') {
    buf++;
    delim = strchr(buf, '\'');
  }
  else {
    delim = strchr(buf, ' ');
  }
  
  while (delim) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* ignore spaces */
      buf++;
    
    if (*buf == '\'') {
      buf++;
      delim = strchr(buf, '\'');
    }
    else {
      delim = strchr(buf, ' ');
    }
  }
  argv[argc] = NULL;
  
  if (argc == 0)  /* ignore blank line */
    return 1;
  
  /* should the job run in the background? */
  if ((bg = (*argv[argc-1] == '&')) != 0) {
    argv[--argc] = NULL;
  }
  return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) //Jake
{
  //strcmp() returns 0 if strings are equal
  if(!strcmp(argv[0], "quit")) {
    exit(0);
  }
  else if(!strcmp(argv[0], "jobs")) {
    listjobs(jobs);
    return 1;
  }
  else if(!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
    do_bgfg(argv);
    return 1;
  }
  return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
  int status;

  // Handle input formats: fg %1 or fg 1.
  struct job_t * job;
  if(argv[1][0] == '%') {
    job = getjobjid(jobs, atoi(argv[1] + 1));
  } else {
    job = getjobjid(jobs, atoi(argv[1]));
  }

  // If job does not exist, print error message.
  if(job == NULL){
    printf("%s: No such job\n", argv[1]);
    return;
  }

  if(!strcmp(argv[0], "bg")) {
    // find job with jid == argv[1]. job.status = BG; send SIGCONT signal;
    job->state = BG;
    kill(job->pid, SIGCONT);
  }
  else { // "fg"
    kill(job->pid, SIGCONT);
    job->state = FG;
    setpgid(job->pid, tcgetpgrp(STDIN_FILENO));

    // Since it's in the fg, wait for it - not sure if correct.
    if(waitpid(job->pid, &status, 0) < 0) {
      unix_error("waitfg: waitpid error");
    }
    // Reap when fg job is done :-/
    deletejob(jobs, job->pid); 

    //    tcsetpgrp(STDIN_FILENO, tcgetpgrp(STDIN_FILENO));
    //    tcsetpgrp(STDIN_FILENO, getgid());
    printf("new gid: %d\n", getgid());

  }

  return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) //Antonio
{
  //http://linux.die.net/man/2/waitpid
  //http://stackoverflow.com/questions/900411/how-to-properly-wait-for-foreground-background-processes-in-my-own-shell-in-c
  //http://www.tutorialspoint.com/unix_system_calls/execve.htm
  // waitpid(pid, &status, 0);
  // waitpid(pid, &status, WNOHANG); //first implementation. Might need revisions.
  return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
  int pid, status;

  // Comment from http://www.gnu.org/software/libc/manual/html_node/Foreground-and-Background.html#Foreground-and-Background
  // When all of the processes in the group have either completed or stopped, the shell should regain control of the terminal for its own process group by calling tcsetpgrp again.

  // Reap every child (not necessarily one signal per child).
  // WNOHANG means quit once all zombies are reaped.
  while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    deletejob(jobs, pid);
  }

  return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) //Antonio
{ 
 /**Question: Why when sending a control-c signal to our shell, it displays ^C and kills the job, but when you do the same on the actual terminal it does not display ^C. Is there any way to handle this?**/

  pid_t fg_pid; 
  
  fg_pid = fgpid(jobs);

  if(fg_pid) {
    kill(fg_pid, sig);
    printf("Job [%d] (%d) terminated by signal %d\n",
           getjobpid(jobs, fg_pid)->jid, fg_pid, sig);
    deletejob(jobs, fg_pid);
  }

  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) //Jake 
{
  int i;

  for(i=0; i < MAXJOBS; i++){
    if(jobs[i].state == FG){
      kill(jobs[i].pid, sig);
      deletejob(jobs, jobs[i].pid);
      return;
    }
  }  
  return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
  job->pid = 0;
  job->jid = 0;
  job->state = UNDEF;
  job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
	    max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
  int i;
    
  if (pid < 1)
    return 0;
  
  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      if (nextjid > MAXJOBS)
        nextjid = 1;
      strcpy(jobs[i].cmdline, cmdline);
      if(verbose){
        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
      }
      return 1;
    }
  }
  printf("Tried to create too many jobs\n");
  return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
  int i;

  if (pid < 1)
    return 0;
  
  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
    }
  }
  return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
  int i;
  
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].state == FG)
	    return jobs[i].pid;
  return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
  int i;
  
  if (pid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid)
	    return &jobs[i];
  return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
  int i;
  
  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
	    return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
  int i;
  
  if (pid < 1)
    return 0;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid) {
      return jobs[i].jid;
    }
  return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
  int i;
  
  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
      case BG: 
		    printf("Running ");
		    break;
      case FG: 
		    printf("Foreground ");
		    break;
      case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
               i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
    }
  }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
  struct sigaction action, old_action;
  
  action.sa_handler = handler;  
  sigemptyset(&action.sa_mask); /* block sigs of type being handled */
  action.sa_flags = SA_RESTART; /* restart syscalls if possible */
  
  if (sigaction(signum, &action, &old_action) < 0)
    unix_error("Signal error");
  return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
  printf("Terminating after receipt of SIGQUIT signal\n");
  exit(1);
}


/**Antonio's Graveyard**/
/**
   short len1 = strlen(cmdline);
   short len2;
   short i= 0, j=0;
   short k;
   short cmp;
   short bool = 0; //flag, 0 = false. 1 = true.
   char* arg;
   
   for(i = 0; i < len1; i++){
   
   if(cmdline[i] != 32){
   
   arg[j] = cmdline[i];
   j++;
   
   }else{
   
   len2 = strlen(arg);
   for(k = 0; k < len2; k++)
   arg[k] = tolower(arg[k]);
   
   cmp = strncmp("quit", arg, 4);
   if(cmp
   
   
   
   
   }
   }
   return;
**/
/**
int cmp;

cmp = strcmp("quit", argv);
if(cmp == 0)
  builtin_cmd(*argv);

cmp = strcmp("jobs", argv);
if(cmp == 0)
  builtin_cmd(*argv);

cmp = strcmp("bg", argv);
if(cmp == 0)
  builtin_cmd(*argv);

cmp = strcmp("fg", argv);
if(cmp == 0)
  builtin_cmd(*argv);
**/


/**Jake's Graveyard**/
//test

// Move current FG job to BG
/*
    int i;

    for(i=0; i < MAXJOBS; i++){
      if(jobs[i].state == FG){
        jobs[i].state = BG;
        break;
      }
    }  
*/
