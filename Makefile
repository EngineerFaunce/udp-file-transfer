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