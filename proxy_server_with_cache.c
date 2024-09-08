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
#define MAX_CLIENTS 10

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
sem_t semaphore; // Basically the number of threads which can read/write simultaneously
pthread_mutex_t lock;

cache_element* head;
int cache_size;

void log_to_cli(const char *message) {
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


int main(int argc, char* argv[]){
    int client_socketId, client_len;

    // Use sockect address struct to create an socket object
    // for client and server for handshake
    struct sockaddr_in server_addr, client_addr;

    // Initialize semaphore with MIN and MAX values
    sem_init(&semaphore,0,MAX_CLIENTS); 
    pthread_mutex_init(&lock, NULL); 

    // Check if the server start command is having a port to be used
    if(argv == 2){
        // Change the port number to user entered port number
        port_number = atoi(argv[1]);
    }else{
        log_to_cli("Too few arguments");
        exit(1);
    }

    // Start the proxy server
    log_to_cli("Starting proxy server");

    // Setup the server proxy with IPv4 -> AF_INET, TCP protocol-> SOCK_STREAM and 0 -> 
    proxy_socketId =  socket(AF_INET, SOCK_STREAM,0);

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
    int setSocketOpt_result = setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR,(const char*) reuse, sizeof(reuse));

    if(setSocketOpt_result < 0){
        log_to_cli("Failed to configure socket options");
        perror("Reason:");
        exit(1);
    }

    // Since the server_addr and client_addr structs gets created with a garbage value initiate it to zeros
    bzero((char*)&server_addr,sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number); // Convert port_number to Network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY; // Assign the server address randomly
    

    return 0;
}