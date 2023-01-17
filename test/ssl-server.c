// Simple ssl socket server

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

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
    servaddr.sin_addr.s_addr=inet_addr("127.0.0.1");
    servaddr.sin_port=htons(443);

    SSL_load_error_strings();
    SSL_library_init();
    SSL_CTX *ssl_ctx = SSL_CTX_new (SSLv23_client_method ());

    // Bind the socket to the port
    bind(sock_fd, (struct sockaddr *) &servaddr, sizeof(servaddr));

    // create an SSL connection and attach it to the socket
    SSL *conn = SSL_new(ssl_ctx);
    SSL_set_fd(conn, sock_fd);

    SSL_read(conn, buffer, 48);
    printf("Received: %s\n", buffer);
    strcpy(buffer, "Hello from server!");
    SSL_write(conn, buffer, strlen(buffer));

    SSL_shutdown(conn);
    SSL_free(conn);
    SSL_CTX_free(ssl_ctx);

    close(sock_fd);
    return 0;
}