#include <stdio.h>

int main() {
    int a = 0;
    printf("Hello from client\n");
    printf("Enter a number: ");
    fflush(stdout);
    scanf("%d", &a);
    printf("You entered %d\n", a);
    fflush(stdout);
}