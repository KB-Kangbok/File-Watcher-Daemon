#include "csapp.h"
#include <mqueue.h>

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

int readConfig(char *host, char *port, struct node* *ptr);

const char* confFileLoc = "/etc/fwd.conf";

int main(int argc, char **argv){
    struct node *folders, *files;
    char host[MAXLINE], port[MAXLINE], buf[MAXLINE], target[200], buFolder[200];
    char *fp;
    int clientfd, fd;
    rio_t rio;
    mqd_t mqd;

    // Read folder list from fwd.conf
    readConfig(host, port, &folders);

    // Open message queue for fwd
    mqd = mq_open("/suspend", O_RDWR);

    // Make pointer for user purpose
    struct node *begin = folders;
    int index = 1;
    printf("Please identify the index of the folder to work with.\nList of target folders:\n");
    while(begin->next != NULL) {
      sscanf(begin->text, "%s %s", target, buFolder);
      printf("%d. %s\n", index, target);
      index++;
      begin = begin->next;
    }
    sscanf(begin->text, "%s %s", target, buFolder);
    printf("%d. %s\n: ", index, target);
    // Get folder index from user.
    scanf("%d", &index);


    for (int i = 1; i < index; i++){
      folders = folders->next;
    }
    // Now folders->text has user identified target folder.
    
    sscanf(folders->text, "%s %s", target, buFolder);
    sprintf(buf, "%s lock", buFolder);
    mq_send(mqd, buf, strlen(buf), 0);
    mq_receive(mqd, buf, MAXLINE, NULL);
    printf("%s %s\n", buf, target);

    // Connect to bu server
    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    // Send list request
    Rio_writen(clientfd, "list\n", strlen("list\n"));
    sprintf(buf, "%s\n", buFolder);
    Rio_writen(clientfd, buf, strlen(buf));

    // Get list response
    Rio_readlineb(&rio, buf, MAXLINE);
    // Remove last character of response which is always \n.
    buf[strlen(buf) - 1] = 0;
    files = make_node(buf);
    while(strcmp(buf, "\0") != 0) {
      Rio_readlineb(&rio, buf, MAXLINE);
      buf[strlen(buf) - 1] = 0;
      if (strcmp(buf, "\0") == 0) {
          break;
      }
      append_node(files, buf);
    }
    Close(clientfd);

    // Ask user what to download
    printf("Identify which file to restore with (Y/N)\n");
    while(files != NULL){
      if(strcmp(files->text, "\0") == 0) {
        printf("%s\n", "There is no file in the target backup folder.");
        break;
      }
      printf("%s %s: ", "File name (Y/N)", files->text);
      char ans[10];
      scanf("%s", ans);
      if(strcmp(ans, "Y") == 0) {
        // Do download request
        clientfd = Open_clientfd(host, port);
        Rio_readinitb(&rio, clientfd);

        Rio_writen(clientfd, "download\n", strlen("download\n"));
        sprintf(buf, "%s\n", buFolder);
        Rio_writen(clientfd, buf, strlen(buf));
        sprintf(buf, "%s\n", files->text);
        Rio_writen(clientfd, buf, strlen(buf));

        // Get response
        Rio_readlineb(&rio, buf, MAXLINE);
        buf[strlen(buf) - 1] = 0;
        if(strcmp(buf,"0") == 0) {
          printf("%s\n", "File does not exist.");
        }
        else{
          fp=malloc(atoi(buf));

          char path[MAXLINE];
          sprintf(path, "%s/%s", target, files->text);
          fd = Open(path, O_CREAT | O_TRUNC | O_RDWR, 0);
          fchmod(fd, S_IRWXU | S_IRWXG | S_IRWXO);

          Rio_readnb(&rio, fp, atoi(buf));
          // Put the data inside fp in backup file.
          Rio_writen(fd, fp, atoi(buf));
          free(fp);
          Close(fd);

          printf("%s %s\n", "Restored file", files->text);
        }
        Close(clientfd);
      }
      files = files->next;
    }

    sprintf(buf, "%s unlock", buFolder);
    mq_send(mqd, buf, strlen(buf), 0);
    mq_close(mqd);
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