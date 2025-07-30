#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    char host[100] = "example.com:80";
    char* colon = strchr(host, ':');
    *colon = 0;
    strcat(host, "/index.html");
    printf("%s\n", host);
}