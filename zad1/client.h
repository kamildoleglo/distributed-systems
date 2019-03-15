//
// Created by kamil on 11.03.19.
//

#ifndef ZAD1_CLIENT_H
#define ZAD1_CLIENT_H
#define FAILURE_EXIT(format, ...) { printf(format, ##__VA_ARGS__); exit(EXIT_FAILURE);}
#include <arpa/inet.h>


#define CLIENT_MAX_NAME 20
#define PAYLOAD_MAX_CONTENT 1024
#define BACKLOG 12
#define MAX_EVENTS 5
#define CONN_RETRY_TIMEOUT 10000

typedef struct sockaddr_in address_t;

enum message_type{
    MESSAGE = 0, LOGIN_REQ = 1, REBIND = 2, DISASSEMBLE = 3
};

typedef struct {
    address_t from;
    address_t to;
} rebind_data_t;

typedef struct {
    char sender[CLIENT_MAX_NAME];
    char receiver[CLIENT_MAX_NAME];
    int taken;
    int type;
    char payload[PAYLOAD_MAX_CONTENT];
    rebind_data_t rebind_data;
} token_t;

#endif //ZAD1_CLIENT_H
