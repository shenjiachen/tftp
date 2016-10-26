
static char *errormsg[] = {
    "Not defined",
    "File not found",
    "Access violation",
    "Disk full or quota exceeded",
    "Illegal operation",
    "Unknown port number",
    "File already exists",
    "No such user"
};

//Since we only focus on Server and don't consider WRQ
//We only to define DATA_Packet and ERROR_Packet
struct DATA_Packet{
    unsigned short int opcode;   //opcode=3
    unsigned short int blocknumber;
    char data[512];
};

struct ERROR_Packet{           //opcode = 5
    unsigned short int opcode;
    unsigned short int errornumber;
    char error[512];
};

struct FileNode{             //double linkedlist
    int socketfd;
    int totalsize;   //Total size of File
    int recentsize;  // The size we have already read
    int blocknumber;
    FILE *fp;
    char filename[512];
    struct sockaddr_in client_addr;
    struct FileNode *prev;
    struct FileNode *next;
    
};

void packi16 (char *buff, unsigned short int i)
{   //change the host order to network byte order (16bit)
	i = htons(i);
	memcpy(buff,&i,2);
}

unsigned short int unpacki16(char *buff)
{	//change  network byte order to the host order (16bit)
	unsigned short int i;
	memcpy(&i,buff,2);
	i = ntohs(i);
	return i;
}

void FileNodeDelete(struct FileNode *node){
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void FileNodeAdd(struct FileNode *head, struct FileNode *node){
    node->prev = head;
    head->next->prev = node;
    node->next = head->next;
    head->next = node;
}

struct FileNode* FindFileNode(struct FileNode *head, struct FileNode *tail, int socketfd){
	struct FileNode *current = head->next;
	while(current != tail){
	  	if(current->socketfd == socketfd){
	  		return current;
	  	}
	  	current = current->next;
	}
	return NULL;
}


int addresscmp(struct sockaddr_in addr1,struct sockaddr_in addr2){
    if((addr1.sin_port==addr2.sin_port) && (addr1.sin_addr.s_addr==addr2.sin_addr.s_addr)){
        return 0;
    }
    else{
        return 1;
    }
}

int RRQisDuplicated(struct FileNode *head, struct FileNode *tail ,char *filename, struct sockaddr_in client_addr){
    struct FileNode *current = head->next;
    while(current != tail){
        if((strcmp(current->filename, filename) == 0) && (addresscmp(current->client_addr, client_addr)== 0)){
            return 1;
        }
        current = current->next;
    }
    return 0;
}

int ACKCheck(unsigned short int blocknumber, struct FileNode *Node){
	if(Node->blocknumber -1 == blocknumber){
		return 0;
	}
    else{
    	return 1;
    }
}


int Send_Data_Packet(struct FileNode *Node ,int ACK, socklen_t addr_len){
	struct FileNode * current = Node;
	int recentsize;
	int blocknumber;
	short unsigned opcode;
	struct DATA_Packet DataPacket ;		
		
	opcode=3;//packet opcode
	opcode=htons(opcode);
	DataPacket.opcode=opcode;

	


	/* lastsize=fread(&dataPacket.data,1,512,temp_ptr->fp_local); *///how many bytes have been read  //packet data

	if(!ACK)
	{
		if(current->recentsize<512){
			close(current->socketfd);//close the fd 
			printf("complete the transmission to the socket %d\n",current->socketfd);
			 printf("##########################################################################\n");
			return 1;
		}
		
		else{
		    printf("##############SEND DATA OF %s OF Packet %d#################\n", current->filename,current->blocknumber);
			recentsize=fread(DataPacket.data,1,512,current->fp);
			printf("the packet size is %d\n",recentsize+4);
			//printf("%s\n",dataPacket.data);
			current->recentsize=recentsize;//return lastsize to node
			blocknumber=htons(current->blocknumber);//packet block_no
			DataPacket.blocknumber=blocknumber;
			printf("Send blocknumber:%d of file:%s to the socket %d\n",current->blocknumber,current->filename,current->socketfd);
			sendto(current->socketfd,&DataPacket,recentsize+4,0,(struct sockaddr*)&(current->client_addr),addr_len);//send a normal packet
			current->blocknumber+=1;
			return 0;
		}
	}
	else{
	//resend the former packet
	printf("####################RESEND DATA OF %s##############################\n", current->filename);
	fseek(current->fp,-(current->recentsize),1);//move the current->fp to former block
	current->blocknumber-=1;
	blocknumber=htons(current->blocknumber);//packet blocknumber
	DataPacket.blocknumber=blocknumber;
	recentsize=fread(&DataPacket.data,1,512,current->fp);
	current->recentsize=recentsize;//return lastsize to node
	printf("RESEND block no.%d to the socket %d\n",current->blocknumber,current->socketfd);
	sendto(current->socketfd,&DataPacket,recentsize+4,0,(struct sockaddr*)&(current->client_addr),addr_len);//resend last packet smaller than 512 byte
	current->blocknumber+=1;
	return 0;

	}
}



void Send_Error_Packet(int socketfd ,unsigned short int errornumber,struct sockaddr_in* client_addr,socklen_t addr_len){
	struct ERROR_Packet ErrorPacket;
	short unsigned opcode;
	ErrorPacket.errornumber=htons(errornumber);
	opcode=5;
	opcode=htons(opcode);
	ErrorPacket.opcode=opcode;
	printf("%s\n",errormsg[errornumber]);
	strcpy(ErrorPacket.error,errormsg[errornumber]);
	printf("send ERROR no.%d to the socket %d\n",errornumber,socketfd);
	sendto(socketfd,&ErrorPacket,sizeof(ErrorPacket),0,(struct sockaddr*)client_addr,addr_len);
}
