#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char buf[512];

void cat(int fd){
    int n;
    while((n = read(fd, buf, sizeof(buf))) > 0){
        if (write(1, buf, n) != n){
            printf("cat: write error\n");
            exit(1);
        }
    }
    if(n < 0){
        printf("cat: read error\n");
        exit(1);
    }
}

int main(int argc, char *argv[]){
    // TODO
    int fd, i;
    if(argc <= 1){
    cat(0);
    exit(1);
    }
    for(i = 1; i < argc; i++){
    if((fd = open(argv[i], 0)) < 0){
        printf("cat: cannot open %s\n", argv[i]);
        exit(1);
    }
    cat(fd);
    close(fd);
    }
    exit(0);
}

