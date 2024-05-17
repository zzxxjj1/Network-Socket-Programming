/**
 * serverM.cpp -- A main server program that will listen to the client via TCP and
 *               send the message to the serverA and serverB via UDP.
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
#include <algorithm>
#include <iostream>
#include <list>
#include <cstring>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>
#include <fcntl.h>

using namespace std;
/**
 * constants definition
*/
#define LOCAL_HOST "127.0.0.1" // Local host address
#define BACKEND_UDP_PORT "23984"    // UDP port number at serverA and serverB end
#define CLIENT_TCP_PORT "24984"    // TCP port number at client end
#define SERVER_A_UDP_PORT "21984"    // UDP port number at serverA end
#define SERVER_B_UDP_PORT "22984"    // UDP port number at serverB end
#define MAXBUFLEN 1024 // Max number of bytes we can get at once
#define BACKLOG 10 // How many pending connections queue will hold

/**
 * global variables
*/

list<string> serverA_username_list; // serverA username list, format: username1 username2 username3 …
list<string> serverB_username_list; // serverA username list, format: username1 username2 username3 …
list<string> client_username_list; // client input username list (up to 10 usernames), format: username1 username2 username3 …
list<string> username_to_serverA; // a sub-list of client_username_list that will be sent to serverA, format: username1 username2 username3 …
list<string> username_to_serverB; // a sub-list of client_username_list that will be sent to serverB, format: username1 username2 username3 …
list<string> username_not_exist; // a sub-list of client_username_list that does not exist in serverA_username_list and serverB_username_list, format: username1 username2 username3 …
list<string> serverA_time_interval_list; // serverA time interval list, format: [[t1_start, t1_end], [t2_start, t2_end], … ].
list<string> serverB_time_interval_list; // serverB time interval list, format: [[t1_start, t1_end], [t2_start, t2_end], … ].
list<string> result_username_list; // result username list
list<string> result_time_intervals; // result time intervals list
bool received_serverA_username_list = false; // flag to indicate whether serverA username list is received
bool received_serverB_username_list = false; // flag to indicate whether serverB username list is received
bool received_serverA_time_interval_list = false; // flag to indicate whether serverA time interval list is received
bool received_serverB_time_interval_list = false; // flag to indicate whether serverB time interval list is received

/**
 * socket variables
*/
int sockfd_TCP, sockfd_UDP, new_fd; // listen on sock_fd, new connection on new_fd
struct addrinfo hints, *servinfo, *p;
struct sockaddr_storage their_addr; // connector's address information 
socklen_t sin_size, addr_len;
struct sigaction sa;
int yes=1;
char s[INET_ADDRSTRLEN];
char buf[MAXBUFLEN];
int rv;
int numbytes;

/**
 * function prototypes
*/

void create_TCP_socket(); // create TCP socket w/ port number CLIENT_TCP_PORT & bind
void create_UDP_socket(); // create UDP socket w/ port number BACKEND_UDP_PORT & bind
void listen_TCP_socket(); // listen to TCP socket
void accept_TCP_connection(); // accept TCP connection
void receive_client_username_list(); // receive client username list
void accept_UDP_connection(); // accept UDP connection
void reply_to_client(); // reply to client with the result
void sigchld_handler(int s); // reap all dead processes
void *get_in_addr(struct sockaddr *sa); // get sockaddr, IPv4 or IPv6
void find_username(); // use client_username_list to find the username in serverA_username_list and serverB_username_list
// send username_to_serverA to serverA
void send_username_to_serverA(); 
// send username_to_serverB to serverB
void send_username_to_serverB(); 
// handle the case when username_not_exist is not empty, return true if username_not_exist is empty
void username_not_exist_handler(); 
// send request to serverA and serverB and handler the case when username_not_exist is not empty
void send_request(); 
// receive result from serverA and serverB 
// and compute the intersection 
// and store the final intersection in result_time_intervals
void receive_result(); 

