/* Server code */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>

#define TCP_PORT        45210
#define UDP_PORT        45211
#define SERVER_IP       "1.1.1.1"
#define FILE_NAME       "BitMap.txt"
#define CHUNK_SIZE      6401

/*
 * Message 
 * chunk_num    header specifying position in original file
 * data         6400 byte data payload of file contents
 */
typedef struct Message
{
    int32_t chunk_num;
    char data[CHUNK_SIZE];
} Message;

/*
 * Global variables 
 * 
 * done_sending     determines when client is done receiving a batch of messages
 * all_sent         determines when client has received all messages
 * ack_array        array for tracking missing chunks
 * tcp_thread       thread for running TCP protocol
 * lock             mutex used for thread synchronization
 */
bool done_sending, all_sent;
char ack_array[1000];
pthread_t tcp_thread;
pthread_mutex_t lock;

/*
 * Checks acknowledgement array for a "gap"
 * Returns true if there is a gap, else false.
 */
bool gapcheck(char arr[])
{
    for (int k=0; k < 1000; k++)
    {
        if(ack_array[k] == '0')
        {
            return true;
        }
    }
    return false;
}

/* TCP thread */
void *tcp_worker()
{
    int server_tcp_socket, client_tcp_socket;
    struct sockaddr_in server_tcp_addr, client_tcp_addr;
    socklen_t sock_len = sizeof(struct sockaddr_in);
    char buffer[20] = "All messages sent.";

    /* Zeroing sockaddr_in structs */
    memset(&server_tcp_addr, 0, sizeof(server_tcp_addr));

    /* Create sockets */
    server_tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    client_tcp_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_tcp_socket < 0 || client_tcp_socket < 0)
    {
        perror("[TCP] Error creating sockets");
        exit(EXIT_FAILURE);
    }
    printf("[TCP] Sockets created successfully\n");

    /* Construct server_tcp_addr struct */
    server_tcp_addr.sin_family = AF_INET;
    server_tcp_addr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, SERVER_IP, &(server_tcp_addr.sin_addr));

    /* TCP Binding */
    if ((bind(server_tcp_socket, (struct sockaddr *)&server_tcp_addr, sizeof(server_tcp_addr))) < 0)
    {
        perror("[TCP] Bind failed. Error");
        exit(EXIT_FAILURE);
    }
    printf("[TCP] Bind completed.\n");

    /* TCP server socket enters listening mode */
    listen(server_tcp_socket, 1);
    printf("[TCP] Server listening on port %d\n", TCP_PORT);
    printf("[TCP] Waiting for incoming connections...\n");

    /* Accepting incoming peers */
    client_tcp_socket = accept(server_tcp_socket, (struct sockaddr *)&client_tcp_addr, (socklen_t*)&sock_len);
    if (client_tcp_socket < 0)
    {
        perror("[TCP] Accept failed.");
        exit(EXIT_FAILURE);
    }
    printf("[TCP] Connection accepted\n");

    while(!all_sent)
    {
        /* Yield the processor until UDP thread finishes sending messages */
        while(!done_sending)
        {
            sched_yield();
        }

        pthread_mutex_lock(&lock);

        /* Inform client TCP thread that all the messages have been sent */
        printf("[TCP] Informing client that all messages have been sent.\n");
        send(client_tcp_socket, buffer, strlen(buffer), 0);

        /* Receive ACK message from client */
        recv(client_tcp_socket, ack_array, sizeof(ack_array), MSG_WAITALL);
        printf("[TCP] Received ACK message from client.\n");

        if(gapcheck(ack_array) == false)
        {
            printf("\n[TCP] ALL MESSAGES HAVE BEEN RECEIVED BY CLIENT\n");
            all_sent = true;
        }

        /* TCP thread yields and UDP thread begins resending missing chunks */
        done_sending = false;
        pthread_mutex_unlock(&lock);
    }

    shutdown(client_tcp_socket, SHUT_RDWR);
    return NULL;
}

/* UDP thread */
int main()
{
    int server_udp_socket, recv_len, bytes_read, counter;
    int total_read = 0, sent_bytes = 0;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sock_len = sizeof(struct sockaddr_in);
    char buffer[1024];
    done_sending = false;
    Message file_contents[1000];
    Message data_message;

    for (int i=0; i < sizeof(ack_array)/sizeof(ack_array[0]); i++)
    {
        ack_array[i] = '0';
    }

    /* Open file in read-only */
    FILE *fp = fopen(FILE_NAME, "r");
	if (fp  == NULL)
	{
		perror("File not found");
		exit(EXIT_FAILURE);
	}
    printf("File opened successfully\n");

    /* Determine file size */
    fseek(fp, 0, SEEK_END);
	long int file_size = ftell(fp);
	printf("%ld is the file size.\n", file_size);

    /* Read file data into local array */
    fseek(fp, 0, SEEK_SET);
    counter = 0;
    int num_left = (int) file_size;
    while(num_left != 0)
    {
        bytes_read = fread(file_contents[counter].data, sizeof(char), CHUNK_SIZE, fp);
        total_read += bytes_read;
        //printf("[UDP] Read %d bytes\n", bytes_read);
        file_contents[counter].chunk_num = counter;
        counter += 1;

        /* Update how many bytes remain in file */
        num_left = (int) file_size - total_read;
    }
    fclose(fp);
    printf("Read %d bytes from file\n", total_read);

    /* Initialize mutex */
    if (pthread_mutex_init(&lock, NULL) != 0) { 
        perror("Mutex init has failed"); 
        exit(EXIT_FAILURE);
    } 

    /* Zeroing sockaddr_in structs */
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    /* Create socket */
    server_udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_udp_socket < 0)
    {
        perror("Error creating sockets");
        exit(EXIT_FAILURE);
    }
    printf("[UDP] Socket created successfully\n");

    /* Construct server_addr struct */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, SERVER_IP, &(server_addr.sin_addr));

    /* UDP Binding */
    if (bind(server_udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[UDP] Bind failed. Error");
        exit(EXIT_FAILURE);
    }
    printf("[UDP] Bind completed.\n");

    /* Start TCP thread */
    pthread_create(&tcp_thread, NULL, tcp_worker, NULL);

    /* Receive incoming message from client */
    printf("[UDP] Waiting for message from client...\n");
    recv_len = recvfrom(server_udp_socket, buffer, 1024, 0, (struct sockaddr*)&client_addr, &sock_len);
    printf("[UDP] Message received from client: %s\n", buffer);
    
    while(!all_sent)
    {
        /* Begin sending messages */
        printf("[UDP] Server beginning transmission...\n");
        pthread_mutex_lock(&lock);

        for (int j=0; j < 1000; j++)
        {
            if(ack_array[j] == '0')
            {
                data_message = file_contents[j];
                sent_bytes = sendto(server_udp_socket, &data_message, sizeof(data_message), 0, (struct sockaddr*)&client_addr, sock_len);
                //printf("[UDP] Sent %d bytes to client\n", sent_bytes);
            }
        }

        /* UDP thread has finished sending messages, now yields */
        printf("[UDP] Server has completed sending messages.\n");
        
        done_sending = true;
        pthread_mutex_unlock(&lock);

        while(done_sending)
        {
            sched_yield();
        }
    }
    pthread_join(tcp_thread, NULL);
    pthread_mutex_destroy(&lock);
    close(server_udp_socket);
    
    return 0;
}