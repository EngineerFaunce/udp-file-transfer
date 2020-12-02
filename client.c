/* Client code */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define TCP_PORT        45210
#define UDP_PORT        45211
#define SERVER_IP       "1.1.1.1"
#define CLIENT_IP       "2.2.2.2"
#define CHUNK_SIZE      6401

/*
 * Message 
 * chunk_num    header specifying position in original file
 * data         data payload of file contents
 */
typedef struct Message
{
    int32_t chunk_num;
    char data[CHUNK_SIZE];
} Message;

/*
 * Global variables 
 * 
 * done_recv        determines when client is done receiving a batch of messages
 * all_recv         determines when client has received all messages
 * ack_array        array for tracking missing chunks
 * tcp_thread       thread for running TCP protocol
 * lock             mutex used for thread synchronization
 */
bool done_recv, all_recv;
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
    int client_socket;
    struct sockaddr_in remote_addr;
    char buffer[20] = {0};

    /* Zeroing remote_addr struct */
    memset(&remote_addr, 0, sizeof(remote_addr));

    /* Construct remote_addr struct */
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, SERVER_IP, &(remote_addr.sin_addr));

    /* Create client socket */
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0)
    {
        perror("[TCP] Error creating socket");
        exit(EXIT_FAILURE);
    }
    printf("[TCP] Socket created successfully.\n");

    /* Connect to the server */
    if (connect(client_socket, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("[TCP]Error connecting to server");
        exit(EXIT_FAILURE);
    }
    printf("[TCP] Client connected to server at port %d\n", TCP_PORT);

    while(!all_recv)
    {
        /* Yield while UDP thread is still receiving */
        while(!done_recv)
        {
            sched_yield();
        }
        
        pthread_mutex_lock(&lock);

        /* Wait for "all sent" message from server */
        recv(client_socket, buffer, sizeof(buffer), 0);
        printf("[TCP] Received \"all sent\" message from server: %s\n", buffer);
        
        /* Check status of missing chunks */
        if(gapcheck(ack_array) == false)
        {
            printf("[TCP] ALL MESSAGES RECEIVED\n");
            all_recv = true;
        }

        /* Send ACK message showing status of chunks */
        printf("[TCP] Sending ACK message to server\n");
        send(client_socket, ack_array, sizeof(ack_array), 0);
        done_recv = false;
        pthread_mutex_unlock(&lock);
    }
    shutdown(client_socket, SHUT_RD);
    return NULL;
}

/* UDP thread */
int main()
{
    int client_socket, msg_count = 0, total_msg_count = 0, check = 0;
    double time_taken;
    struct sockaddr_in server_addr, local_addr;
    char buffer[128] = "Oi mate, it's time to send that file";
    socklen_t sock_len = sizeof(struct sockaddr_in);
    Message file_contents[1000];
    Message recv_message;
    done_recv = false, all_recv = false;
    clock_t start, end;

    for (int i=0; i < sizeof(ack_array)/sizeof(ack_array[0]); i++)
    {
        ack_array[i] = '0';
    }

    /* Initialize mutex */
    if (pthread_mutex_init(&lock, NULL) != 0) { 
        perror("Mutex init has failed"); 
        exit(EXIT_FAILURE);
    }

    /* Start TCP thread */
    pthread_create(&tcp_thread, NULL, tcp_worker, NULL);

    /* Zeroing sockaddr_in struct */
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&local_addr, 0, sizeof(server_addr));

    /* Create socket */
    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0)
    {
        perror("[UDP] Error creating socket");
        exit(EXIT_FAILURE);
    }
    printf("[UDP] Socket created succesfully\n");

    /* Set receiving socket into non-blocking mode */
    int flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
    perror("[UDP] Set client socket to non-blocking mode");

    /* Construct local_addr struct */
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, CLIENT_IP, &(local_addr.sin_addr));

    /* Binds IP and port number for client */
    if (bind(client_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("[UDP] Bind failed. Error");
        exit(EXIT_FAILURE);
    }
    printf("[UDP] Bind completed.\n");
    
    /* Construct server_addr struct */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, SERVER_IP, &(server_addr.sin_addr));

    /* Send message to server */
    printf("[UDP] Sending message to server...\n");
    if (sendto(client_socket, buffer, strlen(buffer), 0, (struct sockaddr*)&server_addr, sock_len) < 0)
    {
        perror("sendto error");
        exit(EXIT_FAILURE);
    }  
    printf("[UDP] Message sent\n");

    
    start = clock();
    while(!all_recv)
    {
        /* Receive messages from server. */
        pthread_mutex_lock(&lock);
        while(msg_count < 3000)
        {
            check = recvfrom(client_socket, &recv_message, sizeof(recv_message), 0, (struct sockaddr *)&server_addr, &sock_len);
            if(check == -1 && errno == EAGAIN)
            {
                printf("GETTING A WOULD BLOCK\n");
                break;
            }
            printf("[UDP] Received message from server. Chunk number: %d\n", recv_message.chunk_num);
            msg_count += 1;
            total_msg_count += 1;
            file_contents[total_msg_count] = recv_message;
            ack_array[recv_message.chunk_num] = '1';
        }
        msg_count = 0;
        
        /* UDP thread has stopped receiving messages, now yields */
        printf("[UDP] Server has stopped receiving messages. Yielding to TCP thread.\n");

        done_recv = true;
        pthread_mutex_unlock(&lock);

        while(done_recv)
        {
            sched_yield();
        }
    }
    end = clock();

    /* Report total time taken for transferring file */
    time_taken = ((double) (end-start)) / CLOCKS_PER_SEC;
    printf("[Client] Total time taken for file transfer: %f", time_taken);

    pthread_join(tcp_thread, NULL);
    pthread_mutex_destroy(&lock);

    /* Rebuild the original file */
    FILE *fp;
    fp = fopen("new-bitmap.txt", "w");
    fseek(fp, 0, SEEK_SET);
    for(int j=0; j < 1000; j++)
    {
        fwrite(file_contents[j].data, CHUNK_SIZE, 1, fp);
    }

    /* Determine file size and output it */
    fseek(fp, 0, SEEK_END);
	long int file_size = ftell(fp);
	printf("%ld is the new file size.\n", file_size);
    fclose(fp);

    return 0;
}