#include <stdio.h>
#include <stdio.h>
#include <string.h>	//strlen
#include <stdlib.h>	
#include <sys/socket.h>
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>
#include <fcntl.h>	// unblocking and select
#include <pthread.h>	// multi thread
#include <netinet/in.h>	
#include "connection.h"
#include "linkedlist.h"

int PORT;
int timeout_duration;
int max_object_size;
int max_cache_size;

void process_connect_data(int sock, char* host, char* Proxy_auth);
void* handle_client(void* sock);


int main(int argc, char** argv) {
	if (argc != 5) {
		printf("correct usage:\n");
		printf("./proxy <port> <timeout> <max_object_size> <max_cache_size>\n");
		return 1;
	}

	// input arguments
	PORT = atoi(argv[1]);
	timeout_duration = atoi(argv[2]);
	max_object_size = atoi(argv[3]);
	max_cache_size = atoi(argv[4]);


	struct sockaddr_in server = {0};
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(PORT);

	int listen_sock;
	if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("could not create socket!\n");
		return 1;
	}

	// Enable address reuse
    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Setsockopt failed");
        close(listen_sock);
        exit(1);
    }

	//bind socket to port
	if (bind(listen_sock,(struct sockaddr *)&server , sizeof(server)) < 0) {
		puts("bind failed");
		close(listen_sock);
		exit(1);
	}

	//Listen on socket
	if(listen(listen_sock, 10) < 0) {
		printf("could not open socket!\n");
		close(listen_sock);
		return 1;
	}

	
	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	while(1) {
		struct sockaddr_in client_address;
		socklen_t client_address_len = sizeof(client_address);

		int* client_sock = malloc(sizeof(client_sock));
		if (client_sock == NULL) {
            perror("Memory allocation failed");
            close(listen_sock);
            exit(1);
        }

		*client_sock = accept(listen_sock, (struct sockaddr*)&client_address, &client_address_len);
        if (*client_sock == -1) {
            perror("Accept failed");
            continue;
        }

		pthread_t client_thread;
		if(pthread_create(&client_thread, NULL, handle_client, (void*)client_sock) != 0) {
			perror("pthread_create");
            close(*client_sock);
            free(client_sock);
		}
		pthread_detach(client_thread);
	}

	return 0;
}

void* handle_client(void* sock) {
	int client_socket = *(int *) sock;
	free(sock);

	struct sockaddr_in client_address;
	socklen_t client_address_len = sizeof(client_address);
	
	/* Get the client address. */
	if (getpeername(client_socket, (struct sockaddr *)&client_address, &client_address_len) == -1) {
		perror("getpeername failed");
		close(client_socket);
		return NULL;
	}

	/* Convert the client address to a format for logging. */
	char client_host[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &client_address.sin_addr, client_host, sizeof(client_host));
	uint16_t client_port = ntohs(client_address.sin_port);

	printf("%s:%d - new connection\n", client_host, client_port);

	/* Set as nonblocking */
	fcntl(client_socket, F_SETFL, O_NONBLOCK);

	int keep_alive = 1;
	
	while(keep_alive) {
		fd_set read_fds;
		struct timeval timeout;

		FD_ZERO(&read_fds);	// Clears the fd set
		FD_SET(client_socket, &read_fds); // Puts client_socket into the set

		timeout.tv_sec = timeout_duration;
		timeout.tv_usec = 0;

		int activity = select(client_socket + 1, &read_fds, NULL, NULL, &timeout);

		if(activity == -1) {
			perror("Select Error");
			break;
		}

		if(activity == 0) {
			printf("no fds ready\n");
			break;
		}

		if(!FD_ISSET(client_socket, &read_fds)) {
			continue;
		}

		printf("Select fd socket\n");

		int buffer_len = 8200;
		char buffer[buffer_len];
		int inbuf_used = 0;

		struct linkedlist header_fields = linkedListConstructor();
		int rv = 0;

		if((rv = recv(client_socket, buffer, buffer_len, 0)) <= 0) {
			printf("recv error\n");
			return NULL;
		}
		inbuf_used += rv;

		// get the method and full uri
		char *line_start = buffer;
		char *line_end;
		line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - buffer));
		*line_end = 0;
		char* method = strtok(line_start, " ");
		if(method == NULL) {
			printf("failed to parse method!\n");
			return NULL;
		}
		char* absolute_form = strtok(NULL, " ");
		line_start = line_end + 1;

		/* Shift buffer down so the unprocessed data is at the start */
		inbuf_used -= (line_start - buffer);
		memmove(buffer, line_start, inbuf_used);

		// process data based on method
		if(strcmp(method, "CONNECT") == 0) {
			ConnectMethodServerConnection(client_socket, method, absolute_form);
			break;
		} else {
			keep_alive = ServerConnection(
				client_socket, 
				method, 
				absolute_form, 
				line_start, 
				line_end, 
				buffer, 
				buffer_len, 
				&inbuf_used
			);
			if(keep_alive == -1) break;
		}
	}
	close(client_socket);
}

