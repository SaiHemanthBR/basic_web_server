#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "config.h"
#include "mimetypes.h"
#include "request.h"
#include "response.h"

#define BACKLOG 16
#define SERVER_NAME "ElServe/2.0"
#define FILE_PATH_BUF_SIZE 1024

void stop_server();
void setup_socket();
void* handle_request(void*);
void clean_request(FILE*, request*, response*);

int tcp_socket = -1;

int main(int argc, char *argv[]) {
    int conn_fd = -1;
    pthread_t tid;

    // Managing Process Lifecycle
    atexit(stop_server);
    signal(SIGINT, exit);
    signal(SIGTSTP, exit);

    // Setup
    load_config();
    create_mime_table();
    setup_socket();

    printf("Server Started...\nListening on http://%s:%d\nPress Ctrl+C to exit.\n\n", 
            get_config_str(HOST_CONF_KEY), get_config_int(PORT_CONF_KEY));
    
    while(true) {
        if((conn_fd = accept(tcp_socket, NULL, NULL)) < 0) {
            perror("Unable to accept new connection");
            continue;
        }

        int *new_conn_fd = (int*) malloc(sizeof(int));
        *new_conn_fd = conn_fd;
        if(pthread_create(&tid, NULL, handle_request, (void *) new_conn_fd) < 0) {
            perror("Unable to create new thread");
            close(conn_fd);
            free(new_conn_fd);
            continue;
        }

        if(pthread_detach(tid) != 0) {
            perror("Unable to detach new thread");
            continue;
        }
    }
    
    return 0;
}

void stop_server() {
    printf("\nShutting down server.....\n");
    if(tcp_socket != -1) close(tcp_socket);
    tcp_socket = -1;
    destroy_mime_table();
    unload_config();
}

void setup_socket() {
    if((tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Unable to get IPv4 TCP Socket using socket()");
        exit(-1);
    }
    
    int on = 1;
    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(get_config_int(PORT_CONF_KEY));
    inet_pton(AF_INET, get_config_str(HOST_CONF_KEY), &(server_addr.sin_addr));
    socklen_t server_addr_len = sizeof(server_addr);

    if(bind(tcp_socket, (struct sockaddr *) &server_addr, server_addr_len) < 0) {
        perror("Unable to bind socket to server address");
        exit(-1);
    }

    if(listen(tcp_socket, BACKLOG) < 0) {
        perror("Unable to listen to socket");
        exit(-1);
    }
}

void* handle_request(void* new_conn_fd) {
    char file_path[FILE_PATH_BUF_SIZE];
    int conn_fd = *((int *) new_conn_fd);
    free(new_conn_fd);

    FILE *file = NULL;
    request *req = NULL;
    response *res = NULL;

    req = get_request(conn_fd);
    if(strcmp(req->url, "/") == 0) {
        free(req->url);
        req->url = get_config_str(PAGE_CONF_KEY);
    }
    printf("> (%s) (%s) (%s)\n", req->http_method, req->url, req->http_ver);
    sprintf(file_path, "%s%s", get_config_str(SITE_DIR_CONF_KEY), req->url);

    file = NULL;
    if((file = fopen(file_path, "r")) == NULL) {
        clean_request(file, req, res);
        return 0;
    }

    res = create_response_from_request(req);
    res->status_code = strdup("200 OK");
    set_response_header(res, "content-type", get_mimetype_for_url(req->url, NULL));
    set_response_header(res, "server", SERVER_NAME);

    if(send_response_header(res) == 0) {
        clean_request(file, req, res);
        return 0;
    };

    if(send_response_file(res, file) == 0) {
        printf("Error Sending File: %s for URL: %s. %s\n", file_path, req->url, strerror(errno));
        clean_request(file, req, res);
        return 0;
    }

    clean_request(file, req, res);
    return 0;
}

void clean_request(FILE *file, request *req, response *res) {
    if(file != NULL) { fclose(file); file = NULL; }
    if(req != NULL) { close_request(req); req = NULL; }
    if(res != NULL) { close_response(res); res = NULL; }
}
