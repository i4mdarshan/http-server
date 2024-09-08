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
#include <fnctl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>

typedef struct cache_element cache_element;

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

int HOST_PORT = 8080;
int PROXY_SOCKET_ID =
