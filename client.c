/*Client.c*/


#include "unpifiplus.h"
#include "udprtt.h"
#include <setjmp.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#define RTT_DEBU




/*Global declarations*/

extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
extern        void      free_ifi_info_plus(struct ifi_info *ifihead);

/*Stores Client parameters*/
struct client_arguments {
    char IP_file[INET_ADDRSTRLEN];
    int portno;
    char file_name[1024];
    int window_size;
    int rwindow;
    float seed;
    float ploss;
    int mean;
    int expected_seq;
    int acktosend;
    
}client_data;

/* Stores IP configuration of the client*/
struct interface
{   char if_name[1024];
    struct sockaddr_in  ipaddr;	/* primary address */
    struct sockaddr_in  netmaskaddr;
    struct sockaddr_in  subnetaddr;
}record[10];

struct sockaddr_in IPclient;
struct sockaddr_in IPserver;
struct msghdr msgsend,msgrecv;
static struct hdr {
    int rwnd;
    int control;
    uint32_t seq;               /* sequence # */
    uint32_t ts;                /* timestamp when sent */
} sendhdr, recvhdr;
struct iovec iovsend[2],iovrecv[2];
static sigjmp_buf jmpbuf;
static struct rtt_info rttinfo;
static int rttinit = 0;


int n_interfaces; // Number of interfaces

//Start Timer Structure
void starttimer(uint32_t timerinfo)
{
    struct itimerval value;
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_usec = 0;
    value.it_value.tv_sec = 0;
    value.it_value.tv_usec = (timerinfo);
    setitimer(ITIMER_REAL, &value, 0);
}

//Stop Timer Structure
void stoptimer(void){
    
    struct itimerval value;
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_usec = 0;
    value.it_value.tv_sec = 0;
    value.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &value, 0);
}


static void sig_alrm(int signo) //Setting SIGJUMP for race conditions
{
    siglongjmp(jmpbuf, 1);
}



int print_record() // Print Record of interfaces structure
{   int i=0;
    char net_mask[INET_ADDRSTRLEN],ipaddress[INET_ADDRSTRLEN],subnet[INET_ADDRSTRLEN];
    
    printf("\n Number of interfaces is %d",n_interfaces);
    for(i=0;i<n_interfaces;i++)
    {
        printf("\n Interface name %s \n",record[i].if_name);
        
        inet_ntop(AF_INET, &(record[i].ipaddr.sin_addr),ipaddress, INET_ADDRSTRLEN);
        printf("\n IP address : %s", ipaddress);
        
        inet_ntop(AF_INET,  &(record[i].netmaskaddr.sin_addr),net_mask, INET_ADDRSTRLEN);
        printf("\n Net Mask Address : % s",net_mask );
        
        inet_ntop(AF_INET,  &(record[i].subnetaddr.sin_addr),subnet, INET_ADDRSTRLEN);
        printf("\n Subnet Address : % s",subnet);
        printf("\n \n");
        
        
    }
}
/*This function sends the file name to the server.It is backed up by a timeout function. Based on the condition if the control packet is a new request or a duplicate request, control bit is set to 0 and 10 respectively */


