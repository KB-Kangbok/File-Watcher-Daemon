/* 
 * bu.c - A backup server for fwd 
 */ 
/* $begin bumain */
#include "csapp.h"

int becomeDaemon(char *home);
int readConfig(char *port, char *home);
int logOpen();
void logMessage(char *msg);
void logClose();
void *thread(void *vargp);

const char* pidFileLoc = "/run/bu.pid";
const char* confFileLoc = "/etc/bu.conf";
const char* logFileLoc = "/var/log/bu.log";
FILE* logFile;
char myHome[MAXLINE];

static volatile sig_atomic_t hupReceived = 0;
static sem_t mutex;


void sighupHandler(int sig){
    hupReceived = 1;
}

void request(int connfd){
    int fd; 
    char buf[MAXLINE], *fp; 
    char path[MAXLINE], logBuffer[MAXLINE];
    struct stat s;
    rio_t rio;

    DIR *watchDIR;
    struct dirent* entp;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);

    if(strcmp(buf,"status\n") == 0) {
        Rio_readlineb(&rio, buf, MAXLINE);
        // Remove linefeed character
        buf[strlen(buf) - 1] = 0;
        strcpy(path, myHome);
        strcat(path, buf);
        sprintf(logBuffer, "%s: %s\n", "Folder", buf);
        logMessage(logBuffer);
        Rio_readlineb(&rio, buf, MAXLINE);
        buf[strlen(buf) - 1] = 0;
        strcat(path, "/");
        strcat(path, buf);
        sprintf(logBuffer, "%s %s\n", "Status request for file:", buf);
        logMessage(logBuffer);
        sprintf(logBuffer, "%s: %s\n", "Total path", path);
        logMessage(logBuffer);
        if(stat(path, &s) == 0) {
            sprintf(buf, "%li\n", s.st_mtime);
            Rio_writen(connfd, buf, strlen(buf));
            sprintf(logBuffer, "%s\n", "Sent m_time of the file");
            logMessage(logBuffer);
        } else {
            Rio_writen(connfd, "0\n", strlen("0\n"));
            sprintf(logBuffer, "%s\n", "No such file exist in backup directory");
            logMessage(logBuffer);
        }

    } else if(strcmp(buf,"upload\n") == 0) {

        Rio_readlineb(&rio, buf, MAXLINE);
        // Remove linefeed character
        buf[strlen(buf) - 1] = 0;
        strcpy(path, myHome);
        strcat(path, buf);
        mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
        strcat(path, "/");
        Rio_readlineb(&rio, buf, MAXLINE);
        buf[strlen(buf) - 1] = 0;
        strcat(path, buf);
        sprintf(logBuffer, "%s %s\n", "Upload request for file:", buf);
        logMessage(logBuffer);

        char size[MAXLINE];
        Rio_readlineb(&rio, size, MAXLINE);
        // Remove linefeed character
        size[strlen(size) - 1] = 0;
        // Create file if not already exist, truncate if file is not empty
        sprintf(logBuffer, "%s %s\n", "Open path:", path);
        logMessage(logBuffer);

        fd = Open(path, O_CREAT | O_TRUNC | O_RDWR, 0);
        fchmod(fd, S_IRWXU | S_IRWXG | S_IRWXO);
        fp = malloc(atoi(size));
        Rio_readnb(&rio, fp, atoi(size));
        // Put the data inside fp in backup file.
        Rio_writen(fd, fp, atoi(size));
        free(fp);
        Close(fd);
        sprintf(logBuffer, "%s\n", "Upload complete");
        logMessage(logBuffer);
        // Respond back to client
        Rio_writen(connfd, "stored\n", strlen("stored\n"));
    } else if(strcmp(buf, "list\n") == 0) {
        Rio_readlineb(&rio, buf, MAXLINE);
        buf[strlen(buf) - 1] = 0;
        sprintf(logBuffer, "%s: %s", "List request for folder", buf);
        logMessage(logBuffer);
        sprintf(path, "%s%s", myHome, buf);

        watchDIR=opendir(path);
        if(watchDIR == NULL) {
          sprintf(logBuffer, "%s: %s", "Folder does not exist", path);
          logMessage(logBuffer);
        }
        else {
          while((entp=readdir(watchDIR)) != NULL){
            sprintf(buf, "%s/%s",path, entp->d_name);
            stat(buf, &s);
            if(S_ISREG(s.st_mode)){
              sprintf(buf, "%s\n", entp->d_name);
              Rio_writen(connfd, buf, strlen(buf));
              sprintf(logBuffer, "%s %s", "Sent file", buf);
              logMessage(logBuffer);
            }
          }
        }
        Rio_writen(connfd, "\n", strlen("\n"));
        sprintf(logBuffer, "%s\n", "List request done");
        logMessage(logBuffer);
    } else if(strcmp(buf, "download\n") == 0) {
        Rio_readlineb(&rio, buf, MAXLINE);
        buf[strlen(buf) - 1] = 0;
        sprintf(logBuffer, "%s: %s", "Download request for folder", buf);
        logMessage(logBuffer);
        sprintf(path, "%s%s", myHome, buf);
        Rio_readlineb(&rio, buf, MAXLINE);
        buf[strlen(buf) - 1] = 0;
        strcat(path, "/");
        strcat(path, buf);
        sprintf(logBuffer, "%s: %s", "Total path", path);
        logMessage(logBuffer);
        stat(path, &s);
        sprintf(buf, "%ld\n", s.st_size);
        Rio_writen(connfd, buf, strlen(buf));

        if(s.st_size > 0) {
          sprintf(logBuffer, "%s: %s", "Opening file to send", path);
          logMessage(logBuffer);
          fd = Open(path, O_RDONLY, 0);
          sprintf(logBuffer, "%s", "Opened file");
          logMessage(logBuffer);
          fp = Mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
          sprintf(logBuffer, "%s", "Mapped file");
          logMessage(logBuffer);
          Close(fd);
          Rio_writen(connfd, fp, s.st_size);
          Munmap(fp, s.st_size);
          sprintf(logBuffer, "%s", "Sent file");
          logMessage(logBuffer);
        }
        sprintf(logBuffer, "%s", "Download request complete");
        logMessage(logBuffer);
    }
}


