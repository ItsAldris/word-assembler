#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <ctype.h>
#include <string>

// Zwykly serwer TCP (dummy code)


int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: ./[program] port\n");
        return -1;
    }

    sockaddr_in s_addr;
    socklen_t saddr_size = sizeof(s_addr);
    memset(&s_addr, 0, saddr_size);
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(atoi(argv[1]));
    s_addr.sin_addr.s_addr = INADDR_ANY;


    int sfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    const int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof(one));

    int b = bind(sfd, (sockaddr *) &s_addr, saddr_size);

    int l = listen(sfd, SOMAXCONN);

    sockaddr_in c_addr;
    socklen_t c_addr_size = sizeof(c_addr);
    
    memset(&c_addr, 0, c_addr_size);
    int c = accept(sfd, (sockaddr *) &c_addr, &c_addr_size);

    char buffor[20];
    while(1)
    {
        int message = read(0, buffor, sizeof(buffor));
        if (c != -1)
        {
            int w = write(c, buffor, message);
        }
    }

    close(c);
    shutdown(sfd, SHUT_RDWR);
    close(sfd);
    return 0;    
}