int sendfilenametoserver(int sockfd,int control_bit)
{
    
    char string[MAXLINE];
    int bytes_rcvd;
    if (rttinit == 0)
    {
        rtt_init(&rttinfo);     /* first time we're called */
        rttinit = 1;
        rtt_d_flag1 = 1;
				}
    
				
    strcpy(string,client_data.file_name);
    printf("\n Requesting file %s \n",client_data.file_name);
				
				
    
				
				sendhdr.control=control_bit; // Setting the control bit to 0 or 10 for identifying duplicates
				msgsend.msg_name = NULL;
				msgsend.msg_namelen = 0;
				msgsend.msg_iov = iovsend;
				msgsend.msg_iovlen = 2;
    
				iovsend[0].iov_base = (void *)&sendhdr;
				iovsend[0].iov_len = sizeof(struct hdr);
				iovsend[1].iov_base = string;
				iovsend[1].iov_len = strlen(string);
    
    
				msgrecv.msg_name = NULL;
				msgrecv.msg_namelen = 0;
				msgrecv.msg_iov = iovrecv;
				msgrecv.msg_iovlen = 1;
    
				iovrecv[0].iov_base = (void *)&recvhdr;
				iovrecv[0].iov_len = sizeof(struct hdr);
    
				Signal(SIGALRM, sig_alrm);
    
				rtt_newpack1(&rttinfo);      /* initialize for this packet */
				
sendagain:
    sendhdr.ts = rtt_ts(&rttinfo);
    
    printf("\n \n Sending file name to server \n \n");
				sendmsg(sockfd, &msgsend, 0);
				
    
				starttimer(rtt_start1(&rttinfo));
				
    /*Timeout function for receiving acknowledgement for sending filename */
    
				if (sigsetjmp(jmpbuf, 1) != 0) {
                    if (rtt_timeout1(&rttinfo) < 0) {
                        err_msg("\n no response from client, giving up \n");
                        rttinit = 0;        /* reinit in case we're called again */
                        errno = ETIMEDOUT;
                        return (-1);
                    }
                    goto sendagain;
                }
    bytes_rcvd = recvmsg(sockfd, &msgrecv, 0);
				if(recvhdr.seq = sendhdr.seq){
                    printf("\n Acknowledgement received from the server for the filename \n \n");
                }
    
				stoptimer();
    
				/* Calculate & store new RTT estimator values */
				rtt_stop1(&rttinfo, rtt_ts1(&rttinfo) - ntohl(recvhdr.ts));
    return 0;
}

/* This function handles the initial connection namely, creation of a socket, binds onto an ephemeral port and later calls the function to send the file name to server. The argument to this functions takes an integer to check if a function is local */



int initial_connect(int local)
{
    int sockfd,portno;
    struct sockaddr_in caddr,saddr,ss,ss1;
    struct sockaddr_in client_addr,server_addr;
    socklen_t len,s_len;
    char str[INET_ADDRSTRLEN],str1[INET_ADDRSTRLEN];
    int breturn;
    const int on=1;
    
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("Socket creation failed (%d)\n",
               errno);
        return -1;
    }
    
    
    Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)); // Socket is set to be reusable.
    
    
    //If server is local, set the SO_DONTROUTE option
    if (local==1)
    {
        setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE,&on, sizeof(on));
        printf("\n \n ******************Server is local and SO_DONTROUTE option set********************\n\n");
    }
    
    caddr.sin_addr.s_addr = IPclient.sin_addr.s_addr;
    caddr.sin_family = AF_INET;
    caddr.sin_port=htons(0);
    
    //Bind
    Bind(sockfd, (SA *)(&caddr), sizeof(struct sockaddr));
    
    
    // Get sock name
    len = sizeof(struct sockaddr);
    
    //Error checking
    getsockname(sockfd, (struct sockaddr *) &client_addr, &len);
    
    
    printf("\n Client socket bound to %s port %d\n",
           inet_ntop(AF_INET, &client_addr.sin_addr, str1, sizeof(str1)),ntohs(client_addr.sin_port));
    
    
    
    
    // Connect
    saddr.sin_addr.s_addr = IPserver.sin_addr.s_addr;
    saddr.sin_family = AF_INET;
    
    //Well known port number of the server
    saddr.sin_port=htons(client_data.portno);
    
    Connect(sockfd,(struct sockaddr *)&saddr, sizeof(struct sockaddr));
    
    //Get peer name
    
    
    getpeername(sockfd, (struct sockaddr *) &server_addr, &len);
    
    
    printf("\n Client socket connected to %s port %d\n",inet_ntop(AF_INET, &server_addr.sin_addr, str1, sizeof(str1)),ntohs(server_addr.sin_port));
    
    
    
    sendfilenametoserver(sockfd,0); // Requesting the file to the server.
    
    
    printf("\n File requested to server \n");
    
    
    return sockfd;
    
    
}


