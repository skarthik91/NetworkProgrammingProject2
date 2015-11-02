/*UDP Server*/

#include    "unpifiplus.h"
#include    "udprtt.h"
#include <math.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <sys/time.h>
#define RTT_DEBU

/*Global Declarations*/

//Structure to store the interface information
struct interface
{
    int sockfd;
    struct sockaddr_in  ipaddr;	/* primary address */
    struct sockaddr_in  netmaskaddr;
    struct sockaddr_in  subnetaddr;
}record[10];

struct server_arguments
{
    int port;
    int windowsize;
}server_data;

//Structure to store the parameters related to flow and congestion control
struct congestion_flow
{
    int previous_ack;
    int duplicate_ack;
    int numberofacks;
    int expected_ack;
    int current_seq;
    int cwnd;
    int ssthresh;
    int current_state;
    int npacketsleft;
    int packets_to_send;
    int client_window;
}info;


struct sockaddr_in IPclient;
struct sockaddr_in IPserver;
struct msghdr msgsend,msgrecv;
static struct rtt_info rttinfo;
static int rttinit = 0;

//Header declaration
static struct hdr {
    int window;
    int control;
    uint32_t seq;               /* sequence # */
    uint32_t ts;                /* timestamp when sent */
} sendhdr, recvhdr;



struct iovec iovsend[2],iovrecv[2];
static sigjmp_buf jmpbuf;
int packetsize;

int n_interfaces; // Number of interfaces


//Timer structure for starting timer
void starttimer(uint32_t timerinfo){
    
    struct itimerval value;
    
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_usec = 0;
    value.it_value.tv_sec = 0;
    value.it_value.tv_sec = 0;
    value.it_value.tv_usec = timerinfo;
    setitimer(ITIMER_REAL, &value, 0);
}



//Timer structure for stopping timer
void stoptimer(void){
    
    struct itimerval value;
    
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_usec = 0;
    value.it_value.tv_sec = 0;
    value.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &value, 0);
}

//Handles the child termination process
void sigchld_handler(int signum)
{
    int pid, status, serrno;
    serrno = errno;
    while ((pid = waitpid(-1, &status, WNOHANG)) > -1)
    {
        Fputs("\n Child terminates \n",stdout);
    }
    errno = serrno;
}

/*Reads the file and stores the server parameters*/
void file_read()
{
    
    FILE *f;
    int i;
    int line[2];
    f=fopen("server.in","r");
    
    if (f == NULL)
    {
        fprintf(stderr, "Can't open input file client.in !\n");
        exit(1);
    }
    
    for (i = 0; i < 2 ; i++)
    {
        fscanf(f, "%d", &line[i]);
    }
    
    //IP address of the server loaded
    server_data.port = line[0];
    printf("server port: %d\n",server_data.port);
    
    //Port number
    server_data.windowsize = line[1];
    printf("window size: %d\n",server_data.windowsize );
    
    
}


