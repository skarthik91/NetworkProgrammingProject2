README for CSE 533 Assignment 2 (Network Programming)
-----------------------------------------------------

Submitted By: Ashana Tayal(110478854) & Karthikeyan Swaminathan(110562357)

AIM : Reliable UDP socket client / server programming. 

We have successfully implemented a TCP like file-exchange between client and server using UDP connection  and 
UDP socket client/ server programming while adding data-transmission reliability, flow control in the client and server using TCP-like automatic repeat request (ARQ) sliding window
mechanisms.

Important changes we have implemented are as follows : 

1. Server Initialization Phase : Interface Information and multiple listening sockets (Implemented)
-------------------------------------------------------
1.1 Server successfully read data from file "server.in" and creates multiple UDP sockets to listening for incoming
  connections, one for each interface. Interface information is received using get_ifi_info_plus() which also includes netmask address of each interface.
  The received information is stored in array structure as follows: 
	struct interface{
		int sockfd;
		struct sockaddr_in  ipaddr;	
		struct sockaddr_in  netmaskaddr;
		struct sockaddr_in  subnetaddr;
	}record;

The server listens on all these interfaces for incoming connections but binding is strictly done on unicast 
addresses including loopback address.
1.2 Server then uses select to monitor the sockets it has created from incoming datagrams.

2. Client Initialization Phase : Interface Information and checking server connection (Implemented)
-------------------------------------------------------
2.1 Client successfully reads parameters from "client.in". 
2.2 Client obtain all of its interface information and associated network mask using get_ifi_info_plus() routine and stores it in array structure as follows :
	struct interface
	{   char if_name[1024];
		struct sockaddr_in  ipaddr;	/* primary address */
		struct sockaddr_in  netmaskaddr;
		struct sockaddr_in  subnetaddr;
	}record;
2.3 Client then checks if server is on the same host and if its local to its network otherwise.
If the server is on the same host IPclient and IPserver are set to loopback address 127.0.0.1

If server is local all communication occurs as "MSG_DONTROUTE". 
IPclient is set based on longest prefix match.
IPserver is chosen from the client.in file.

If server is not local IPclient is chosen arbitrarily



3. Client Connection Phase : Creating UDP sockets, binding , connecting (Implemented)
-------------------------------------------------------
3.1 Client creates a UDP socket and calls bind on IPclient, with 0 as the port number. This causes kernel to bind an ephemeral port to the socket. IPclient and the ephemeral
port number assigned to the socket are obtained by getsockname() routine.
3.2 Client connects its socket to server IP address and wel known port number and use getpeername() routine to server information 
3.3 Client then sends a filename(specified in "client.in") as datagram to the server which is backed up by timeout. Header file of datagram is as follows :
	static struct hdr {
		int rwnd;
		int control;
		uint32_t seq;               /* sequence # */
		uint32_t ts;                /* timestamp when sent */
	} sendhdr, recvhdr;	

4. Server Connection Phase : receiving and forking (Implemented)
-------------------------------------------------------
4.1 Server receives datagram from child which also carries client information in header.    
4.2 Server forks off the child only if new request comes in i.e received control bit with datagram will be 0. From here onwards all communication with client is done by child.Duplicate requests have control bit set 10.

5. Server Child : checking client connection (Implemented)
-------------------------------------------------------
5.1 child closes all the sockets inherited except one on which the client request arrived. This socket is listening socket.
5.2 It then checks wether client is local or not and displays message accordingly.If client is local, all communication occurs as MSG_DONTROUTE.

6. Server Child : creating connection socket, binding, ARQ Mechanism (Implemented)
-------------------------------------------------------
6.1 child creates a UDP connection socket to handle file transfer to the client and binds to server IP address with ephemeral port number assigned by kernel.
6.2 After this getsockname() routine is used to print IPserver and the ephemeral port number that has been assigned to the socket.
6.3 Child connects this connection socket to the client’s IPclient and ephemeral port number.
6.4 The server send ephemeral port number of its connection socket as datagram payload using the listening socket. This datagram is backed up with timeout if acknowledgment is not received from client 
and is retransmitted in the event of loss. Header of datagram is same as that of client.
6.5 In the case of datagram loss, the client re-transmits the file request.
6.6 The server takes care to not spawn a new child for this request


7. Client : Receiving server port number (Implemented)
-------------------------------------------------------
7.1 client reconnects its socket to server�s connection socket through ephemeral port number received as datagram and sends an acknowledgment on same socket
7.2 If server times out, server retransmit two copies of its ephemeral port number message, one on its listening socket and the other on connection socket.
7.3 If client times out it retransmits original file request and this duplicate request is taken care of by server through control bit which is set to 10 for duplicate request.
7.4 Server on the other hand, when receives the acknowledgment, it closes the listening socket it inherited and start file transfer through connection socket