//Routine to check if host is local
int check_host_local()
{
    int i,count;
    int k; //Check for local
    struct sockaddr_in ipaddr,temp_addr;
    int subnetmax=-100;
    int setbits;
    
    
    
    //Checking for same host
    inet_pton(AF_INET, client_data.IP_file, &ipaddr.sin_addr);
    for (i = 0; i <n_interfaces; i++)
    {
        if (ipaddr.sin_addr.s_addr == record[i].ipaddr.sin_addr.s_addr)
        {
            //Same host - Set local and loopback address.
            
            printf("\n \n ********Server is on the same host and local*************\n \n");
            k=1;
            IPclient.sin_addr.s_addr=record[i].ipaddr.sin_addr.s_addr;
            IPserver.sin_addr.s_addr=record[i].ipaddr.sin_addr.s_addr;
            return 1;
        }
    }
    
    //Checking for same network
    
    inet_pton(AF_INET, client_data.IP_file, &temp_addr.sin_addr);
    
    for (count = 0; count <n_interfaces; count++)
    {
        
        if ((record[count].netmaskaddr.sin_addr.s_addr & temp_addr.sin_addr.s_addr) ==
            record[count].subnetaddr.sin_addr.s_addr)
        {
            setbits=0;
            
            
            //Longest prefixing matching
            while (record[count].netmaskaddr.sin_addr.s_addr)
            {
                
                (record[count].netmaskaddr.sin_addr.s_addr) = (record[count].netmaskaddr.sin_addr.s_addr) & ((record[count].netmaskaddr.sin_addr.s_addr) - 1);
                setbits++;
            }
            
            
            //IPserver and IPclient are assigned addresses based on longest prefix match.
            if (setbits > subnetmax)
            {
                subnetmax = setbits;
                IPclient.sin_addr.s_addr=record[count].ipaddr.sin_addr.s_addr;
                IPserver.sin_addr.s_addr=temp_addr.sin_addr.s_addr;
                k = 1;
            }
        }
    }
    
    // Check for local
    if (k==1)
    {
        printf(" \n \n**************Server is local************\n \n");
        return 1;
    }
    
    //If not local, assign client IP arbitrarily and assign IPserver from client.in
    else if (k==0)
    {
        printf(" \n Server is not local \n");
        IPclient.sin_addr.s_addr=record[0].ipaddr.sin_addr.s_addr;
        IPserver.sin_addr.s_addr=temp_addr.sin_addr.s_addr;
        return 0;
    }
    return 0;
    
}

//Routing to read from client.in and load up the intial parameters
int file_read()
{
    
    FILE *ifp;
    int count;
    char file_val[7][25];
    char temp[1024];
    
    
    ifp=fopen("client.in","r");
    
    if (ifp == NULL)
    {
        fprintf(stderr, "Can't open input file client.in !\n");
        exit(1);
    }
    
    for(count=0;count<7;count++)
    {
        if (fgets(file_val[count],150, ifp) == NULL)
        { printf("\n Enter all the file arguments");
            exit(1);
        }
        strcpy(temp,file_val[count]);
        int length=strlen(temp)-1;
        if(temp[length]=='\n')
            temp[length]='\0';
        
        strcpy(file_val[count],temp);
        // printf("%3d: %s \n", count, file_val[count]);
        
    }
    
    //IP address of the server loaded
    strcpy(client_data.IP_file,file_val[0]);
    printf("\n The IP address of the server read from the fie is %s\n",client_data.IP_file);
    
    //Port number
    client_data.portno=atoi(file_val[1]);
    printf("\n  The  Well known port number of the server is %d\n",client_data.portno );
    
    //File name to be transferred
    strcpy(client_data.file_name,file_val[2]);
    printf("\n The file name to be transferred is %s \n",client_data.file_name);
    
    //receiver sliding window size
    client_data.window_size=atoi(file_val[3]);
    printf("\n  The  receiver sliding window size %d\n",client_data.window_size );
    client_data.rwindow=client_data.window_size; // Initializing the window size to max window size in the first step.
    
    //Seed value
    client_data.seed=atof(file_val[4]);
    printf("\n  The random number generator seed value is %f\n",client_data.seed );
    
    //Probability of datagram loss
    client_data.ploss=atof(file_val[5]);
    printf("\n  The  probability of datagram loss is  %f\n",client_data.ploss );
    
    //Mean distribution
    client_data.mean=atoi(file_val[6]);
    printf("\n  The  mean u in milliseconds is %d\n",client_data.mean );
    
    return 0;
    
}