/*Routine to get the IP interfaces information  of the server*/
int Ip_interface()
{
    int family = AF_INET;
    int doaliases=1;
    int i,sockfd;
    int j = 0;
    struct ifi_info *ifi, *ifihead;
    struct sockaddr_in *sa;
    char ipaddress[INET_ADDRSTRLEN], netmaskaddr[INET_ADDRSTRLEN], subnet[INET_ADDRSTRLEN];
    const int on = 1;
    printf("Interface List: %s\n");
    for (ifihead = ifi = Get_ifi_info_plus(family, doaliases);ifi != NULL; ifi = ifi->ifi_next)
    {

        sockfd = Socket(family, SOCK_DGRAM, 0);
        record[j].sockfd = sockfd;

        //Set socket address as reusable
        Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        
        //IP Address
        if ( (sa = (struct sockaddr_in *)ifi->ifi_addr) != NULL){
            record[j].ipaddr.sin_addr.s_addr = sa->sin_addr.s_addr;
            inet_ntop(family,&(record[j].ipaddr.sin_addr),ipaddress, INET_ADDRSTRLEN);
            printf("ipaddress: %s\n",ipaddress);
            
        }
        
        //NETMASK Address
        if ( (sa = (struct sockaddr_in *)ifi->ifi_ntmaddr) != NULL){
            record[j].netmaskaddr.sin_addr.s_addr = sa->sin_addr.s_addr;
            inet_ntop(family,&(record[j].netmaskaddr.sin_addr),netmaskaddr, INET_ADDRSTRLEN);
            printf("netmaskaddr: %s\n",netmaskaddr);
        }
        
        //Subnet calculations
        record[j].subnetaddr.sin_addr.s_addr = (record[j].ipaddr.sin_addr.s_addr & record[j].netmaskaddr.sin_addr.s_addr);
        printf("Subnet Address : % s\n" , inet_ntop(family,&(record[j].subnetaddr.sin_addr),subnet, INET_ADDRSTRLEN));
        
        /* Start bind address */
        bzero(&sa, sizeof(struct sockaddr_in));
        sa = (struct sockaddr_in *) ifi->ifi_addr;
        sa->sin_family = family;
        sa->sin_port = htons(server_data.port);
        bind(sockfd, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
        printf("bound %s \n", Sock_ntop((SA *) sa, sizeof(*sa)));
        /* End bind address */
        printf("\n");
        j++;
    }
    
    n_interfaces=j;
    free_ifi_info_plus(ifihead);
    return 0;
}

/*Routine to check if host is local*/

int check_host_local(int listensockfd,struct sockaddr_in cliaddr){
    printf("\n Child checking if server client on same host or not.....\n");
    int i,on = 1;
    struct sockaddr_in clisubnet;
    for (i = 0; i < n_interfaces; i++)
    {
        if (record[i].sockfd == listensockfd)
        {
            IPclient.sin_addr.s_addr= cliaddr.sin_addr.s_addr;
            IPserver.sin_addr.s_addr=record[i].ipaddr.sin_addr.s_addr;
            if(IPclient.sin_addr.s_addr == IPserver.sin_addr.s_addr){
                printf("\n \n ********Client is on the same host and local*************\n \n");
                if (setsockopt(listensockfd, SOL_SOCKET, SO_DONTROUTE,&on, sizeof(on)) < 0) {
                    printf("failed to set SO_DONTROUTE (%d)\n",errno);
                    return -1;
                }
            }
            else{
                printf("\n \n ********Client is not on the same host*************\n \n");
                clisubnet.sin_addr.s_addr = record[i].netmaskaddr.sin_addr.s_addr & cliaddr.sin_addr.s_addr ;
                if(clisubnet.sin_addr.s_addr == record[i].subnetaddr.sin_addr.s_addr){
                    printf("\n \n ********Client is on same network*************\n \n");
                }else{
                    printf("\n \n ********Client is on different network*************\n \n");
                }
            }
            
        }
        
    }
    return 0;
}

/*Routine to avoid race conditions*/
static void sig_alrm(int signo)
{
    siglongjmp(jmpbuf, 1);
}


//Routing to compute cwnd based on whether we are using slow start or CA*/
int compute_cwnd()
{
    
    if(info.cwnd==0)
    {
        info.cwnd=1;
        return 0;
    }
    if(info.cwnd<info.ssthresh)
    {
        info.current_state=0;// Slow start
        info.cwnd=info.cwnd*2;
    }
    
    else if(info.cwnd>=info.ssthresh||info.current_state==2) //Congestion
    {   printf("\n Entering Congestion state \n");
        info.current_state=2; //Additive increase Multiplicative decrease
        info.cwnd++;
    }
    
    
}

/* Function to read the file and send the packet*/
int sendpacket(char buf_rcvd[],int connsockfd){
    
    int byte_rcvd_file,bytes_rcvd,bytes_rcvd1,j,i=0,k;
    float npackets;
    char *buf_send;
    char *data_send[packetsize];
    char temp[packetsize];
    int fsize;
    char string[packetsize];
    int number_of_packets;
    int f = open(buf_rcvd,O_RDONLY);
    int bytes_rcvd_persist;
    info.expected_ack=0;
    info.previous_ack=0;
    info.duplicate_ack=0;
    info.ssthresh=128;
    info.current_state=0;
    info.cwnd=0;
    recvhdr.window=info.client_window;
    
    
    printf("\n Receive Header Window is %d \n",recvhdr.window);
    
    if ( f < 0 )
    {
        printf( "Could not open file\n" );
        exit(1);
    }
    
    packetsize = 512 - sizeof(struct hdr);
    printf("\n Packet size is %d \n",packetsize);
    buf_send = (char *)malloc(packetsize);
    bzero(buf_send,packetsize);
    byte_rcvd_file=read(f, buf_send, packetsize);
    
    if(byte_rcvd_file<0)
    {
        printf("\n File open error %d \n",errno);
        exit(0);
    }
    while(byte_rcvd_file > 0)
    {
        data_send[i]=(char *)malloc(packetsize);
        
        strcpy(data_send[i],buf_send);
        bzero(buf_send,packetsize);
        byte_rcvd_file=read(f, buf_send, packetsize);
        
        i++;
        
        
    }
    
    number_of_packets=i;
    info.npacketsleft=i;
    
    printf("\n\n\n $$$$$$$$$Number of packets from the bytes_recvd_file is %d$$$$$\n\n\n",i);
    
    if (rttinit == 0) {
        rtt_init(&rttinfo);     /* first time we're called */
        rttinit = 1;
        rtt_d_flag1 = 1;
    }
    
    
    
    // Computes cwnd value
    info.packets_to_send=0;
    
    
    
    while(info.npacketsleft>0)
    {
        
        compute_cwnd(); // Compute the cwnd value
        
        
        for(k=info.packets_to_send;k< min(info.packets_to_send+info.cwnd,number_of_packets);k++) /* Minimum conditions ensures loop isnt executed when its not required*/
        {
            info.npacketsleft--;
            
            
            sendhdr.seq=k+1;
            info.packets_to_send=k+1;
            
            sendhdr.control = 2; // Setting control bit to 2 to indicate data transfer
            
            
            
            
            info.expected_ack=sendhdr.seq;
            
            //Fast retransmit
            if(info.duplicate_ack==3)
            {
                info.ssthresh=info.cwnd/2;
                info.current_state==2;
                
                for(j=info.previous_ack+1;j<=info.expected_ack;j++)
                {
                    
                    printf("\n Duplicate ack  - Fast retransmit ");
                    
                    sendhdr.control = 2;
                    sendhdr.seq=j;
                    
                    msgsend.msg_name = NULL;
                    msgsend.msg_namelen = 0;
                    msgsend.msg_iov = iovsend;
                    msgsend.msg_iovlen = 2;
                    
                    iovsend[0].iov_base = (void *)&sendhdr;
                    iovsend[0].iov_len = sizeof(struct hdr);
                    iovsend[1].iov_base = data_send[j];
                    iovsend[1].iov_len = packetsize;
                    
                    
                    msgrecv.msg_name = NULL;
                    msgrecv.msg_namelen = 0;
                    msgrecv.msg_iov = iovrecv;
                    msgrecv.msg_iovlen = 1;
                    
                    iovrecv[0].iov_base = (void *)&recvhdr;
                    iovrecv[0].iov_len = sizeof(struct hdr);
                    
                    
                    
                    
                sendagain2:
                    
                    
                    printf("Server Child retransmitting packet no %d data to client.....\n",i);
                    sendmsg(connsockfd, &msgsend, 0);
                    
                    // Acknowledgement receive
                    bytes_rcvd1 = recvmsg(connsockfd, &msgrecv, 0);
                    
                    
                    if (recvhdr.seq!=j)
                    {   printf("\n Retransmitted packet not in sequence");
                        printf(" Receive header sequence %d \n",recvhdr.seq);
                        printf(" Info expected sequence %d \n",j);
                        
                        goto sendagain2;
                    }
                    
                    printf("\n Acknowledgement received for the retransmitted packet number %d \n ",info.expected_ack);
                    
                }
                
                info.duplicate_ack=0;
                continue;
                
                
            }
            
            
            
            msgsend.msg_name = NULL;
            msgsend.msg_namelen = 0;
            msgsend.msg_iov = iovsend;
            msgsend.msg_iovlen = 2;
            
            iovsend[0].iov_base = (void *)&sendhdr;
            iovsend[0].iov_len = sizeof(struct hdr);
            iovsend[1].iov_base = data_send[k];
            iovsend[1].iov_len = packetsize;
            
            
            msgrecv.msg_name = NULL;
            msgrecv.msg_namelen = 0;
            msgrecv.msg_iov = iovrecv;
            msgrecv.msg_iovlen = 1;
            
            iovrecv[0].iov_base = (void *)&recvhdr;
            iovrecv[0].iov_len = sizeof(struct hdr);
            
            Signal(SIGALRM, sig_alrm);
            
            rtt_newpack1(&rttinfo);      /* initialize for this packet */
            
            
        sendagain:
            sendhdr.ts = rtt_ts1(&rttinfo);
            
            printf("Server Child sending packet no %d data to client.....\n",sendhdr.seq);
            
            while(recvhdr.window=0 || recvhdr.control==6) /* Persist mode */
            {
                printf("\n Client Window locked - Sending Probe message \n");
                
                
                sendhdr.control=6; // Requests a window probe
                sendmsg(connsockfd, &msgsend, 0);
                bytes_rcvd_persist = recvmsg(connsockfd, &msgrecv, 0);
                
            }
            
            sendhdr.control=2;
            sendmsg(connsockfd, &msgsend, 0);
            
            
            
            starttimer(rtt_start1(&rttinfo));
            
            if (sigsetjmp(jmpbuf, 1) != 0) {
                printf("inside timeout\n");
                if (rtt_timeout1(&rttinfo) < 0) {
                    err_msg("no response from client, giving up");
                    rttinit = 0;        /* reinit in case we're called again */
                    errno = ETIMEDOUT;
                    return (-1);
                }
                info.ssthresh=info.cwnd/2; /* Incase of a timeout - Loss has occured . Reset cwnd and ssthresh values and slow start agin*/
                info.cwnd=1;
                info.current_state=0;
                goto sendagain;
            }
            
            // Acknowledgement receive
            bytes_rcvd = recvmsg(connsockfd, &msgrecv, 0);
            
            stoptimer();
            
            /* Calculate & store new RTT estimator values */
            rtt_stop1(&rttinfo, rtt_ts1(&rttinfo) - recvhdr.ts);
            
            if(info.expected_ack==recvhdr.seq) // Expected ack is same as the received ack.
            {
                info.previous_ack=recvhdr.seq;
                printf("Acknowledgement No %d Received for packet\n",recvhdr.seq);
            }
            
            else if(info.expected_ack>recvhdr.seq)
            {
                //Checking for duplicates
                if(recvhdr.seq==info.previous_ack)
                {
                    info.duplicate_ack++;
                }
                
                
            }
            
            
        }
        
    }
    
    
    
    
    
    // End of file control probe
    sendhdr.control = 11;
    //Control byte indicates end of file transfer.
    sendhdr.seq = -1;
    Signal(SIGALRM, sig_alrm);
    
    rtt_newpack1(&rttinfo);      /* initialize for this packet */
    
sendagain1:
    sendhdr.ts = rtt_ts1(&rttinfo);
    
    //server sending control byte for EOF
    sendmsg(connsockfd, &msgsend, 0);
    
    starttimer(rtt_start1(&rttinfo));
    
    if (sigsetjmp(jmpbuf, 1) != 0) {
        if (rtt_timeout1(&rttinfo) < 0) {
            err_msg("no response from client, giving up");
            rttinit = 0;        /* reinit in case we're called again */
            errno = ETIMEDOUT;
            return (-1);
        }
        goto sendagain1;
    }
    
    //receiving acknowledgement for client detected EOF
    recvmsg(connsockfd, &msgrecv, 0);
    stoptimer();
    
    /* Calculate & store new RTT estimator values */
    rtt_stop1(&rttinfo, rtt_ts1(&rttinfo) - recvhdr.ts);
    
    
    //Indicates end of file transfer- Exit server child.
    if(recvhdr.control = 11){
        printf("Acknowledgement Received for end of file. Terminating child \n");
        exit(0);
        
    }
    
    
    
    return 0;
}


/*This routine sends the ephemeral port number to the client*/

int sendporttoclient(int listensockfd,int server_port,int connsockfd, char buf_rcvd[],struct sockaddr_in caddr){
    
    int bytes_rcvd,n_bytes,len;
    char port[MAXLINE],msgrcvd[MAXLINE],bufrecv[MAXLINE];
    printf("server port number : %d\n",server_port);
    sprintf(port, "%d", server_port);
    
    
    if (rttinit == 0) {
        rtt_init(&rttinfo);     /* first time we're called */
        rttinit = 1;
        rtt_d_flag1 = 1;
    }
    len = sizeof(struct sockaddr);
    
    msgsend.msg_name = (SA *) &caddr;
    msgsend.msg_namelen = len;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 2;
    
    iovsend[0].iov_base = (void *)&sendhdr;
    iovsend[0].iov_len = sizeof(struct hdr);
    iovsend[1].iov_base = port;
    iovsend[1].iov_len = sizeof(port);
    
    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 1;
    
    iovrecv[0].iov_base = (void *)&recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);
    
    
    Signal(SIGALRM, sig_alrm);
    
    rtt_newpack1(&rttinfo);      /* initialize for this packet */
    
    
sendagain:
    sendhdr.ts = rtt_ts1(&rttinfo);
    printf("Server Child sending ehphemral port number to client.....\n");
    sendmsg(listensockfd, &msgsend, 0);
    
    
    
    starttimer(rtt_start1(&rttinfo));
    
    if (sigsetjmp(jmpbuf, 1) != 0) {
        printf("timeout\n");
        if (rtt_timeout1(&rttinfo) < 0) {
            err_msg("no response from client, giving up");
            rttinit = 0;        /* reinit in case we're called again */
            errno = ETIMEDOUT;
            return (-1);
        }
        sendmsg(connsockfd,&msgsend,0); /*Retransmitting duplicate copy of ephemeral port number on connection socket*/
        goto sendagain;
        
    }
    
    
    bytes_rcvd = recvmsg(connsockfd, &msgrecv, 0);
    info.client_window=recvhdr.window; // Copying intial client window size
    
    stoptimer();
    /* Calculate & store new RTT estimator values */
    rtt_stop1(&rttinfo, rtt_ts1(&rttinfo) - recvhdr.ts*1000);
    
    printf("\n Acknowledgement Received for port number\n");
    
    close(listensockfd); // Closing the listen file descriptor
    sendpacket(buf_rcvd,connsockfd);
    
    
    
    return 0;
    
}

