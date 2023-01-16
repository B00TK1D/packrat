#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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

    printf("\nSelect an interface:\n");
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

    printf("\nEnter index of interface selection (0-%d): ", index);
    int selection;
    scanf("%d", &selection);
    if (selection < 0 || selection >= index) {
        printf("Invalid selection\n");
        select_ip();
    }

    strcpy(ip, ips[selection]);
    free(ips);
    return ip;
}



int main(int argc, char **argv){
    char* ip = select_ip();
    printf("ip: %s\n", ip);
    return 0;
}