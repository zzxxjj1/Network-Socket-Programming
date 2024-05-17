/**
 * client.cpp - a client program that connects to serverM via TCP
 *             and sends usernames to serverM and receives the reply from serverM
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <iostream>
#include <list>
#include <cstring>
#include <sstream>

using namespace std;

/**
 * constants definition
*/
#define LOCAL_HOST "127.0.0.1"
#define SERVER_M_PORT "24984"
#define MAXDATASIZE 1024
#define BACKLOG 10

/**
 * global variables
*/
int sockfd, numbytes;
char buf[MAXDATASIZE];
struct addrinfo hints, *servinfo, *p;
int rv;
char s[INET_ADDRSTRLEN];
unsigned int client_port;
list<string> username_record;

/**
 * function prototypes
*/
void *get_in_addr(struct sockaddr *sa);
void create_socket();
void send_username(const string&  username);
void receive_from_serverM();
bool check_username(const string&  username);
void insert_to_username_record(const string& username_string);
bool match_username_record(const string& username_string);


/**
 * got from Beej's Guide to Network Programming
*/
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/**
 * got from Beej's Guide to Network Programming
*/
// create TCP socket in order to connect serverM
void create_socket(){
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(LOCAL_HOST, SERVER_M_PORT, &hints, &servinfo)) !=0) { 
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, // create socket
                p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) { // connect to serverM
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        exit(2);
    }
    
    // Get the local port of the client's socket
    struct sockaddr_storage local_addr;
    socklen_t local_addr_len = sizeof(local_addr);
    if (getsockname(sockfd, (struct sockaddr *)&local_addr, &local_addr_len) == -1) {
        perror("getsockname");
        exit(1);
    }

    // Store the client's port number in the global variable
    if (local_addr.ss_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&local_addr;
        client_port = ntohs(addr_in->sin_port);
        //printf("client: local port number is %u\n", client_port);
    } else {
        struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&local_addr;
        client_port = ntohs(addr_in6->sin6_port);
        //printf("client: local port number is %u\n", client_port);
    }

    freeaddrinfo(servinfo);
}

/**
 * got from Beej's Guide to Network Programming
*/
// send usernames to serverM
void send_username(const string&  username){
    if (send(sockfd, username.c_str(), username.length(), 0) == -1) // send username to serverM
        perror("client: send");
    cout << "Client finished sending the usernames to Main Server." << endl;
}

/**
 * got from Beej's Guide to Network Programming
*/
void receive_from_serverM(){
    if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
        perror("recv");
        exit(1);
    }
    buf[numbytes] = '\0';
    cout << "Client received the reply from the Main Server using TCP over port ";
    cout << client_port << ": " << endl;
    cout << buf << endl;
}

// check if the username is valid return true if valid, false if not
bool check_username(const string&  username_str){
    istringstream iss(username_str);
    string username;
    int count = 0;
    bool valid = true;

    while(getline(iss, username, ' ')){
        if (count >  9){
            valid = false;
        }
        // check if username is all small letters
        if (username.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != string::npos){
            valid = false;
        }
        count++;
    }
    return valid;
}

// insert the username input into the username record
void insert_to_username_record(const string& username_string) {
    username_record.clear();
    // Convert the input string into a list of usernames
    istringstream iss(username_string);
    string username;
    while (getline(iss, username, ' ')) {
        username_record.push_back(username);
    }
}


//check if the username received from serverM matches the entire username record
bool match_username_record(const string& username_string) {
    // Convert the input string into a list of usernames
    istringstream iss(username_string);
    list<string> input_usernames;
    string username;
    while (getline(iss, username, ' ')) {
        input_usernames.push_back(username);
    }

    // Check if the lengths of the input_usernames and username_record lists are the same
    if (input_usernames.size() != username_record.size()) {
        return false;
    }

    // Sort both lists to make comparison easier
    input_usernames.sort();
    username_record.sort();

    // Compare the sorted input_usernames and username_record lists element-wise
    auto input_iter = input_usernames.begin();
    auto record_iter = username_record.begin();
    while (input_iter != input_usernames.end() && record_iter != username_record.end()) {
        if (*input_iter != *record_iter) {
            return false;
        }
        ++input_iter;
        ++record_iter;
    }

    return true;
}


int main(){
    create_socket();
    cout << "Client is up and running." << endl;
    string usernames;
    while(1){
        cout << "Please enter the usernames to check schedule availability:" << endl;
        getline(cin, usernames);
        if (!check_username(usernames)){
            continue;
        }
        insert_to_username_record(usernames);
        send_username(usernames);
        receive_from_serverM();
        if (match_username_record(buf)){
            cout << "-----Start a new request-----" << endl;
            continue;
        }
        else if (buf[0] != 'T'){ // if the previous reply is the username not found
            receive_from_serverM(); // epecting another reply
        }
        cout << "-----Start a new request-----" << endl;
        
    }
    close(sockfd);
    return 0;
}