/* Server child is connected on ephemeral port number*/

int serverchildconnect(int listensockfd, struct sockaddr_in cliaddr,char buf_rcvd[])
{
    printf("Child connecting with client to handle client's request.....\n");
    int i,connsockfd,server_port;
    struct sockaddr_in saddr,caddr,*ss;
    struct sockaddr_storage server_addr;
    socklen_t len,len1;
    char str[INET_ADDRSTRLEN],str1[INET_ADDRSTRLEN];
    
    for (i = 0; i <n_interfaces; i++)
    {
        if (record[i].sockfd == listensockfd)
        {
            connsockfd = Socket(AF_INET, SOCK_DGRAM, 0);
            saddr.sin_addr.s_addr = record[i].ipaddr.sin_addr.s_addr;
            saddr.sin_family = AF_INET;
            saddr.sin_port=htons(0);
            
            Bind(connsockfd, (SA *) &saddr, sizeof(saddr));
            
            len = sizeof(server_addr);
            if (getsockname(connsockfd, (struct sockaddr *) &server_addr, &len)== -1)
                perror("getsockname error!\n");
            
            ss = (struct sockaddr_in *)&server_addr;
            server_port = ntohs(ss->sin_port);
            printf("The connection socket is bound on port number %d \n",server_port);
            inet_ntop(AF_INET, &ss->sin_addr, str, sizeof str);
            printf("The bound IP address is %s \n", str);
            
            caddr.sin_addr.s_addr = cliaddr.sin_addr.s_addr;
            caddr.sin_family = AF_INET;
            caddr.sin_port=htons(cliaddr.sin_port); //Connecting to ephemeral port number
            Connect(connsockfd,(SA *) &caddr, sizeof(caddr));
            len1 = sizeof(caddr);
            getpeername(connsockfd, (struct sockaddr *) &caddr, &len1);
            
            
            printf("\n Server socket connected to %s port %d\n",inet_ntop(AF_INET, &caddr.sin_addr, str1, sizeof(str1)),ntohs(caddr.sin_port));
            
            sendporttoclient(listensockfd,server_port,connsockfd,buf_rcvd,caddr); /* Calls this routine to send the port number to client*/
            
            
        }
    }
    return 0;
    
}


