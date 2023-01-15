#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>



int *running;

int *counter;

void *loop(void *vargp) {
    printf("[LOOP]: running pointer: %p\n", running);
    while (*running) {
        printf("[LOOP]: Run %d times\n", *counter);
        (*counter)++;
        sleep(1);
    }
    printf("[LOOP]: Loop done\n");
    return NULL;
}


int main() {
    running = malloc(sizeof(int));
    counter = malloc(sizeof(int));
    *running = 1;
    *counter = 0;

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, loop, NULL);

    printf("[MAIN]: running pointer: %p\n", running);

    printf("[MAIN]: Waiting 5 seconds...\n");

    sleep(5);

    printf("[MAIN]: Count at %d\n", *counter);

    printf("[MAIN]: Stopping loop...\n");
    *running = 0;

    pthread_join(thread_id, NULL);

    printf("[MAIN]: Main finished\n");

}