/**
 * got from Beej's Guide to Network Programming
*/
void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

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
// create TCP socket w/ port number CLIENT_TCP_PORT & bind
void create_TCP_socket(){

    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM; // TCP socket
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    if ((rv = getaddrinfo(LOCAL_HOST, CLIENT_TCP_PORT, &hints, &servinfo)) != 0) { // get address info
        fprintf(stderr, "serverM: create_TCP_socket: getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p->ai_next) { // loop through all the results and bind to the first we can
        if ((sockfd_TCP = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { // create socket
            perror("serverM: create_TCP_socket: socket");
            continue;
        }

        if (setsockopt(sockfd_TCP, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) { // set socket option
            perror("serverM: create_TCP_socket: setsockopt");
            exit(1);
        }

        if (bind(sockfd_TCP, p->ai_addr, p->ai_addrlen) == -1) { // bind socket
            close(sockfd_TCP);
            perror("serverM: create_TCP_socket: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure
    
    if (p == NULL)  { // if we got here, it means we didn't get bound
        fprintf(stderr, "serverM: create_TCP_socket: failed to bind\n");
        exit(1);
    }

}

/**
 * got from Beej's Guide to Network Programming
*/
// create UDP socket w/ port number BACKEND_UDP_PORT & bind
void create_UDP_socket(){
    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_DGRAM; // UDP socket
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    if ((rv = getaddrinfo(LOCAL_HOST, BACKEND_UDP_PORT, &hints, &servinfo)) != 0) { // get address info
        fprintf(stderr, "serverM: create_UDP_socket: getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p->ai_next) { // loop through all the results and bind to the first we can
        if ((sockfd_UDP = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { // create socket
            perror("serverM: create_UDP_socket: socket");
            continue;
        }

        if (bind(sockfd_UDP, p->ai_addr, p->ai_addrlen) == -1) { // bind socket
            close(sockfd_UDP);
            perror("serverM: create_UDP_socket: bind");
            continue;
        }

        break;
    }
    
    if (p == NULL)  { // if we got here, it means we didn't get bound
        fprintf(stderr, "serverM: create_UDP_socket: failed to bind\n");
        exit(1);
    }

    freeaddrinfo(servinfo); // all done with this structure
}
/**
 * got from Beej's Guide to Network Programming
*/
// listen to TCP socket
void listen_TCP_socket(){
    if (listen(sockfd_TCP, BACKLOG) == -1) { // listen to socket
        perror("serverM: TCPlisten");
        exit(1);
    }
    
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("serverM: TCPsigaction");
        exit(1);
    }
}



/**
 * got from Beej's Guide to Network Programming
*/
// accept TCP connection, return true if accept successfully
void accept_TCP_connection(){
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd_TCP, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
        perror("accept");
        exit(1);
    }
}
/**
 * got from Beej's Guide to Network Programming
*/
// receive client username list using TCP and store them in client_username_list
void receive_client_username_list(){
    client_username_list.clear(); // clear the list before adding new usernames
    // Receive the list of usernames from the client
    if ((numbytes = recv(new_fd, buf, MAXBUFLEN - 1, 0)) == -1) {
        perror("serverM: accept_TCP_connection: TCPrecv");
        close(new_fd);
        exit(1);
    }
    buf[numbytes] = '\0';
    string received_data(buf);
    istringstream iss(received_data);
    string username;

    // Process the received data and add usernames to the client_username_list
    while (getline(iss, username, ' ')) { // split the received data by space
        client_username_list.push_back(username);
    }
    // Print the on screen message for the received request
    cout << "Main Server received the request from client using TCP over port "
                << CLIENT_TCP_PORT << "." << endl;

    /*// Print the received usernames
    cout << "Received usernames from the client:" << endl;
    for (const string &username : client_username_list) {
        cout << username << endl;
    }
    */
}

/**
 * got from Beej's Guide to Network Programming
*/
// accept UDP connection
// first, determine the data received is a list of usernames or a list of time intervals
// and if the message is from serverA, store the username list in serverA_username_list
// if the messgae is from serverB, store the username list in serverB_username_list
// after receiving the username list, print the on screen message: 
// "Main Server received the username list from server<A or B> using UDP over port <port number>."
// else if the message is from serverA, store the time interval list in serverA_time_interval_list
// if the messgae is from serverB, store the time interval list in serverB_time_interval_list
// after receiving the time interval list, print the on screen message: 
//"Main Server received from server <A or B> the intersection result using UDP over port <port number>: 
// <[[t1_start, t1_end], [t2_start, t2_end], … ]>."

void accept_UDP_connection(){
    addr_len = sizeof their_addr;
    // receive message from serverA or serverB
    if ((numbytes = recvfrom(sockfd_UDP, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) { 
        perror("serverM: accept_UDP_connetion: recvfrom");
        exit(1);
    }
    // Add null terminator to the buffer
    buf[numbytes] = '\0';

    // Identify the server from which the message was received
    char server_id;
    uint16_t port = ntohs(((struct sockaddr_in *)&their_addr)->sin_port); 
    
    if (port == atoi(SERVER_A_UDP_PORT)) {
        server_id = 'A';
    } else if (port == atoi(SERVER_B_UDP_PORT)) {
        server_id = 'B';
    } else {
        fprintf(stderr, "serverM: accept_UDP_connetion: Received message from an unknown server\n");
        return;
    }

    if (isalpha(buf[0])) { //if the message is a list of usernames, which means buf[0] is a english letter
        string received_data(buf);
        istringstream iss(received_data);
        string username;

        // Process the received data and add usernames to the serverA_username_list or serverB_username_list
        if (server_id == 'A'){
            received_serverA_username_list = true; // set the flag to true
            while (getline(iss, username, ' ')){
                serverA_username_list.push_back(username);
            }
            cout << "Main Server received the username list from server A using UDP over port " << BACKEND_UDP_PORT << "." << endl;
        }else if (server_id == 'B'){
            received_serverB_username_list = true; // set the flag to true
            while (getline(iss, username, ' ')){
                serverB_username_list.push_back(username);
            }
            cout << "Main Server received the username list from server B using UDP over port " << BACKEND_UDP_PORT << "." << endl;
        }
    } else if (buf[0] == '[') { // if the message is a list of time intervals, which means buf[0] is '['
    string received_data(buf);
    regex interval_regex("\\[([0-9]+), ([0-9]+)\\]");

    sregex_iterator it(received_data.begin(), received_data.end(), interval_regex);
    sregex_iterator end;

    if (server_id == 'A'){
        received_serverA_time_interval_list = true; // set the flag to true
        while (it != end) {
            smatch match = *it;
            string start_time = match[1].str();
            string end_time = match[2].str();
            string time_interval = "[" + start_time + ", " + end_time + "]";
            serverA_time_interval_list.push_back(time_interval);
            ++it;
        }
        //"Main Server received from server <A or B> the intersection result using UDP over port <port number>: <[[t1_start, t1_end], [t2_start, t2_end], … ]>."
        cout << "Main Server received from server A the intersection result using UDP over port " << BACKEND_UDP_PORT << ": [";
                if (!serverA_time_interval_list.empty()){
                    for (const string &time_interval : serverA_time_interval_list) {
                        cout << time_interval << ", ";
                    }
                    cout << "\b\b]." << endl;
                } else{
                    cout << "]." << endl;
                }
                
    } else if (server_id == 'B'){
        received_serverB_time_interval_list = true; // set the flag to true
        while (it != end) {
            smatch match = *it;
            string start_time = match[1].str();
            string end_time = match[2].str();
            string time_interval = "[" + start_time + ", " + end_time + "]";
            serverB_time_interval_list.push_back(time_interval);
            ++it;
        }
        cout << "Main Server received from server B the intersection result using UDP over port " << BACKEND_UDP_PORT << ": [";
                if (!serverB_time_interval_list.empty()){
                    for (const string &time_interval : serverB_time_interval_list) {
                        cout << time_interval << ", ";
                    }
                    cout << "\b\b]." << endl;
                } else{
                    cout << "]." << endl;
                }
    } else if (buf[0] == '\0'){ // accept no time intersection
        if (server_id == 'A'){
            received_serverA_time_interval_list = true; // set the flag to true
            cout << "Main Server received from server B the intersection result using UDP over port " 
            << BACKEND_UDP_PORT << ": <>." << endl;
        } else if (server_id == 'B'){
            received_serverB_time_interval_list = true; // set the flag to true
            cout << "Main Server received from server B the intersection result using UDP over port "
            << BACKEND_UDP_PORT << ": <>." << endl;
        }
    }


    }
}   

// use client_username_list to find the username in serverA_username_list and serverB_username_list
// if the username is found in serverA_username_list store in client_to_serverA list
// if the username is found in serverB_username_list store in client_to_serverB list
// if the username is not found in both serverA_username_list and serverB_username_list store in username_not_exist list
void find_username(){
    username_to_serverA.clear(); // clear the previous data
    username_to_serverB.clear();
    username_not_exist.clear();
    result_username_list.clear();
    for (const string &username : client_username_list) {
        if (find(serverA_username_list.begin(), serverA_username_list.end(), username) != serverA_username_list.end()) {
            username_to_serverA.push_back(username);
            result_username_list.push_back(username);
        } else if (find(serverB_username_list.begin(), serverB_username_list.end(), username) != serverB_username_list.end()) {
            username_to_serverB.push_back(username);
            result_username_list.push_back(username);
        } else {
            username_not_exist.push_back(username);
        }
    }
}

// handle the case when username_not_exist is not empty
// if username_not_exist is not empty, print error message:"<username1, username2, …> do not exist. Send a reply to the client."
// and send user_not_exist list back to the client
// return true if username_not_exist is empty
void username_not_exist_handler(){
    if (!username_not_exist.empty()) {
        // send username_not_exist list back to the client
        string username_list, not_exist_message;
        for (const string &username : username_not_exist) {
            username_list += username + ", ";
        }
        if (!username_list.empty()){
            username_list += "\b\b";
        }
        not_exist_message = username_list + " do not exist.";
        if ((numbytes = send(new_fd, not_exist_message.c_str(), not_exist_message.length(), 0)) == -1) {
            perror("serverM: username_not_exit_handler: send");
            exit(1);
        }

        for (const string &username : username_not_exist) {
            cout << username << ", ";
        }
        cout << "\b\b do not exist. Send a reply to the client." << endl;
    }
}

// send username_to_serverA to serverA
void send_username_to_serverA() {
    int list_not_empty_flag = !username_to_serverA.empty();
    // If username_to_serverA is not empty, send the username list to serverA
    if (list_not_empty_flag) {
        // initilize UDP connection to serverA
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        if ((rv = getaddrinfo(LOCAL_HOST, SERVER_A_UDP_PORT, &hints, &servinfo)) != 0) {
            fprintf(stderr, "serverM: send_username_to_serverA: getaddrinfo: %s\n", gai_strerror(rv));
            exit(1);
        }
        string username_list;
        for (const string &username : username_to_serverA) {
            username_list += username + " ";
        }
        username_list.pop_back(); // remove the last space

        if ((numbytes = sendto(sockfd_UDP, username_list.c_str(), username_list.length(), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("serverM: sendto serverA");
            exit(1);
        }
        // Print on screen message: "Found <username1, username2, …> located at Server A. Send to ServerA."
        cout << "Found <";
        for (const string &username : username_to_serverA) {
            cout << username << ", ";
        }
        cout << "\b\b> located at Server A. Send to ServerA." << endl;
        // Free the allocated memory for the addrinfo structure
        freeaddrinfo(servinfo);
    }
}


// send username_to_serverB to serverB
// similar to send_username_to_serverA()
void send_username_to_serverB(){
    int list_not_empty_flag = !username_to_serverB.empty();
    if (list_not_empty_flag) {
        // Initialize the UDP connection to serverB
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        if ((rv = getaddrinfo(LOCAL_HOST, SERVER_B_UDP_PORT, &hints, &servinfo)) != 0) {
            fprintf(stderr, "serverM: send_username_to_serverB: getaddrinfo: %s\n", gai_strerror(rv));
            exit(1);
        }
        string username_list;
        for (const string &username : username_to_serverB) {
            username_list += username + " ";
        }
        username_list.pop_back(); // remove the last space
        if ((numbytes = sendto(sockfd_UDP, username_list.c_str(), username_list.length(), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("serverM: sendto serverB");
            exit(1);
        }
        // print on screen message: "Found <username1, username2, …> located at Server B. Send to ServerB."
        cout << "Found <";
        for (const string &username : username_to_serverB) {
            cout << username << ", ";
        }
        cout << "\b\b> located at Server B. Send to ServerB." << endl;
        // Free the allocated memory for the addrinfo structure
        freeaddrinfo(servinfo);
    }
}
// send request to serverA and serverB and handler the case when username_not_exist is not empty
// first process the received username list by calling find_username()
// second process the username_not_exist list by calling username_not_exist_handler()
// if username_not_exist_handler return true, send username_to_serverA to serverA and send username_to_serverB to serverB
void send_request(){
    find_username();
    username_not_exist_handler();
    send_username_to_serverA();
    send_username_to_serverB();
}
// receive the intersection results from serverA and serverB by calling accept_UDP_connection()
// and compare the two serverA_time_interval_list and serverB_time_interval_list lists
// and store the intersection results in result_time_intervals global variable
// ie. if serverA_time_interval_list = [[1, 3], [5, 10], [12, 16], [17, 18], [21, 23]], 
// and serverB_time_interval_list = [[0, 4], [8, 11], [15, 17], [18, 24]]
// then result_time_intervals = [[1, 3], [8, 10], [15, 16], [21, 23]]
void receive_result(){
    serverA_time_interval_list.clear(); // clear the previous time interval list
    serverB_time_interval_list.clear(); // clear the previous time interval list
    received_serverA_time_interval_list = false; // reset the flag
    received_serverB_time_interval_list = false; // reset the flag
    if (username_to_serverA.empty() && !username_to_serverB.empty()){ // if username_to_serverA is empty, expecting only UDP connection from serverB
        accept_UDP_connection();
    }else if (username_to_serverB.empty() && !username_to_serverA.empty()){// if username_to_serverB is empty, expecting only UDP connection from serverA
        accept_UDP_connection();
    }else{
        // wait until both serverA_time_interval_list and serverB_time_interval_list are received
        while(!received_serverA_time_interval_list || !received_serverB_time_interval_list){
            accept_UDP_connection();
            this_thread::sleep_for(chrono::milliseconds(10)); // add a small delay to avoid busy waiting
        }
    }
    result_time_intervals.clear(); // clear the previous result_time_intervals
    if(username_to_serverA.empty() && serverA_time_interval_list.empty()){
        result_time_intervals = serverB_time_interval_list;
    }else if(username_to_serverB.empty() && serverB_time_interval_list.empty()){
        result_time_intervals = serverA_time_interval_list;
    }else{
        // Compare the two time interval lists and store the intersection results in result_time_intervals
        auto it_a = serverA_time_interval_list.begin(); // iterator for serverA_time_interval_list
        auto it_b = serverB_time_interval_list.begin(); // iterator for serverB_time_interval_list

    
        while (it_a != serverA_time_interval_list.end() && it_b != serverB_time_interval_list.end()) {
            string a_interval = *it_a; // get the current interval from serverA_time_interval_list
            string b_interval = *it_b; // get the current interval from serverB_time_interval_list

            int start_a = stoi(a_interval.substr(1, a_interval.find(',') - 1)); // get the start time of a_interval
            // get the end time of a_interval
            int end_a = stoi(a_interval.substr(a_interval.find(',') + 2, a_interval.size() - a_interval.find(',') - 3)); 

            int start_b = stoi(b_interval.substr(1, b_interval.find(',') - 1));
            int end_b = stoi(b_interval.substr(b_interval.find(',') + 2, b_interval.size() - b_interval.find(',') - 3));

            int max_start = max(start_a, start_b); 
            int min_end = min(end_a, end_b);

            if (max_start < min_end) {
                string result_interval = "[" + to_string(max_start) + ", " + to_string(min_end) + "]";
                result_time_intervals.push_back(result_interval);
            }

            if (end_a < end_b) {
                it_a++;
            } else {
                it_b++;
            }
        }
    }
    
    
        
    cout << "Found the intersection between the results from server A and B: [";
        if (!result_time_intervals.empty()){
            for (const string &interval : result_time_intervals) {
                cout << interval << ", ";
            }
            cout << "\b\b]." << endl;
        } else {
            cout << "]." << endl;
        }
}
// send result_time_intervals to the client
void reply_to_client() {
    // Convert result_time_intervals list to a single string
    string result_interval_str, result_username_str, result;
    result_interval_str = "[";
    for (const auto& interval : result_time_intervals) {
        result_interval_str += interval + ", ";
    }
    if(result_interval_str == "["){
        result_interval_str = "[]";
    }else{
        result_interval_str += "\b\b]";
    }
    
    for(const auto& username : result_username_list){
        result_username_str += username + ", ";
    }
    if(!result_username_str.empty()){
        result_username_str += "\b\b ";
    }
    result = "Time intervals " + result_interval_str + " works for " + result_username_str;
    // Send the result to the client
    if (send(new_fd, result.c_str(), result.length(), 0) == -1) {
        perror("serverM: reply_to_client: send");
    }
    
    
    cout << "Main Server sent the result to the client." << endl;
}



int main (void){
    create_TCP_socket(); // create TCP socket w/ port number CLIENT_TCP_PORT & bind
    listen_TCP_socket(); // listen to TCP socket
    create_UDP_socket(); // create UDP socket w/ port number BACKEND_UDP_PORT & bind
    while(!received_serverA_username_list || !received_serverB_username_list){ // wait for serverA and serverB to send their username list`
        accept_UDP_connection(); // expect to receive from serverA and serverB
        this_thread::sleep_for(chrono::milliseconds(10)); // Add a short delay
    }
    printf("The Main server is up and running.\n");
    accept_TCP_connection(); // accept TCP connection from client
    /**
     * got from Beej's Guide to Network Programming
    */
    while(1){
        receive_client_username_list();
        send_request(); // send request to serverA and serverB
        if(!username_to_serverA.empty() || !username_to_serverB.empty()){
            //receive the intersection results from serverA and serverB
            receive_result();
            // send the intersection results to the client
            reply_to_client();

            
        }

        this_thread::sleep_for(chrono::milliseconds(10)); // Add a short delay
    }


    // close sockets
    close(new_fd);
    close(sockfd_TCP);
    close(sockfd_UDP);
    return 0;
}