//Routine to print IP interfaces of the client
int Ip_interface()
{
    int family = AF_INET;
    int doaliases=1;
    int i;
    int iface = 0;
    struct ifi_info *ifi, *ifihead;
    struct sockaddr_in *sa, *net_mask;
    char ipaddress[INET_ADDRSTRLEN], netmaskaddr[INET_ADDRSTRLEN], subnet[INET_ADDRSTRLEN];
    u_char		*ptr;
    
    for (ifihead = ifi = Get_ifi_info_plus(family, doaliases);ifi != NULL; ifi = ifi->ifi_next)
    {
        strcpy(record[iface].if_name,ifi->ifi_name);
        
        
        if (ifi->ifi_index != 0)
            printf("(%d) ", ifi->ifi_index);
        printf("<");
        
        /* *INDENT-OFF* */
        if (ifi->ifi_flags & IFF_UP)			printf("UP ");
        if (ifi->ifi_flags & IFF_BROADCAST)		printf("BCAST ");
        if (ifi->ifi_flags & IFF_MULTICAST)		printf("MCAST ");
        if (ifi->ifi_flags & IFF_LOOPBACK)		printf("LOOP ");
        if (ifi->ifi_flags & IFF_POINTOPOINT)	printf("P2P ");
        printf(">\n");
        /* *INDENT-ON* */
        
        if ( (i = ifi->ifi_hlen) > 0) {
            ptr = ifi->ifi_haddr;
            do {
                printf("%s%x", (i == ifi->ifi_hlen) ? "  " : ":", *ptr++);
            } while (--i > 0);
            printf("\n");
        }
        
        
        
        
        printf("\n Interface is: %s ",record[iface].if_name);
        if (ifi->ifi_index != 0)
            printf("(%d) ", ifi->ifi_index);
        
        if ( (i = ifi->ifi_hlen) > 0) {
            
            ptr = ifi->ifi_haddr;
            
        }
        
        
        
        //IP address
        sa = (struct sockaddr_in *)ifi->ifi_addr;
        record[iface].ipaddr.sin_addr.s_addr=sa->sin_addr.s_addr;
        
        inet_ntop(AF_INET, &(record[iface].ipaddr.sin_addr),ipaddress, INET_ADDRSTRLEN);
        printf("\n IP address : %s",ipaddress);
        
        //Net mask address
        net_mask = (struct sockaddr_in *)ifi->ifi_ntmaddr;
        record[iface].netmaskaddr.sin_addr.s_addr=net_mask->sin_addr.s_addr;
        inet_ntop(AF_INET,  &(record[iface].netmaskaddr.sin_addr),netmaskaddr, INET_ADDRSTRLEN);
        printf("\n Net Mask Address : % s" ,netmaskaddr );
        
        //Subnet calculations
        record[iface].subnetaddr.sin_addr.s_addr = (record[iface].ipaddr.sin_addr.s_addr & record[iface].netmaskaddr.sin_addr.s_addr);
        printf("\n Subnet Address : % s" , inet_ntop(AF_INET,  &(record[iface].subnetaddr.sin_addr),subnet, INET_ADDRSTRLEN));
        
        iface++;
    }
    
    
    free_ifi_info_plus(ifihead);
    n_interfaces=iface;
    return 0;
}

/*This Function returns 1 if datagram is not lost*/
int probability()
{
    double  probtrans;
    srand(client_data.seed);
    probtrans=drand48();
    
    
    if(probtrans > client_data.ploss)//Datagram not lost
    {
        printf("\n Datagram not lost \n");
        return 1;
    }
    
    else //Loss
    {
        printf("\n Datagram Lost \n");
        return 0;
    }
    
}

/* Reconnecting port to the ephemeral of the server */
int reconnect(int sockfd)
{
    char mesg[MAXLINE],str1[INET_ADDRSTRLEN],recvline[MAXLINE],string[MAXLINE];
    int nbytes,server_port;
    struct sockaddr_in saddr,server_addr;
    socklen_t len;
    //receive ephemeral portno of server
    
    
    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    
    iovrecv[0].iov_base = (void *)&recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);
    iovrecv[1].iov_base = recvline;
    iovrecv[1].iov_len = 512;
    
    //Initialize packet for acknowledgement
    strcpy(string,"Acknowledgement");
    
    
    msgsend.msg_name = NULL;
    msgsend.msg_namelen = 0;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 1;
    
    iovsend[0].iov_base = (void *)&sendhdr;
    iovsend[0].iov_len = sizeof(struct hdr);
    printf("\n      Receiving ephemeral port number from server    \n");
