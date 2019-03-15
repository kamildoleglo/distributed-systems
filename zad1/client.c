//
// Created by kamil on 11.03.19.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <zconf.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include "client.h"
#include "list.h"

char* Name;
int Protocol;
address_t* Next_addr;
address_t* Multicast_addr;
int Next_s = -1;
int Local_s = -1;
int Multicast_s = -1;
int Epoll_fd = -1;
node_t* List = NULL;

const char* Multicast_ip = "225.1.1.1";
const uint16_t Multicast_port = (uint16_t) 10000;


void rebind_next(address_t* addr, int protocol);
int connect_socket(address_t* addr, int socket);
int create_socket(int protocol);
address_t* create_address(const char *address, uint16_t port);
void listen_on_socket(int socket, uint16_t port, int protocol);
void handle_message(token_t* token);
int send_token(token_t* token);
int send_empty_token();
void receive_token(int fd);
void send_message(char* message, char* receiver);
void send_info();
void enqueue_free_token();
int epoll_init(int local_socket);
void send_login(address_t* local_address);
void* serve(void* cs);

void send_info(){
    sendto(Multicast_s, Name, strlen(Name), 0, (struct sockaddr *) Multicast_addr, sizeof(struct sockaddr));
}

void rebind_next(address_t* addr, int protocol){
    if(Next_s < 0) {
        shutdown(Next_s, SHUT_RDWR);
        close(Next_s);
    }
    if(Next_addr != NULL)
        free(Next_addr);

    Next_addr = addr;
    Next_s = create_socket(protocol);

    if(protocol == SOCK_STREAM) {
        while(connect_socket(addr, Next_s) != 0) {
            printf("Cannot connect to next, retrying in %d seconds \r\n", CONN_RETRY_TIMEOUT/1000);
            sleep(CONN_RETRY_TIMEOUT/1000);
        }
    }
}



address_t* create_address(const char *address, uint16_t port){
    address_t* addr = malloc(sizeof(address_t));
    if(!addr)
    FAILURE_EXIT("Cannot allocate memory for address\r\n");

    addr->sin_family = AF_INET;

    if (inet_pton(AF_INET, address, &(addr->sin_addr)) != 1)
    FAILURE_EXIT("Cannot convert address \r\n");

    addr->sin_port = htons(port);
    return addr;
}


int connect_socket(address_t* addr, int socket){
    return connect(socket, (const struct sockaddr *) addr, sizeof(struct sockaddr));
}

int create_socket(int protocol) {
    int soc = socket(AF_INET, protocol, 0);
    if (soc == -1)
    FAILURE_EXIT("Cannot create IPv4 socket \r\n");
    return soc;
}

void listen_on_socket(int socket, uint16_t port, int protocol) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket, (struct sockaddr*) &addr, sizeof(addr)) != 0)
    FAILURE_EXIT("Cannot bind socket, exiting \r\n");

    if(protocol == SOCK_STREAM)
        if (listen(socket, BACKLOG) != 0)
        FAILURE_EXIT("Cannot start listening on socket, exiting \r\n");
}

void send_login(address_t* local_address) {
    token_t* token = malloc(sizeof(token_t));
    if(!token)
    FAILURE_EXIT("Cannot allocate memory for address\r\n");

    strcpy(token->sender, Name);
    token->type = LOGIN_REQ;
    token->taken = true;
    token->rebind_data.to = *local_address;
    token->rebind_data.from = *Next_addr;
    send_token(token);
    free(token);
    printf("Sent login request \r\n");
}


int epoll_init(int local_socket) {
    int epoll_fd = epoll_create1(0);

    if (epoll_fd == -1)
    FAILURE_EXIT("Failed to create epoll file descriptor \r\n");

    struct epoll_event e;

    e.events = EPOLLIN | EPOLLET;
    e.data.fd = local_socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, local_socket, &e) == -1)
    FAILURE_EXIT("Failed to add socket to epoll \r\n");
    return epoll_fd;
}

void add_client(int fd){
    printf("Accepting client \r\n");
    struct sockaddr new_addr;
    socklen_t new_addr_len = sizeof(new_addr);
    int file_descriptor = accept(fd, &new_addr, &new_addr_len);

    struct epoll_event e;
    e.events = EPOLLIN | EPOLLET;
    e.data.fd = file_descriptor;
    if (epoll_ctl(Epoll_fd, EPOLL_CTL_ADD, file_descriptor, &e) == -1) {
        printf("Failed to create epoll file descriptor for client \r\n");
        fflush(stdout);
    }
}

