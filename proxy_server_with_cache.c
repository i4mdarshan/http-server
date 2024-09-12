/**
 *  This is the main server file.
 */

#include "proxy_parse.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>

typedef struct cache_element cache_element;
#define MAX_BYTES 4096                  // max allowed size of request/response
#define MAX_CLIENTS 400                 // max number of client requests served at a time
#define MAX_SIZE 200 * (1 << 20)        // size of the cache
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache

struct cache_element
{
    cache_element *next;
    char *data;
    char *url;
    int len;
    time_t lru_time_track;
};

cache_element *find(char *url);

int add_cache_element(char *data, int size, char *url);

void remove_cache_element();

int port_number = 8080;
int proxy_socketId;
pthread_t tid[MAX_CLIENTS]; // used to maintain the thread Ids
sem_t semaphore;            // Basically the number of threads which can read/write simultaneously
pthread_mutex_t lock;

cache_element *head;
int cache_size;
/**
 * Function to log formatted messages to cli
 */
void log_to_cli(const char *message)
{
    // Get the current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    // Buffer to hold the formatted date and time
    char time_buffer[26];

    // Format the date and time
    strftime(time_buffer, sizeof(time_buffer), "[%Y-%m-%d %H:%M:%S]", tm_info);

    // Print the log message with the date and time in square brackets
    printf("[%s] %s\n", time_buffer, message);
}

/**
 * Function to send error response with repective HTTP codes
 */

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        log_to_cli("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        log_to_cli("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        log_to_cli("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        // printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        log_to_cli("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        log_to_cli("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}

/**
 * Function to make a remote connection to server
 */
int connectRemoteServer(char *host_addr, int port_num)
{
    // Creating Socket for remote server ---------------------------

    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (remoteSocket < 0)
    {
        log_to_cli("Error in Creating Socket.\n");
        return -1;
    }

    // Get host by the name or ip address provided

    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL)
    {
        fprintf(stderr, "No such host exists.\n");
        return -1;
    }

    // inserts ip address and port number of host in struct `server_addr`
    struct sockaddr_in server_addr;

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to Remote server ----------------------------------------------------

    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting !\n");
        return -1;
    }
    // free(host_addr);
    return remoteSocket;
}

/**
 * Function to check the HTTP version of the remote server
 */
int checkHTTPversion(char *msg)
{
    int version = -1;

    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1; // Handling this similar to version 1.1
    }
    else
        version = -1;

    return version;
}

/**
 * Thread function to initiate and manage new thread for each request
 *
 */
void *thread_fn(void *socketNew)
{
    sem_wait(&seamaphore);
    int p;
    sem_getvalue(&seamaphore, &p);
    printf("semaphore value:%d\n", p);
    int *t = (int *)(socketNew);
    int socket = *t;            // Socket is socket descriptor of the connected Client
    int bytes_send_client, len; // Bytes Transferred

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char)); // Creating buffer of 4kb for a client

    bzero(buffer, MAX_BYTES);                               // Making buffer zero
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0); // Receiving the Request of client by proxy server

    while (bytes_send_client > 0)
    {
        len = strlen(buffer);
        // loop until u find "\r\n\r\n" in the buffer
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }

    // printf("--------------------------------------------\n");
    // printf("%s\n",buffer);
    // printf("----------------------%d----------------------\n",strlen(buffer));

    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    // tempReq, buffer both store the http request sent by client
    for (int i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }

    // checking for the request in cache
    struct cache_element *temp = NULL;

    if (temp != NULL)
    {
        // request found in cache, so sending the response to client from proxy's cache
        int size = temp->len / sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < size)
        {
            bzero(response, MAX_BYTES);
            for (int i = 0; i < MAX_BYTES; i++)
            {
                response[i] = temp->data[pos];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        log_to_cli("Data retrived from the Cache\n\n");
        printf("%s\n\n", response);
        // close(socketNew);
        // sem_post(&seamaphore);
        // return NULL;
    }

    else if (bytes_send_client > 0)
    {
        len = strlen(buffer);
        // Parsing the request
        ParsedRequest *request = ParsedRequest_create();

        // ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in
        //  the request
        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            log_to_cli("Parsing failed\n");
        }
        else
        {
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET"))
            {

                if (request->host && request->path && (checkHTTPversion(request->version) == 1))
                {
                    bytes_send_client = handle_request(socket, request, tempReq); // Handle GET request
                    if (bytes_send_client == -1)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }
                else
                    sendErrorMessage(socket, 500); // 500 Internal Error
            }
            else
            {
                log_to_cli("This code doesn't support any method other than GET\n");
            }
        }
        // freeing up the request pointer
        ParsedRequest_destroy(request);
    }

    else if (bytes_send_client < 0)
    {
        log_to_cli("Error in receiving from client.\n");
        perror("Reason: \n");
    }
    else if (bytes_send_client == 0)
    {
        log_to_cli("Client disconnected!\n");
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&seamaphore);

    sem_getvalue(&seamaphore, &p);
    printf("Semaphore post value:%d\n", p);
    free(tempReq);
    return NULL;
}

