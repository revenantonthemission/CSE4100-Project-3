/*
 * echoclient.c - An echo client
 */
/* $begin echoclientmain */
#include "csapp.h"

int main(int argc, char **argv)
{
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    // When we execute socketclient, we need to insert two more arguments --- host and port.
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    host = argv[1];
    port = argv[2];

    // Connect the host and the client via port
    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    // Communicate with the server until EOF
    while (Fgets(buf, MAXLINE, stdin) != NULL)
    {
        Rio_writen(clientfd, buf, strlen(buf));
        Rio_readlineb(&rio, buf, MAXLINE);
        Fputs(buf, stdout);
    }

    // EOF means that the client lost the connection. Close the client.
    Close(clientfd); // line:netp:echoclient:close
    exit(0);
}
/* $end echoclientmain */