void* serve(void* cs){
    int connection_status = *((int *) cs);

    struct epoll_event* event = malloc(sizeof(struct epoll_event) * MAX_EVENTS);
    if(!event)
    FAILURE_EXIT("Cannot allocate memory for epoll \r\n");

    while (1) {
        while(connection_status != 0){
            int event_count = epoll_wait(Epoll_fd, event, MAX_EVENTS, CONN_RETRY_TIMEOUT);
            for (int i = 0; i < event_count; i++) {
                if (event[i].data.fd == Local_s && Protocol == SOCK_STREAM) {
                    add_client(event[i].data.fd);
                } else {
                    receive_token(event[i].data.fd);
                }
            }
            if((connection_status = connect_socket(Next_addr, Next_s)) != 0){
                printf("Cannot connect to next host, retrying in %d seconds\r\n", CONN_RETRY_TIMEOUT/1000);
            }
        }
        if(!isEmpty(List)){
            token_t* token = dequeue(&List);
            if(!token->taken) {
                send_token(token);
                free(token);
            } else {
                enqueue(&List, token);
            }
        }

        while(1) {
            int event_count = epoll_wait(Epoll_fd, event, MAX_EVENTS, -1);
            for (int i = 0; i < event_count; i++) {
                if (event[i].data.fd == Local_s && Protocol == SOCK_STREAM) {
                    add_client(event[i].data.fd);
                } else {
                    receive_token(event[i].data.fd);
                }
            }
        }
    }
    return NULL;
}

void decide_what_to_do(){
    if(isEmpty(List)) {
        if(send_empty_token() != 0)
            enqueue_free_token();
        return;
    }
    token_t* token = dequeue(&List);
    send_token(token);
    free(token);
}

void receive_token(int fd) {
    token_t* token = malloc(sizeof(token_t));
    if(!token) {
        printf("Cannot allocate memory for token! \r\n");
        return;
    }
    struct sockaddr* addr = malloc(sizeof(struct sockaddr));
    socklen_t addr_size = sizeof(struct sockaddr);
    ssize_t bytes = recvfrom(fd, token, sizeof(token_t), 0, addr, &addr_size);
    free(addr);

    if (bytes < 1) {
        printf("Received empty data \r\n");
        fflush(stdout);
        free(token);
        return;
    }
    sleep(1);
    send_info();
    if(token->taken == false){
        decide_what_to_do();
        free(token);
        return;
    }

    if(strcmp(token->sender, Name) == 0){
        printf("Received token sent by self. Dropping \r\n");
        send_empty_token();
        free(token);
        return;
    }

    switch(token->type) {
        case LOGIN_REQ:
            token->type = REBIND;
            enqueue(&List, token);
            printf("Got login request from %s \r\n", token->sender);
            return;
        case MESSAGE:
            if (strcmp(token->receiver, Name) == 0) {
                send_empty_token();
                handle_message(token);
            } else {
                send_token(token);
            }
            break;
        case REBIND:
            if ((Next_addr->sin_addr.s_addr == (token->rebind_data.from.sin_addr.s_addr)) &&
                (Next_addr->sin_port == token->rebind_data.from.sin_port)) {
                address_t* address = malloc(sizeof(address_t));
                if(!address) {
                    printf("Cannot allocate memory for address\r\n");
                    break;
                }
                memcpy(address, &token->rebind_data.to, sizeof(address_t));
                rebind_next(address, Protocol);
                send_empty_token();
            } else {
                send_token(token);
            }
            break;
        case DISASSEMBLE:
            send_token(token);
            printf("Received token disassembly command \r\n");
            exit(EXIT_SUCCESS);
        default:
            printf("Unrecognized message type: %d\r\n", token->type);
    }

    free(token);
}

void handle_message(token_t* token){
    printf("Received message from %s: %s \r\n", token->sender, token->payload);
}

void send_message(char* message, char* receiver) {
    if(strlen(message) + 1 > PAYLOAD_MAX_CONTENT){
        printf("Message too large! \r\n");
        return;
    }
    token_t* token = malloc(sizeof(token_t));
    if(!token){
        printf("Cannot allocate memory for token\r\n");
        return;
    }

    strcpy(token->sender, Name);
    strcpy(token->receiver, receiver);
    token->type = MESSAGE;
    token->taken = true;
    strcpy(token->payload, message);

    send_token(token);

    free(token);
}

int send_empty_token(){
    token_t* token = malloc(sizeof(token_t));
    if(!token) {
        printf("Cannot allocate memory for token\r\n");
        return -1;
    }
    token->taken = false;
    int status = send_token(token);
    free(token);
    return status;
}

int send_token(token_t* token) {
    ssize_t bytes = 0;

    switch(Protocol){
        case SOCK_STREAM:
            bytes = send(Next_s, token, sizeof(token_t), 0);
            break;
        case SOCK_DGRAM:
            bytes = sendto(Next_s, token, sizeof(token_t), 0, (struct sockaddr*) Next_addr, sizeof(address_t));
            break;
        default:
            printf("Unrecognized protocol \r\n");
    }
    if (bytes < 1) {
        printf("Failed to send token \r\n");
        return -1;
    }
    return 0;
}

