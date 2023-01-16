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

int main() {
    char* path = malloc(256 + 8);
    strcpy(path, "../beacons/");
    strcat(path, "bash");
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        printf("Beacon could not be loaded\n");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* beacon = malloc(fsize + 1);
    fread(beacon, fsize, 1, f);
    fclose(f);
    free(path);
    beacon[fsize] = 0;

    printf("Loaded beacon: %s\n", beacon);
}