#include <stdio.h>
#include <unistd.h>
#include <string.h>


int main() {
    // Create two pipes - one for stdin and one for stdout
    int stdin_pipe[2];
    int stdout_pipe[2];
    pipe(stdin_pipe);
    pipe(stdout_pipe);

    // Fork the process
    if (fork()) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], 0);
        dup2(stdout_pipe[1], 1);
        dup2(stdout_pipe[1], 2);
        execl("/bin/bash", "/bin/bash", "-i", NULL);
    } else {
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        char buffer[1024];
        if (fork()) {
            while (1) {
                // Receive data
                int recv_len = read(stdout_pipe[0], buffer, 1024);
                buffer[recv_len] = '\0';
                printf("%s", buffer);
                fflush(stdout);
            }
        } else {
            while (1) {
                // Send data
                char* line = NULL;
                size_t len = 0;
                getline(&line, &len, stdin);
                int sent = write(stdin_pipe[1], line, strlen(line));
            }
        }
    }
}