8. Client : Probability check for every datagram received (Implemented)
-------------------------------------------------------

8.1 Whenever a datagram arrives the client use srand() to set the seed value for random function and drand48()(Generates uniform distribution between 0 and 1) to generate random probability of transmission numbers between 0 to 1
8.2 Probability of datagram loss is specified in client.in file. If random generated probability of transmission is greater than probability of loss then client accepts the datagram successfully otherwise it prints "Datagram lost message"

-------------------------
ADDING RELIABILITY TO UDP
-------------------------

1. Modifications done to the RTT library functions in rtt.c and unprtt.h  (Implemented)
-------------------------------------------------------

1.1 Modified files are present as udprtt.c and udprtt.h in project
1.2 Timers for detecting timeouts and retransmitting lost packets and have been implemented using the rtt routines provided in rtt.c and since timeout values are in the sub-second range, we have used itimers instead of alarm().
1.3 The functions rtt_init(), rtt_ts(), rtt_start() and rtt_stop() have been modified to measure time in milliseconds rather than seconds. This modification has been done as the measured RTT in the compserv machines were in the sub-second range.
1.4 All the rtt functions have been modified to compute and store RTT and Smoothed RTT values as integers rather than floats. As part of this, struct rtt_info has been modified to store rtt_rtt, rtt_srtt, rtt_rto and rtt_base
as type int rather than float.
1.5 Values in unprtt.h file has been changed as follows :
		RTT_RXTMIN to 1000 msec 
		RTT_RXTMAX to 3000 msec
		RTT_MAXNREXMT to 12 

2. Flow Control and Congestion Control implementation (Implemented)
-----------------------------------------------------
2.1 Window Size : The sender and the receiver window size is specified in the server.in and client.in files respectively. On the server, this parameter represents the maximum size the congestion window can grow to. On the receiver, this
represents the maximum number of packets that can be buffered.
2.2 Handling Cumulative acknowledgements :  When packet drops , the server receives duplicate acknowledgements for lost packets. The server is equipped to handle cumulative acknowledgements.
2.3 Fast Retransmit and Fast Recovery : When a packet is lost and the server times out, we halve the current window size and store it as the new threshold. The server moves back to slow start
phase with window size of 1. It moves to congestion avoidance once the window size grows beyond the new SS threshold. 
When the server receives 3 duplicate acks, it stops its timer and retransmits the lost packet immediately. This is the fast retransmit phase.
2.4 Slow Start : The server will be in the slow start phase initially with a cwnd of 1. For every valid acknowledgement received, it increases it's window by 1 MSS (1 packet of 512 bytes).In each iteration window size doubles i.e from 1 to 2 and so on.
This continues till the server either times out on an acknowledgement or the cwnd > ssthresh/2
2.5 Congestion Avoidance : Whenever the server times out or gets 3 consecutive duplicate acknowledgements, it moves from slow start to congestion avoidance phase.In this phase, the congestion window grows linearly rather than
exponentially. The current cwnd is halved and stored as the new ssthresh. Whenever the cwnd is greater then ssthresh, the window size is increased linearly instead of exponential increase. This is implemented by counting the number of valid acks and increasing the window by 1 MSS only when 'congestion-window size' number of valid acknowledgements are received.
2.6 The client reads the file contents in its receive buffer and prints them out on stdout using a separate thread. This thread sits in a repetitive loop till all the file contents have been printed out,
It sleeps for that number of milliseconds calculated according to formula -1 * u * ln( random( ) ); wakes up to read and print all in-order file contents
available in the receive buffer at that point.
2.7 Proper Termination of the program after file transfer : Client terminates when receives end of file acknowledgment(Fin message) from server in terms of control bit i.e 11 in our case for EOF and sequence number i.e -1 for EOF. On the other hand, server child also terminates as soon as it gets acknowledgment of FIN from client side
This triggers the SIGCHLD which is delivered to server parent.
2.8 Persist mode and window probes: Incase the window locks, the server enters persist mode and continuously sends window probes to the client, with control bit set to 6. The client advertises its window size to this effect. Once the receiver window is free the server is free to transfer data.


More Information 
-----------------

1. Header Format of each datagram is :
	    ---------
	    | Window Size 
HEADER      | Control Bit (0: New Client Request , 10 : Duplicate Request, 11: EOF, 6: Window lock advertisment(Client) and Window probe(Server) , 2: Data Transfer)
	    | Sequence Number
	    | Time Stamp
	    --------

	
		
		
	

	


