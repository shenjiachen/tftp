
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>		// defines perror(), herror() 
#include <fcntl.h>		// set socket to non-blocking with fcntrl()
#include <unistd.h>
#include <string.h>
#include <assert.h>		//add diagnostics to a program

#include <netinet/in.h>		//defines in_addr and sockaddr_in structures
#include <arpa/inet.h>		//external definitions for inet functions
#include <netdb.h>		//getaddrinfo() & gethostbyname()

#include <sys/socket.h>		//
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/select.h>		// for select() system call only
#include <sys/time.h>		// time() & clock_gettime()
#include"tftp.h"


#define MYPORT "5000" 
#define MAX_FILE_SIZE 33554432  //32MB

void *get_in_addr(struct sockaddr *sa)
{
 if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
 }
     return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* ########################### Main Function #################################*/

int main(int argc, char *argv[])
{
    


/* ############################ Init  ########################################*/
//variable used in Create Socket and Bind
int listenfd;         // listen()ing file descriptor;
struct addrinfo hints,  *servinfo, *current;


//variable used in select
int i, j;            //loop variable;
int newfd;
int fdmax = 0;           // highest file descriptor number;
int messagebytes;    // the length of the message;

int client_portno;
char CLIENT_PORT[5];

unsigned short int opcode;
unsigned short int blocknumber;

fd_set master;       // master file descriptor list;
fd_set read_fds;     //  a temp file descriptor list; 
struct timeval tv;  //time used in select(); record the time transmission for timeout

struct addrinfo *clientinfo;
struct sockaddr_in client_addr; // client address
socklen_t addrlen;

char buff[1024];     // buffer to store client data;
char filename[512]; // store filename;


struct FileNode *head =(struct FileNode*)malloc(sizeof(struct FileNode));
struct FileNode *tail = (struct FileNode*)malloc(sizeof(struct FileNode));
int filesize = 0;
FILE *fileopen = NULL;





/* ########################## Create Socket ##################################*/

memset(&hints, 0, sizeof hints);  //clear hints;
hints.ai_family = AF_UNSPEC;
hints.ai_socktype = SOCK_DGRAM;  //UDP
hints.ai_flags = AI_PASSIVE;
if ( getaddrinfo(NULL, MYPORT, &hints, &servinfo) != 0) {
    perror("Get Information Failed\n");
    exit(1);
}

/* ############################## Bind #######################################*/

// loop through all the results and bind to the first we can
for(current = servinfo; current != NULL; current = current->ai_next) {
    if ((listenfd = socket(current->ai_family, current->ai_socktype, current->ai_protocol)) <0) {
        perror("listener: socket");
        continue;
     }


    if ((bind(listenfd, current->ai_addr, current->ai_addrlen)) < 0) {
        perror("listener: bind");
        close(listenfd);
        continue; 
    }
    break; 
}

if (current == NULL) {         
    perror("Bind Failed\n");    //check whether we bind successfully;
    exit(2); 
}

freeaddrinfo(servinfo);
printf("Waiting to recvfrom... ...\n");

/* ############################## Select #################################### */

fdmax = listenfd;        // keep track of the biggest file descriptor;
FD_ZERO(&master);    // clear the master and temp sets;
FD_ZERO(&read_fds);
FD_SET(listenfd, &master);   // add the listenfd to the master set;
tv.tv_sec=10;
tv.tv_usec=0;
//Init the double linkedlist to store FileNode
head->next = tail;
tail->prev = head;
head->prev =NULL;
tail->next = NULL;

while(1){
    read_fds = master; // copy master to read_fds
    if (select(fdmax+1, &read_fds, NULL, NULL, &tv) == -1) {
        perror("Select Failed");
        exit(4);
    }

 // run through the existing connections looking for data to read
    for(i = 0; i <= fdmax; i++){
        if(FD_ISSET(i, &read_fds)){

            if (i == listenfd) {
                    printf("####################RECIEVE RRQ FROM CLIENT#####################\n");
                    addrlen = sizeof(client_addr);
                    memset(buff, '\0', sizeof(buff));
                    if ((messagebytes = recvfrom(listenfd, buff, sizeof(buff), 0, (struct sockaddr *)&client_addr, &addrlen)) <= 0) {
                       // got error or connection closed by client
                         perror("Receiving Message Error");
                         continue;
                    }
                    opcode =  unpacki16(buff);
                    //printf("Get RRQ opcode = %d\n",opcode);
                    if(opcode != 1){
                        //message type error, we need get RRQ first
                         printf("Need RRQ FOR READING");
                         continue;
                    }
                    memset(filename, '\0', sizeof(filename));
                    strcpy(filename,buff+2);

                    if(RRQisDuplicated(head, tail , filename, client_addr) == 1){
                        printf("Duplicated RRQ\n");
                        continue;
                    }
                   // printf("Get filename: %s\n", filename);
                    
                    client_portno = rand();  // rand() return a number between 0 ~ 32767;
                    client_portno = client_portno % 1000 + 5001;
                    memset(CLIENT_PORT, '\0', sizeof(CLIENT_PORT));
                    sprintf(CLIENT_PORT, "%d", client_portno);
        
                    // Create new socket for client
                    if ( getaddrinfo(NULL, CLIENT_PORT, &hints, &clientinfo) != 0) {
                         perror("Get Information Failed\n");
                         exit(6);
                    }
                    
                    // loop through all the results and bind to the first we can
                    for(current = clientinfo; current != NULL; current = current->ai_next) {
                        if ((newfd = socket(current->ai_family, current->ai_socktype, current->ai_protocol)) <0) {
                            perror("newfd: socket");
                            continue;
                        }

                        if ((bind(newfd, current->ai_addr, current->ai_addrlen)) < 0) {
                            perror("newfd: bind");
                            close(listenfd);
                            continue; 
                        }
                        break; 
                    }

                    if (current == NULL) {         
                        perror("Bind Failed\n");    //check whether we bind successfully;
                        exit(7); 
                        }

                    freeaddrinfo(clientinfo);
                    printf("Set new socket %d successfull, which port on %s\n",newfd, CLIENT_PORT);
                    printf("#################OPEN FILE , GET FILE INFORMATION###############\n");
                    fileopen = fopen(filename,"r");
                    if(fileopen == NULL){
                        //SEND ERROR
                        Send_Error_Packet(newfd,1,&client_addr,addrlen);
                        close(newfd);  //close newfd
                        continue;
                    }
                    FILE *fp = fileopen;
                    fseek(fp, 0L, SEEK_END);
                    filesize = ftell(fp);
                    fclose(fp);
                    printf("The size of file: %s is %d\n",filename, filesize);
                    if(filesize >MAX_FILE_SIZE){
                        //SEND ERROR
                        Send_Error_Packet(i,4,&client_addr,addrlen);
                        close(newfd);
                        continue;
                    }
                    struct FileNode *Node = (struct FileNode*)malloc(sizeof(struct FileNode));     
                    Node->socketfd = newfd;
                    Node->fp = fopen(filename,"r");
                    Node->totalsize = filesize;
                    Node->recentsize = 512;
                    Node->blocknumber = 1;
                       
                    memset(Node->filename,'\0',sizeof filename);
  					strcpy(Node->filename,filename);

                    memcpy(&(Node->client_addr),&(client_addr),sizeof(client_addr));
                    FileNodeAdd(head, Node);
                    
                    //SEND DATA
                    Send_Data_Packet(Node,0,addrlen);
                    
                    FD_SET(newfd,&master);
  					if(newfd>fdmax){
  						fdmax=newfd;
  					}
           }
           else {
               
               
                   struct FileNode *Node =  FindFileNode(head, tail, i);
                   printf("###########################RECIEVE ACK FROM CLIENT#############################\n");
                   addrlen = sizeof(client_addr);
                   memset(buff, '\0', sizeof(buff));
                   if ((messagebytes = recvfrom(i, buff, sizeof(buff), 0, (struct sockaddr *)&client_addr, &addrlen)) <= 0) {
                        // got error or connection closed by client
                        perror("Receiving Message Error");
                        continue;
                   }
                   
                   opcode =  unpacki16(buff);
                   printf("Get ACK opcode = %d\n",opcode);
                   if(opcode != 4){
                        //message type error, we need get ACK first
                        
                        printf("Need ACK FOR READING");
                        continue;
                   }  
                   
                   blocknumber = unpacki16(buff+2);
                   printf("Get ACK blocknumber = %d\n",blocknumber);
                   
                   int ACK = ACKCheck(blocknumber,Node);
                   if((Send_Data_Packet(Node,ACK,addrlen)) == 1){
                       FileNodeDelete(Node);
                       FD_CLR(i,&master);
                   }
           }
        }//
    }//end of for loop;
}//end of while;

return 0;
}