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
#include <sys/select.h>
#include "connection.h"
#include "linkedlist.h"
#include "cache/cache.h"
#include "util.h"

int PORT;
int timeout_duration;
int max_object_size;
int max_cache_size;
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

void process_connect_data(int sock, char* host, char* Proxy_auth);
void* handle_client(void* thread_data);

struct client_thread_data {
	cache* cache;
	int client_sock;
};

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
	if(listen(listen_sock, 50) < 0) {
		printf("could not open socket!\n");
		close(listen_sock);
		return 1;
	}

	cache* cache = cache_construct(max_cache_size, max_object_size);

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

		struct client_thread_data* thread_data = malloc(sizeof(struct client_thread_data));
		thread_data->cache = cache;
		thread_data->client_sock =  *client_sock;
		free(client_sock);

		pthread_t client_thread;
		if(pthread_create(&client_thread, NULL, handle_client, (void*)(thread_data)) != 0) {
			perror("pthread_create");
            close(*client_sock);
            free(client_sock);
		}
		pthread_detach(client_thread);
	}

	return 0;
}

void* handle_client(void* thread_data) {
	struct client_thread_data* proxy_client_data = (struct client_thread_data *)thread_data;
	int client_socket = proxy_client_data->client_sock;
	cache* cache = proxy_client_data->cache;

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

	printf(">>>>>>> %s:%d - new connection\n", client_host, client_port);

	/* Set as nonblocking */
	fcntl(client_socket, F_SETFL, O_NONBLOCK);

	int keep_alive = 1;
	while(keep_alive != -1) {
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

		int buffer_len = 8200;
		char buffer[buffer_len];
		int inbuf_used = 0;

		struct linkedlist header_fields = linkedListConstructor();
		int rv = 0;

		if((rv = recv(client_socket, buffer, buffer_len, 0)) < 0) {
			printf("recv error\n");
			return NULL;
		}

		if(rv == 0) {
			printf("--> Client %s:%d disconnected from proxy <--\n\n", client_host, client_port);
			return NULL;
		}
		inbuf_used += rv;

		printf("--> Request from %s:%d\n", client_host, client_port);
		printf("-- Select fd socket\n");

		printf("request - %s\n", buffer);
		char* request_line;

		// get the method and full uri
		char *line_start = buffer;
		char *line_end;
		line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - buffer));
		*line_end = 0;
		*(line_end - 1) = 0;
		printf("Request Line - %s\n", line_start);
		request_line = strdup(line_start);

		char* method = strdup(strtok(line_start, " "));
		printf("method - %s\n", method);
		if(method == NULL) {
			printf("failed to parse method!\n");
			return NULL;
		}
		char* absolute_form = strdup(strtok(NULL, " "));
		printf("absolute form - %s\n", absolute_form);
		line_start = line_end + 1;

		/* Shift buffer down so the unprocessed data is at the start */
		inbuf_used -= (line_start - buffer);
		memmove(buffer, line_start, inbuf_used);
		buffer[inbuf_used] = 0;


		/* Check of cache if GET */
		if(strcasecmp(method, "GET") == 0) {
			printf(">Checking cache\n");
			pthread_mutex_lock(&stats_lock);
			
			res* r = get_cache(cache, absolute_form);
			printf("---\n");
			if(r != NULL) {
				printf("127.0.0.1 %d --> cache hit\n", PORT);
				printf("127.0.0.1 %d r->header: %s\n", PORT, r->header);
				printf("127.0.0.1 %d h_len %d\n", PORT, r->h_len);
				printf("127.0.0.1 %d b->header: %s\n", PORT, r->body);
				printf("127.0.0.1 %d b_len %d\n", PORT, r->b_len);
				send_message(client_socket, r->header, r->h_len);
				send_message(client_socket, r->body, r->b_len);
				printf("127.0.0.1 %d -->> cached response sent", PORT);
				pthread_mutex_unlock(&stats_lock);
				logging(PORT, 'H', r->log, r->status_code, r->bytes);
				break;
			} 
			pthread_mutex_unlock(&stats_lock);
		}
		printf("127.0.0.1 %d >cache did not hit\n", PORT);

		// If CONNECT method is invoked and NOT initiated
		if(strcmp(method, "CONNECT") == 0) {
			int sfd = ConnectTunnel(client_socket, absolute_form);
			if(sfd != -1) {
				logging(PORT, '-', request_line, 200, 0);
				/* if CONNECT IS initiated */
				ConnectMethodServerConnection(client_socket, sfd);
			} else {
				logging(PORT, '-', request_line, 400, 0);
			}
			free(method);
			free(absolute_form);
			close(sfd);
			printf("--> CONNECT Tunnel ended\n\n");
			printf("--> Client %s:%d disconnected from proxy <--\n\n", client_host, client_port);
			break;
		} 

		int status_code;
		int bytes;

		keep_alive = ServerConnection(
			client_socket, 
			method, 
			absolute_form,  
			buffer, 
			buffer_len, 
			&inbuf_used,
			cache,
			&status_code,
			&bytes,
			request_line,
			&stats_lock
		);

		logging(PORT, (strcasecmp(method, "GET") == 0) ? 'M' : '-', request_line, status_code, bytes);

		free(method);
		free(absolute_form);
	}
	close(client_socket);
}

