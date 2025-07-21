#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#define KEEP_ALIVE_TIMEOUT 20  // 20 seconds
#define BUFFER_SIZE 1024
#define MAX_PATH 256
#define MAX_RESPONSE 8192

void *handle_client(void *client_socket_ptr);
void handle_http_request(const char* filename, int client_socket, const char* client_host, uint16_t client_port);
const char* get_file_extension(const char* filename);
const char* get_content_type(const char* extension);
int read_file(const char* filename, char** content, unsigned long* size);

static int total_requests = 0;
static size_t total_bytes_sent = 0;
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s PORT\n", argv[0]);
        exit(1);
    }
    
    signal(SIGPIPE, SIG_IGN);
    
    int port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        exit(1);
    }
    
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Enable address reuse
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Setsockopt failed");
        close(server_socket);
        exit(1);
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_socket);
        exit(1);
    }
    
    if (listen(server_socket, 5) == -1) {
        perror("Listen failed");
        close(server_socket);
        exit(1);
    }
    
    printf("Server listening on http://127.0.0.1:%d\n", port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        /* Allocate memory for the client socket descriptor.
           This ensures that each thread gets its own copy,
           avoiding race conditions if accept() is called again
           before the thread reads the value. */
        int *client_socket = malloc(sizeof(*client_socket));
        if (client_socket == NULL) {
            perror("Memory allocation failed");
            close(server_socket);
            exit(1);
        }

        *client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (*client_socket == -1) {
            perror("Accept failed");
            continue;
        }
        
        /* Create a new thread to handle the client connection.
           Pass the client socket pointer to the handler thread. */
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, (void *)client_socket) != 0) {
            perror("pthread_create");
            close(*client_socket);
            free(client_socket);
        }

        /* If we don't plan to pthread_join() the client_thread later,
           we should call pthread_detach() here to avoid leaking resources
           (i.e., the thread's exit status and stack space). */
    }
    
    close(server_socket);
    return 0;
}

void *handle_client(void *client_socket_ptr) {
    int client_socket = *(int *)client_socket_ptr;
    free(client_socket_ptr);

    /* We want the client's address for logging. We *could* pass it as an argument,
       but this introduces complications in a multithreaded context:

       (a) pthread_create() only accepts a single argument, so we'd need to define
           a struct to bundle multiple arguments (e.g. socket + address).

       (b) Passing a pointer to client_addr from the main thread is unsafe - it may
           be overwritten by the next accept() call before the handler thread copies
           it to local storage. We'd need to heap-allocate the address (like we do
           with the socket) to ensure thread safety.

       Instead, we call getpeername() here to retrieve the client's address directly
       from the socket. This avoids shared-state issues and simplifies the threading
       model. */

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

    int keep_alive = 1;
    
    while (keep_alive) {
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        
        timeout.tv_sec = KEEP_ALIVE_TIMEOUT;
        timeout.tv_usec = 0;
        
        int activity = select(client_socket + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity == -1) {
            perror("Select error");
            break;
        }
        
        // Keep-alive timeout
        if (activity == 0) {
            break;
        }
        
        if (FD_ISSET(client_socket, &read_fds)) {
            char buffer[BUFFER_SIZE];
            int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_read == -1) {
                perror("Receive error");
                break;
            }
            
            // Client closed connection
            if (bytes_read == 0) {
                break;
            }
            
            buffer[bytes_read] = '\0';
            
            // Parse request line
            char* line_end = strstr(buffer, "\r\n");
            if (line_end == NULL) {
                line_end = strstr(buffer, "\n");
            }
            
            // Received empty request
            if (line_end == NULL) {
                continue;
            }
            
            *line_end = '\0';
            char request_line[BUFFER_SIZE];
            strncpy(request_line, buffer, BUFFER_SIZE - 1);
            request_line[BUFFER_SIZE - 1] = '\0';
            
            // Received empty request
            if (strlen(request_line) == 0) {
                continue;
            }
            
            // Parse GET request

            // Not a valid GET request
            if (strncmp(request_line, "GET ", 4) != 0) {
                break;
            }
            
            char* path_start = request_line + 4;
            char* path_end = strstr(path_start, " HTTP/1.1");

            // Not a valid HTTP/1.1 request
            if (path_end == NULL) {
                break;
            }

            pthread_mutex_lock(&stats_lock);
            total_requests++;
            printf("%s:%d - request - %s  (total requests: %d)\n", client_host, client_port, request_line, total_requests);
            pthread_mutex_unlock(&stats_lock);
            
            *path_end = '\0';
            char requested_file[MAX_PATH];
            
            if (path_start[0] == '/') {
                path_start++;
            }
            
            if (strlen(path_start) == 0) {
                strcpy(requested_file, "index.html");
            } else {
                strncpy(requested_file, path_start, MAX_PATH - 1);
                requested_file[MAX_PATH - 1] = '\0';
            }
            
            // Handle the request
            handle_http_request(requested_file, client_socket, client_host, client_port);
            
            // Check if client requested connection close
            if (strstr(buffer, "Connection: close") != NULL || 
                strstr(buffer, "connection: close") != NULL) {
                keep_alive = 0;
            }
        }
    }

    close(client_socket);
    printf("%s:%d - connection closed\n", client_host, client_port);
    return NULL;
}

void handle_http_request(const char* filename, int client_socket, const char* client_host, uint16_t client_port) {
    char* content = NULL;
    unsigned long content_length = 0;
    const char* status;
    const char* content_type;
    
    // Check if file exists and read it
    if (read_file(filename, &content, &content_length) == 0) {
        status = "200 OK";
        const char* extension = get_file_extension(filename);
        content_type = get_content_type(extension);
    } else {
        printf("File %s not found.\n", filename);
        status = "404 Not Found";
        content_type = "text/html";
        content = strdup("Page Not Found!");
        content_length = strlen(content);
    }
    
    // Create HTTP response
    char response[MAX_RESPONSE];
    int header_len = snprintf(response, MAX_RESPONSE,
        "HTTP/1.1 %s\r\n"
        "Content-Length: %ld\r\n"
        "Content-Type: %s\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: timeout=%d, max=100\r\n"
        "\r\n",
        status, content_length, content_type, KEEP_ALIVE_TIMEOUT);
    
    // Send header
    if (send(client_socket, response, header_len, 0) == -1) {
        perror("Send header failed");
        free(content);
        return;
    }
    
    // Send content
    if (content_length > 0) {
        if (send(client_socket, content, content_length, 0) == -1) {
            perror("Send content failed");
        }
    }
    
    pthread_mutex_lock(&stats_lock);
    total_bytes_sent += header_len + content_length;
    printf("%s:%d - response - %s  (total bytes sent: %zu)\n", client_host, client_port, status, total_bytes_sent);
    pthread_mutex_unlock(&stats_lock);
    
    if (content) {
        free(content);
    }
}

const char* get_file_extension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) {
        return "";
    }
    return dot + 1;
}

const char* get_content_type(const char* extension) {
    if (strcasecmp(extension, "html") == 0) {
        return "text/html";
    } else if (strcasecmp(extension, "jpeg") == 0 || strcasecmp(extension, "jpg") == 0) {
        return "image/jpeg";
    }
    return "text/html";  
}

int read_file(const char* filename, char** content, unsigned long* size) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        return -1;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate memory for content
    *content = malloc(*size);
    if (*content == NULL) {
        fclose(file);
        return -1;
    }
    
    // Read file content
    size_t bytes_read = fread(*content, 1, *size, file);
    fclose(file);
    
    if (bytes_read != *size) {
        free(*content);
        *content = NULL;
        return -1;
    }
    
    return 0;
}