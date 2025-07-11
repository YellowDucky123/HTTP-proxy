#include <stdio.h>
#include <stdio.h>
#include <string.h>	//strlen
#include <stdlib.h>	//strlen
#include <sys/socket.h>
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>
#include "connection.h"

void process_connect_data(int sock, char* host, char* Proxy_auth);


int main(int argc, char** argv) {
	if (argc != 5) {
		printf("correct usage:\n");
		printf("./proxy <port> <timeout> <max_object_size> <max_cache_size>\n");
		return 1;
	}

	// input arguments
	int PORT = atoi(argv[1]);
	int timeout = atoi(argv[2]);
	int max_object_size = atoi(argv[3]);
	int max_cache_size = atoi(argv[4]);


	struct sockaddr_in server = {0};
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(PORT);

	int listen_sock;
	if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("could not create socket!\n");
		return 1;
	}

	//bind socket to port
	if (bind(listen_sock,(struct sockaddr *)&server , sizeof(server)) < 0) {
		puts("bind failed");
		return 1;
	}

	//Listen on socket
	if(listen(listen_sock, 10) < 0) {
		printf("could not open socket!\n");
		return 1;
	}

	
	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	while(true) {
		struct sockaddr_in client_address;
		socklen_t client_address_len = sizeof(client_address);

		int sock;
		if((sock = accept(listen_sock, (struct sockaddr *) &client_address, &client_address_len)) < 0) {
			printf("could not open socket to accept data!\n");
			return 1;
		}

		int buffer_len = 1024;
		char buffer[buffer_len];
		size_t inbuf_used = 0;

		int rv = 0;

		// get the method and full uri
		if((rv = recv(sock, buffer, buffer_len, 0)) <= 0) {
			printf("recv error\n");
			return 1;
		}
		inbuf_used += rv;

		char *line_start = buffer;
		char *line_end;
		line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - buffer));
		*line_end = 0;
		char* method = strtok(line_start, " ");
		if(method == NULL) {
			printf("failed to parse method!\n");
			return 1;
		}
		char* full_uri = strtok(NULL, " ");
		line_start += line_end + 1;

		/* Shift buffer down so the unprocessed data is at the start */
		inbuf_used -= (line_start - buffer);
		memmove(buffer, line_start, inbuf_used);

		// process data based on method
		if(strcmp(method, "CONNECT") == 0) {

		} else {
			ServerConnection(sock, line_start, line_end, buffer, inbuf_used);
		}

			
			/* Scan for newlines in the line buffer; we're careful here to deal with embedded \0s
			* an evil server may send, as well as only processing lines that are complete.
			*/
			// while ( (line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - inbuf))))
			// {
			// 	*line_end = 0;
			// 	process_line(line_start);
			// 	line_start = line_end + 1;
			// }
	}
	

	return 0;
}

void process_connect_data(int sock, char* host, char* Proxy_auth) {

}