readagain:
    
    nbytes = recvmsg(sockfd, &msgrecv, 0);
    
    sendhdr.seq=recvhdr.seq;
    
    //If data received check for probability and then send acknowledgement
    if (nbytes>0)
    {
        printf("Receiving ephemeral port number from server.....\n");
        if(probability() == 1) /* If no loss*/
        {
            
            
            printf("\n The received port number is: %s\n",recvline);
            
            
            
            server_port = atoi(recvline);
            saddr.sin_addr.s_addr = IPserver.sin_addr.s_addr;
            saddr.sin_family = AF_INET;
            saddr.sin_port=htons(server_port);
            
            Connect(sockfd,(struct sockaddr *)&saddr, sizeof(struct sockaddr));
            
            len=sizeof(struct sockaddr);
            if (getpeername(sockfd, (struct sockaddr *) &server_addr, &len)==-1)
                perror("getpeername error!\n");
            
            printf("\n Client socket is re-connected to %s port %d\n",
                   inet_ntop(AF_INET, &server_addr.sin_addr, str1, sizeof(str1)),ntohs(server_addr.sin_port));
            
            
            sendhdr.rwnd=client_data.window_size; // Setting the window size
            sendmsg(sockfd, &msgsend, 0);
            
            printf (" \n Acknowledgement sent for connection to ephemeral port number of server \n");
        }
        
        else
        {   /* If the datagram is lost, file is again requested to server*/
            printf("Datagram lost");
            
            sendfilenametoserver(sockfd,10); // File resent with the control bit 10 to indicate duplicate request
            
            printf("\n File re-requested to server \n");
            
            
            goto readagain;
        }
        
    }
    
    
    else
    {  /* If the datagram is lost, file is again requested to server*/
        printf("Datagram lost");
        
        sendfilenametoserver(sockfd,10); // File resent with the control bit 10 to indicate duplicate request
        
        printf("\n File re-requested to server \n");
        
        
        goto readagain;
    }
    
    
    return 0;
    
}


/*Sleep function to read the contents of buffer onto stdout*/
void sleep_function()
{
    
    int sleep_time;
    float u=client_data.mean;
    double val=drand48();
    if(val==0)
        val+=0.5;
    sleep_time= (int)((-1)*u*(log(val)));
    usleep(sleep_time);
    
}


//Thread to print out the contents of buffer to the stdout*/
void *fileoutput_printer(void *arg)
{
    
    
    // Client sleeps for U microseconds before printing
    char *buf=(char*)arg;
    sleep_function;
    printf("\n$$ DATAGRAM: $$\n\n");
    printf("\n %s \n",buf); // Printing the datagram
    client_data.rwindow--;
    
    if(client_data.rwindow==0) //Checking for locked window
    {
        printf("\n$$$$$$$$$$$$$$ Window locked $$$$$$$$$$$$$$$\n");
        sendhdr.control=6; // Setting the control bit to locked
    }
    return(NULL);
}