void cleanup() {
    shutdown(Next_s, SHUT_RDWR);
    shutdown(Local_s, SHUT_RDWR);
    close(Next_s);
    close(Local_s);
    close(Epoll_fd);
    if(rand() <  0.3 * ((double)RAND_MAX + 1.0))
        printf("\r\nI've seen things you people wouldn't believe.\r\n"
               "Attack ships on fire off the shoulder of Orion.\r\n"
               "I watched C-beams glitter in the dark near the Tannhäuser Gate.\r\n"
               "All those moments will be lost in time, like tears in rain. Time to die.\r\n");

}

void sigint_handler(int _) {
    exit(EXIT_SUCCESS);
}

void register_handlers(){
    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&(sa.sa_mask));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    if(atexit(cleanup) != 0)
    FAILURE_EXIT("Cannot register atexit function \r\n");
}

void init_multicast_socket(const char* address, uint16_t port){
    Multicast_s = create_socket(SOCK_DGRAM);
    Multicast_addr = create_address(address, port);
}

void set_global_variables(char* argv[]){
    Name = argv[1];

    char delim[] = ":";
    char* addr = strtok(argv[3], delim);
    uint16_t port = (uint16_t) strtol(strtok(NULL, delim), NULL, 10);

    printf("Next hop address: %s\r\n", addr);
    printf("Next hop port: %d\r\n", port);

    Next_addr = create_address(addr, port);

    Protocol = (int) strtol(argv[5], NULL, 10);
    if(Protocol != 1 && Protocol != 2)
    FAILURE_EXIT("Unrecognized protocol: %d. TCP=1, UDP=2 \r\n", Protocol);

    Next_s = create_socket(Protocol);
    Local_s = create_socket(Protocol);
    init_multicast_socket(Multicast_ip, Multicast_port);
}

void enqueue_free_token(){
    token_t* token = malloc(sizeof(token_t));
    if(!token)
    FAILURE_EXIT("Cannot allocate memory for token\r\n");

    token->taken = false;
    enqueue(&List, token);
}

int init(int argc, char* argv[]){
    if (argc < 5)
    FAILURE_EXIT("Incorrect usage. Usage: ./client <name> <port> <neighbour_ip>[:neighbour_port] <TOKEN=1> <TCP=1, UDP=2> \r\n")

    register_handlers();
    set_global_variables(argv);
    listen_on_socket(Local_s, (uint16_t) strtol(argv[2], NULL, 10), Protocol);

    int connection_status = 0;
    if(Protocol == SOCK_STREAM) {
        if((connection_status = connect_socket(Next_addr, Next_s)) != 0){
            printf("Cannot connect to next host, retrying in %d seconds\r\n", CONN_RETRY_TIMEOUT/1000);
        }
    }
    Epoll_fd = epoll_init(Local_s);
    srand((unsigned int) time(NULL));

    int option = 0;
    char local_ip[INET_ADDRSTRLEN];
    do {
        printf("Startup: \r\n [1] The token ring is not operating yet \r\n [2] The token ring is operational\r\n");
        scanf("%d", &option);
        switch(option) {
            case 1:
                break;
            case 2:
                printf("Pass the interface's address to rebind the ring to:\r\n");
                scanf("%s", local_ip);
                address_t* addr = create_address(local_ip, (uint16_t) strtol(argv[2], NULL, 10));
                send_login(addr);
                free(addr);
                break;
            default:
                printf("Unrecognized option! \r\n");
                fflush(stdin);
                break;
        }
    } while(option != 1 && option != 2);
    return connection_status;
}

void handle_commands(){
    while(1){
        int option = 0;
        do {
            printf("Message type: \r\n [1] Text \r\n [2] Disassemble the ring\r\n");
            scanf("%d", &option);
            switch(option) {
                case 1: {
                    token_t *token = malloc(sizeof(token_t));
                    if (!token) {
                        printf("Cannot allocate memory for token! \r\n");
                        break;
                    }
                    printf("Receipent name: \r\n");
                    scanf("%s", token->receiver);
                    printf("Message content: \r\n");
                    scanf("%s", token->payload);
                    strcpy(token->sender, Name);
                    token->taken = true;
                    token->type = MESSAGE;
                    enqueue(&List, token);

                    break;
                }
                case 2: {
                    token_t *token = malloc(sizeof(token_t));
                    if (!token) {
                        printf("Cannot allocate memory for token! \r\n");
                        break;
                    }
                    token->type = DISASSEMBLE;
                    token->taken = true;
                    enqueue(&List, token);
                    return;
                }
                default:
                    printf("Unrecognized option! \r\n");
                    fflush(stdin);
                    break;
            }
        } while(option != 1 && option != 2);

    }
}

int main(int argc, char* argv[]) {
    int* connection_status = malloc(sizeof(int));
    if(!connection_status)
        FAILURE_EXIT("Cannot allocate memory for an int \r\n");
    *connection_status = init(argc, argv);

    if(strtol(argv[4], NULL, 10) == 1){
        int status = send_empty_token();
        if(status != 0)
            enqueue_free_token();
    }
    pthread_t network;
    pthread_create(&network, NULL, serve, (void*) connection_status);
    handle_commands();
    pthread_join(network, NULL);
    //serve(connection_status);


    return 0;
}