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
#define ID "010.211.055.010"

int main() {
    int a = inet_addr(ID);
    printf("a: %d\n", a);
}