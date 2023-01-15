#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define NTP_HEADER (char[]) {0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0xe6, 0x9b, 0x4a, 0xff, 0x0a, 0x9d, 0x14}

#define NULL_MSG (char[]) {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

int main() {

    // Initialize connection to 127.0.0.1 on udp port 123 and send hello world
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;

    addr.sin_port = htons(123);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int id = 3;
    char *msg = "This is a very long test message to see if it breaks apart during transmission";

    char *packet = malloc(60);

    memcpy(packet, NTP_HEADER, sizeof(NTP_HEADER));
    memcpy(packet + 48, &id, sizeof(unsigned int));

    while (strlen(msg) > 0) {
        memcpy(packet + 52, msg, 8);
        sendto(sock, packet, 60, 0, (struct sockaddr*)&addr, sizeof(addr));
        msg += 8;
    }

    memcpy(packet + 52, NULL_MSG, 8);
    sendto(sock, packet, 60, 0, (struct sockaddr*)&addr, sizeof(addr));


    return 0;
}