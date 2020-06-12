#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <mqueue.h>
#include "csapp.h"

// node struct for list of strings
struct node {
  char* text;
  struct node *next;
};

/* Make a new node to hold a copy of txt. */
struct node *make_node(char* txt) {
  struct node *new_node;
  new_node = (struct node*) malloc(sizeof(struct node));
  new_node->text = (char*) malloc(strlen(txt));
  strcpy(new_node->text,txt);
  new_node->next = NULL;
  return new_node;
}

/* Attach a new node to the end of a chain of nodes. */
void append_node(struct node *chain,char* txt) {
  struct node *current = chain;
  while(current->next != NULL)
    current = current->next;
  current->next = make_node(txt);
}

int becomeDaemon(char *home);
int readConfig(char *host, char *port, struct node* *ptr);
int logOpen();
void logMessage(char *msg);
void logClose();
void *thread(void *vargp);
void *threadM(void *vargp);

const char* pidFileLoc = "/run/fwd.pid";
const char* confFileLoc = "/etc/fwd.conf";
const char* logFileLoc = "/var/log/fwd.log";
FILE* logFile;
char host[MAXLINE], port[MAXLINE];

static volatile sig_atomic_t hupReceived = 0;
static sem_t mutex;
// static sem_t *fmutex;

void sighupHandler(int sig){
    hupReceived = 1;
}


int main(int argc, char **argv)
{
  struct sigaction action, old_action, sa;

  struct node *paths;
  pthread_t tid;
  /* Install the handler for SIGHUP */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = sighupHandler;
  if(sigaction(SIGHUP, &sa, NULL) == -1) {
    fprintf(stderr, "Failed to install SIGHUP handler\n");
    exit(1);
  }

  /* Read configuration information */
  if(readConfig(host, port, &paths) != 0) {
    fprintf(stderr, "Failed to read config\n");
    exit(1);
  }

  /* Open log file */
  if(logOpen() != 0) {
    fprintf(stderr, "Could not open log file\n");
    exit(1);
  }
  /* Switch to the background */
  if(becomeDaemon("/") != 0) {
    fprintf(stderr, "Failed becomeDaemon\n");
    exit(1);
  }

  /* Initialize semaphore */
  Sem_init(&mutex, 0, 1);
  
  /* Launch thread that listens to message queue */
  Pthread_create(&tid, NULL, threadM, NULL);

  /* Launch thread except for the last folder */
  while(paths->next != NULL){
    Pthread_create(&tid, NULL, thread, paths);
    paths = paths->next;
  }

  /* Run the thread routine for the last folder */
  thread(paths);
  
  return 0;
}

/*
 * thread routine for message queue
 */
void *threadM(void *vargp){
  mqd_t mqd;
  sem_t *fmutex;
  char buf[MAXLINE], logBuffer[MAXLINE];
  int size;

  mqd = mq_open("/suspend", O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO, NULL);

  while(1){
    
    if((size = mq_receive(mqd, buf, MAXLINE, NULL)) == -1){
      perror("Error getting message");
      break;
    }
    buf[size] = 0;
    sprintf(logBuffer, "%s: %s\n","Got message", buf);
    logMessage(logBuffer);
    char folder[MAXLINE], inst[MAXLINE], path[MAXLINE];
    sscanf(buf, "%s %s", folder, inst);
    sprintf(path, "/%s", folder);
    fmutex = sem_open(path, 0);
    int val;
    sem_getvalue(fmutex, &val);
    sprintf(logBuffer, "%s %d\n", "Opened semaphore with value:", val);
    logMessage(logBuffer);

    if(strcmp(inst, "lock") == 0) {
      P(fmutex);
      mq_send(mqd, "Locked", strlen("Locked"), 0);
      sprintf(logBuffer, "%s\n", "Sent message");
      logMessage(logBuffer);
      sem_close(fmutex);
    }
    else if (strcmp(inst, "unlock") == 0){
      V(fmutex);
      mq_send(mqd, "Unlocked", strlen("Unlocked"), 0);
      sem_close(fmutex);
    }
  }
  mq_close(mqd);
  mq_unlink("/suspend");
}

/*
 * thread routine
 */
