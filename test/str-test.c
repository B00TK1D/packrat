#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main() {
    char listen_ip[] = "127.0.0.1";

    char* str_c2_ip = malloc(16);
    // Copy listen_ip to str_c2_ip but change format from 127.0.0.1 to 127.000.000.001

    // use strtok to split listen_ip into 4 parts
    char* part = strtok(listen_ip, ".");
    printf("%s\n", part);
    int segment = atoi(part);
    sprintf(str_c2_ip, "%03d", segment);
    part = strtok(NULL, ".");
    segment = atoi(part);
    sprintf(str_c2_ip, "%s.%03d", str_c2_ip, segment);
    part = strtok(NULL, ".");
    segment = atoi(part);
    sprintf(str_c2_ip, "%s.%03d", str_c2_ip, segment);
    part = strtok(NULL, ".");
    segment = atoi(part);
    sprintf(str_c2_ip, "%s.%03d", str_c2_ip, segment);

    printf("str_c2_ip: %s\n", str_c2_ip);
}