/*This routine handles the data receiving and sending and acknowledgement. Each datagram is backed up by an ARQ mechanism*/
int data_receive_ack(int sockfd)
{
    char recvline[512];
    int n_bytes;
    char string[512];
    pthread_t pr_id;
    
    
    
    
    sendhdr.rwnd=client_data.window_size; // Assigning the header window of the client to max window size.
    
    // Initialize packet for sending
    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    
    iovrecv[0].iov_base = (void *)&recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);
    iovrecv[1].iov_base = recvline;
    iovrecv[1].iov_len = 512;
    
    //Initialize packet for acknowledgement
    strcpy(string,"Acknowledgement");
    
    
    msgsend.msg_name = NULL;
    msgsend.msg_namelen = 0;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 1;
    
    iovsend[0].iov_base = (void *)&sendhdr;
    iovsend[0].iov_len = sizeof(struct hdr);
    
    printf("Receiving packet data from server.....\n");
    
    
    client_data.expected_seq=1; //Intial value of expected sequence
    client_data.acktosend=0;
    while(1)
    {
    readagain:
        n_bytes = recvmsg(sockfd, &msgrecv, 0);
        if (n_bytes>0)
        {   printf(" \n Received Sequence number  %d \n",recvhdr.seq);
            
            
            if(recvhdr.seq==-1) //If End of file transfer
            {
                printf("\n End of File transfer. Exiting client \n \n");
                exit(0);
            }
            
            else if(probability() == 1)//If no loss
            {
                if(recvhdr.seq!=-1 && recvhdr.control==2) //Checking for not EOF
                {
                    if(recvhdr.seq==client_data.expected_seq) //Expected = Current arrived sequence
                    {
                        client_data.acktosend=client_data.expected_seq;
                        printf(" \n \n Number of bytes received is %d\n",n_bytes);
                        
                        
                        sendhdr.seq=client_data.acktosend; // Sender acknowledgement sequence is current acknowledgement
                        
                        
                        //printf("Sending acknowledgment for packet data to server.....\n");
                        sendmsg(sockfd, &msgsend, 0);
                        printf(" \n Acknowledgement no %d sent \n",sendhdr.seq);
                        
                        client_data.expected_seq++;
                        client_data.acktosend=client_data.expected_seq-1;
                        //printf(" \n        \n %s  ",recvline); // Printing the file
                        client_data.rwindow++;
                        
                        Pthread_create(&pr_id, NULL, &fileoutput_printer, (void*)recvline); // Thread for printer of data
                        pthread_join(pr_id, NULL);
                        bzero(&recvline,sizeof(recvline));
                        
                        
                        
                    }
                    
                    
                    else if(recvhdr.seq>client_data.expected_seq) // Out of order packet.
                    {
                        sendhdr.seq=client_data.acktosend;
                        printf("\n Out of order packet \n");
                        sendmsg(sockfd, &msgsend, 0);
                        printf(" \n Acknowledgement no %d sent \n",sendhdr.seq); //Duplicate acknowledgement sent
                        
                        
                        
                    }
                    
                    else if(recvhdr.seq<client_data.expected_seq) // Error checking
                    {
                        printf("\n Error in sequencing. Terminating program \n");
                        exit(0);
                    }
                }
                
                else if(recvhdr.control=6) // Window advertisement. Checks for the window status and responds to the status accordingly*/
                {
                    if (client_data.rwindow>0)
                    {
                        sendhdr.control=2; // Control bit set to data transfer
                        sendhdr.rwnd=client_data.rwindow;
                        printf("$$$$$$$$  Receiver Window is free $$$$$$$$");
                        
                    }
                    else if(client_data.rwindow<0){
                        sendhdr.control=6; //Control bit set for locking window
                        sendhdr.rwnd=client_data.rwindow;
                        printf("$$$$$$$$  Receiver Window is free $$$$$$$$");
                    }
                    sendmsg(sockfd, &msgsend, 0);
                    
                    
                }
                else if(recvhdr.control==11 ) //If end of file transfer
                {
                    sendhdr.control=11; // Control bit set to end of file transfer
                    
                    sendmsg(sockfd, &msgsend, 0); //Send acknowledgement to indicate that the server has received the end of file transfer message
                    
                    printf("\n Sending acknowledgment for end of data transfer- Terminating client \n");
                    
                    exit(0); //Exit client
                    
                    
                }
                
                
                
            }
            
            
            else  // Datagram is lost in network. Sending acknowledgement to receive expected sequence
            {
                printf("\n Datagram lost \n");
                sendhdr.seq=client_data.acktosend; // acktosend=expected sequence - 1
                
                
                sendmsg(sockfd, &msgsend, 0);
                printf(" \n Acknowledgement no %d sent \n",sendhdr.seq);
                
                
            }
        }
        
        
        else // If no bytes are received
        {
            printf("\n No bytes received \n");
            goto readagain;
        }
    }
    return 0;
}







/*Main Driver program*/

int main(int argc, char **argv)
{
    int sockfd;
    int local;
    pthread_t t_id;
    
    file_read(); // File read
    
    Ip_interface(); //Interfaces are loaded
    
    local= check_host_local(); // Check to see if server is local
    
    
    
    sockfd = initial_connect(local); //Connection to listening socket of server
    
    
    
    reconnect(sockfd); /*Requesting the server for the file*/
    
    
    
    data_receive_ack(sockfd); //Receiving the data
    
    exit(0); //Exit client
    
}