/* Server listens on all interfaces and spawns a child if a new request arrives*/

void initial_connection()

{
    
    pid_t   pid;
    fd_set  rset;
    socklen_t len;
    ssize_t bytes_rcvd;
    char buf_rcvd[MAXLINE],from_ip[INET_ADDRSTRLEN];
    int maxfdp1,maxfdp,k,nready,listensockfd,client_port,newrequest;
    struct sockaddr_in cliaddr;
    char recvline[512];
    int n_bytes;
    char string[512];
    
    for(;;){
        FD_ZERO(&rset);
        for(k =0;k<n_interfaces;k++){
            FD_SET(record[k].sockfd, &rset);
        }
        maxfdp1 = max(record[0].sockfd,record[1].sockfd);
        maxfdp = max(maxfdp1,record[2].sockfd) +1;
        if ( (nready = select(maxfdp, &rset, NULL, NULL, NULL)) < 0) // Select listens on all sockets.
        {
            if (errno == EINTR)
                continue;
            else
                err_sys("select error");
        }
        
        for(k = 0;k<n_interfaces;k++){
            if (FD_ISSET(record[k].sockfd, &rset)) {
                listensockfd = record[k].sockfd;
                len = sizeof(struct sockaddr);
                
                
                msgrecv.msg_name = (SA *) &cliaddr;
                msgrecv.msg_namelen = len;
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
                
                
                printf("Receiving filename from client.....\n");
            readagain:
                n_bytes = recvmsg(record[k].sockfd, &msgrecv, 0);
                client_port = ntohs(cliaddr.sin_port);
                printf("\n filename received : %s\n",recvline);
                printf(" \n \n Number of bytes received is %d\n",n_bytes);
                printf("\n\n client port number : %d\n",client_port);
                sendhdr.seq=recvhdr.seq;
                printf("control bit received : %d\n",recvhdr.control);
                strcpy(buf_rcvd,recvline);
                printf("Sending Acknowledgement for filename to client.....\n");
                sendmsg(record[k].sockfd, &msgsend, 0);
                
                
                
                signal(SIGCHLD, sigchld_handler); //this signal is received from kernel when child terminates
                if(recvhdr.control == 0 && recvhdr.control!=10) // Checking for duplicates . Control bit = 0 indicates new request. Control bit = 10 indicates duplicates
                {
                    printf("Creating Child for new request.....\n");
                    if ( (pid = Fork()) == 0){
                        if(record[k].sockfd != listensockfd){
                            close(record[k].sockfd);
                        }
                        
                        check_host_local(listensockfd,cliaddr); // Checking if host is local and assigning IP accordingly
                        serverchildconnect(listensockfd,cliaddr,buf_rcvd); // Server child connects to socket
                        exit(0);
                    }
                }
                
                
                
                
            }
            
        }
        
        
    }
}

int main(int argc, char **argv)
{
    
    file_read(); // Read file parameters and stores in the structure
    
    Ip_interface(); // Gets all interface info
    
    initial_connection(); // Connects with the client and transfers requested file
    
}