int main(int argc, char **argv) 
{
    int listenfd, *connfdp;
    char myPort[MAXLINE], logBuffer[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */
    char client_hostname[MAXLINE], client_port[MAXLINE];
    struct sigaction sa;
    pthread_t tid;

    /** Install SIGHUP handler **/
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sighupHandler;
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
      fprintf(stderr, "Failed to install SIGHUP handler\n");
      exit(1);
    }

    /* Read configuration information */
    if (readConfig(myPort, myHome) != 0) {
      fprintf(stderr, "Failed to read config file\n");
      exit(1);
    }

    /* Open log file */
    if (logOpen() != 0){
      fprintf(stderr, "Could not open log file\n");
      exit(1);
    }

    /* Switch to the background */
    if (becomeDaemon(myHome) != 0) {
      fprintf(stderr, "Failed becomeDaemon\n");
      exit(1);
    }

    /* Initialize semaphore */
    Sem_init(&mutex, 0, 1);

    /* Open socket and serve requests */
    listenfd = Open_listenfd(myPort);
    while (1) {
      if(hupReceived != 0) {
        logClose();
        logOpen();
        hupReceived = 0;
      }
	    clientlen = sizeof(struct sockaddr_storage); 
      connfdp = Malloc(sizeof(int));
	    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
      Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, 
                  client_port, MAXLINE, 0);
      sprintf(logBuffer, "Connected to (%s, %s)", client_hostname, client_port);
      logMessage(logBuffer);
      Pthread_create(&tid, NULL, thread, connfdp);
    }
    exit(0);
}
/* $end bumain */

/*
 * thread routine
 */
void *thread(void *vargp) {
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  request(connfd);
  Close(connfd);
  return NULL;
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
int readConfig(char *port,char *home) {
  FILE *config;

  config = fopen(confFileLoc,"r");
  if(config == NULL)
    return -1;
  fscanf(config,"%s",port);
  fscanf(config,"%s",home);
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
 * logMessage - write a message to the log file with use of semaphore
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