int main(int argc, char *argv[])
{
    int client_socketId, client_len;

    // Use sockect address struct to create an socket object
    // for client and server for handshake
    struct sockaddr_in server_addr, client_addr;

    // Initialize semaphore with MIN and MAX values
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    // Check if the server start command is having a port to be used
    if (argv == 2)
    {
        // Change the port number to user entered port number
        port_number = atoi(argv[1]);
    }
    else
    {
        log_to_cli("Too few arguments");
        exit(1);
    }

    // Start the proxy server
    log_to_cli("Starting proxy server");

    // Setup the server proxy with IPv4 -> AF_INET, TCP protocol-> SOCK_STREAM and 0 ->
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);

    // Check if the socket is created succesfully
    if (proxy_socketId < 0)
    {
        log_to_cli("Failed to create a socket");
        perror("Reason:");
        exit(1);
    }

    log_to_cli("Socket created successfully");

    int reuse = 1;
    // Configure socket options
    log_to_cli("Configuring socket options");
    int setSocketOpt_result = setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)reuse, sizeof(reuse));

    if (setSocketOpt_result < 0)
    {
        log_to_cli("Failed to configure socket options");
        perror("Reason:");
        exit(1);
    }

    // Since the server_addr and client_addr structs gets created with a garbage value initiate it to zeros
    bzero((char *)&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number); // Convert port_number to Network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Assign the server address randomly

    log_to_cli("Binding socket to port: " + port_number);
    // Bind the socket port
    int bindSocket_result = bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (bindSocket_result < 0)
    {
        log_to_cli("Port is not available, failed to bind the socket");
        perror("Reason: ");
        exit(1);
    }

    log_to_cli("Socket binded successfully");

    // Listen to the assigned port
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if (listen_status < 0)
    {
        log_to_cli("Failed to liste to port " + port_number);
        perror("Reason: ");
        exit(1);
    }
    log_to_cli("Listening to port " + port_number);

    // Iterate through the clients which are connected to server
    // Infinite Loop for accepting connections
    while (1)
    {

        bzero((char *)&client_addr, sizeof(client_addr)); // Clears struct client_addr
        client_len = sizeof(client_addr);

        // Accepting the connections
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len); // Accepts connection
        if (client_socketId < 0)
        {
            fprintf(stderr, "Error in Accepting connection !\n");
            exit(1);
        }
        else
        {
            Connected_socketId[i] = client_socketId; // Storing accepted client into array
        }

        // Getting IP address and port number of client
        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN]; // INET_ADDRSTRLEN: Default ip address size
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client is connected with port number: %d and ip address: %s \n", ntohs(client_addr.sin_port), str);
        // printf("Socket values of index %d in main function is %d\n",i, client_socketId);
        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]); // Creating a thread for each client accepted
        i++;
    }
    close(proxy_socketId); // Close socket
    return 0;
}