# UDP File Transfer

A multithreaded socket program for transferring a file over UDP.

## How it works

### Server
1. The server opens the file and reads its contents into an array of type `Message` (user-defined struct).
	* A `Message` contains a 32-bit integer header, `chunk_num` and char array, `data`, for storing file contents. `chunk_num`, represents the data's position in the original file.
2. The server then waits for a message from the client.
3. When it receives this message, it begins transmitting 1,000 messages using UDP that contain the original file contents.
4. Once the server has finished sending messages, the UDP thread yields to the TCP thread which
informs the client that it has finished sending its messages.
5. The server then waits to receive an ACK message from the client's TCP thread that tracks
the chunks that the client missed.
6. Once this ACK message is received, the TCP thread yields and the UDP thread begins resending
the missing chunks.
7. This cycle continues until all 1,000 messages are received from the client.

### Client
1. The client creates an array of 1,000 Messages. The client also sets up an array of 1,000 chars, each initialized to '0'. This array represents an acknowledgement message to keep track of exactly which messages have been received from the server.
2. The client then sends a message to the server stating it is ready to start receiving.
3. The client then enters into a receive loop where it receives the messages being sent by the server. Once a message has been received, the client stores the message into an array of messages, `file_contents`.
4. After receiving 3000 messages, the client breaks out of the loop and yields to the TCP thread.
5. The TCP thread then waits for the "all sent" message from the server. Once received, it sends 
`ack_array` to the TCP server thread which specifies which chunks are missing.
6. The TCP thread then yields to the UDP thread which enters into the receive loop for the next
batch of messages.
7. This cycle continues until all 1,000 messages are received by the client.
8. The client then rebuilds the original file in a simple for loop and prints out the new file size.

## Compiling
The user must first edit the following define statements:
    
* `TCP_PORT`
* `UDP_PORT`
* `SERVER_IP`
* `CLIENT_IP`

The two programs can then be compiled using the following `Makefile` configuration.

```
# Usage:
# make			# compiles source files
# make clean	# removes compiled files

CFLAGS = -std=c99 -g

all:
	@echo "Compiling server and client code..."
	$(CC) $(CFLAGS) server.c -o server -lpthread
	$(CC) $(CFLAGS) client.c -o client -lpthread
	@echo "Done! Copy the compiled files to their respective machines."

clean:
	@echo "Cleaning up..."
	rm -f server
	rm -f client
```

## Usage
The user must start the server program first and then start the client program using the following commands in a terminal:
* `./server`
* `./client`

The two programs will log their outputs in the console window.