void *thread(void *vargp){
  time_t timer;
  DIR *watchDIR;
  struct dirent* entp;
  struct stat s;
  char path[1024];
  sem_t *nameSem;
  int clientfd, fd;
  char *bufp, buf[MAXLINE], logBuffer[MAXLINE], target[MAXLINE], buFolder[MAXLINE];
  rio_t rio;
  char *home = ((struct node*) vargp)->text;
  sscanf(home, "%s %s", target, buFolder);
  Pthread_detach(pthread_self());

  // Open named semaphore
  sprintf(buf, "/%s", buFolder);
  // Make sure that there is no semaphore already exists
  sem_unlink(buf);
  nameSem = sem_open(buf, O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO, 1);
  while(1) {
    /* Log rotation */
    if(hupReceived != 0){
      logClose();
      logOpen();
      hupReceived = 0;
    }
    /* Note the time and go to sleep. */
    time(&timer);
    sleep(60);
    /* Scan the watched directory for changes. */
    P(nameSem);
    watchDIR = opendir(target);
    while((entp = readdir(watchDIR))!=NULL) {
      strcpy(path,target);
      strcat(path,"/");
      strcat(path,entp->d_name);
      stat(path,&s);
      /* Only pay attention to files, and ignore directories. */
      if(S_ISREG(s.st_mode)) {
        /* Backup any recently modified files. */
        if(difftime(s.st_mtime,timer) > 0) {
          /* Connect to server */
          clientfd = Open_clientfd(host, port);
          Rio_readinitb(&rio, clientfd);
          
          /* Send a status request to server */
          Rio_writen(clientfd, "status\n", strlen("status\n"));
          sprintf(logBuffer, "%s: %s\n","Folder name", buFolder);
          logMessage(logBuffer);
          strcpy(buf, buFolder);
          strcat(buf, "\n");
          Rio_writen(clientfd, buf, strlen(buf));
          strcpy(buf, entp->d_name);
          strcat(buf, "\n");
          Rio_writen(clientfd, buf, strlen(buf));
          Rio_readlineb(&rio, buf, MAXLINE);

          // Remove last character of read buffer which is '\n'
          buf[strlen(buf) - 1] = 0;
          /* Close connection to server */
          Close(clientfd);

          /* Case for no such file exist in backup directory */
          if(strcmp(buf, "0") == 0) {
            /* Connect to server */
            clientfd = Open_clientfd(host, port);
            Rio_readinitb(&rio, clientfd);

            /* Send a upload request to server */
            Rio_writen(clientfd, "upload\n", strlen("upload\n"));
            strcpy(buf, buFolder);
            strcat(buf, "\n");
            Rio_writen(clientfd, buf, strlen(buf));
            strcpy(buf, entp->d_name);
            strcat(buf, "\n");
            Rio_writen(clientfd, buf, strlen(buf));
            sprintf(buf, "%ld\n", s.st_size);
            Rio_writen(clientfd, buf, strlen(buf));
            
            /* Open file and map the data, send it to server */
            fd = Open(path, O_RDONLY, 0);
            bufp = Mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            Close(fd);
            Rio_writen(clientfd, bufp, s.st_size);
            Munmap(bufp, s.st_size);
            Rio_readlineb(&rio, buf, MAXLINE);

            /* Close connection to server */
            Close(clientfd);
            sprintf(logBuffer, "%s\n", buf);
            logMessage(logBuffer);
          }

          /* Case for old version of file exist in the backup directory */
          else {
            if(difftime(s.st_mtime, atoi(buf)) > 0){
              /* Connect to server */
              clientfd = Open_clientfd(host, port);
              Rio_readinitb(&rio, clientfd);

              /* Send a upload request to server */
              Rio_writen(clientfd, "upload\n", strlen("upload\n"));
              strcpy(buf, buFolder);
              strcat(buf, "\n");
              Rio_writen(clientfd, buf, strlen(buf));
              strcpy(buf, entp->d_name);
              strcat(buf, "\n");
              Rio_writen(clientfd, buf, strlen(buf));
              sprintf(buf, "%li\n", s.st_size);
              Rio_writen(clientfd, buf, strlen(buf));
              
              fd = Open(path, O_RDONLY, 0);
              bufp = Mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
              Close(fd);
              Rio_writen(clientfd, bufp, s.st_size);
              Munmap(bufp, s.st_size);
              Rio_readlineb(&rio, buf, MAXLINE);

              /* Close connection to server */
              Close(clientfd);
              sprintf(logBuffer, "%s\n", buf);
              logMessage(logBuffer);
            }
          }
        }
      }
    }
    closedir(watchDIR);
    V(nameSem);
  }
}

/*
 * becomeDaemon - convert this process to a daemon
 */
int becomeDaemon(char *home)
{
  int fd;
  pid_t pid;
  FILE* pidFile;

  if((pid = Fork()) != 0) { /* Become background process */
    exit(0);  /* Original parent terminates */
  }

  if(setsid() == -1) /* Become leader of new session */
    return -1;

  if((pid = Fork()) != 0) { /* Ensure we are not session leader */
	/** Prepare pid file and terminate **/
	pidFile = fopen(pidFileLoc,"w");
	fprintf(pidFile,"%d",pid);
	fclose(pidFile);
    exit(0);
  }

  chdir(home); /* Change to home directory */

  Close(STDIN_FILENO); /* Reopen standard fd's to /dev/null */

  fd = Open("/dev/null", O_RDWR, 0);

  if (fd != STDIN_FILENO)         /* 'fd' should be 0 */
    return -1;
  if (Dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    return -1;
  if (Dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO)
    return -1;

  return 0;
}

/*
 * readConfig - read the configuration file
 */
int readConfig(char* host,char* port,struct node* *ptr) {
  FILE *config;
  config = fopen(confFileLoc,"r");
  if(config == NULL)
    return -1;

  char buffer[256];
  
  // Read in the line and remove the last character which is '\n'
  fgets(buffer, 256, config);
  buffer[strlen(buffer) - 1] = 0;
  *ptr = make_node(buffer);
  while(1) {
    // Read in the line and remove the last character which is '\n'
    fgets(buffer, 256, config);
    buffer[strlen(buffer) - 1] = 0;
    if(strlen(buffer) == 0) /* Stop when you hit a blank line. */
       break;
    append_node(*ptr,buffer);
  }

  fscanf(config,"%s",host);
  fscanf(config,"%s",port);
  fclose(config);
  return 0;
}

/*
 * logOpen - open the log file
 */
int logOpen() {
  logFile = fopen(logFileLoc,"a");
  if(logFile == NULL)
    return -1;
  return 0;
}

/* 
 * logMessage - write a message to the log file
 */
void logMessage(char *msg) {
  if(logFile != NULL) {
    P(&mutex);
    fprintf(logFile,"%s\n",msg);
    fflush(logFile);
    V(&mutex);
  }
}

/*
 * logClose - close the log file
 */
void logClose() {
  if(logFile != NULL)
    fclose(logFile);
}

