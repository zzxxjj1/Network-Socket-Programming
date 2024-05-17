/**
 * serverA.cpp - a backend server that processes a database file a.txt which contains username and timer interval
 *               in the format of "username;[[t1_start,t1_end],[t2_start,t2_end]...]"
 *               check for any input errors as reading a.txt and print out the error messages
 *               stores the username in a list and the time intervals in a map<string, list<string>>
 *               and send the list of usernames to serverM via UDP
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
#include <map>
#include <fstream>
#include <regex>


using namespace std;

/**
 * constants definition
*/
#define LOCAL_HOST "127.0.0.1"
#define SERVER_A_PORT "21984"
#define SERVER_M_PORT "23984"
#define MAXBUFLEN 1024
#define BACKLOG 10

/**
 * gobal variables
*/
list<string> username_list;
map<string, list<string>> time_interval;
list<string> request_user_list;
list<string> result_time_intervals;

/**
 * socket variables
*/
int sockfd;
struct addrinfo hints, *servinfo, *p;
int rv;
int numbytes;
struct sockaddr_storage their_addr;
char buf[MAXBUFLEN];
socklen_t addr_len;
char s[INET_ADDRSTRLEN];

/**
 * function prototypes
*/
void *get_in_addr(struct sockaddr *sa);
void read_file();
void print_data();
void print_result_time_interval();
void create_socket();
bool accept_connection();
void send_username_list();
list<string> intersect_intervals(list<string>& time_intervals1, list<string>& time_intervals2);
void find_intersection();
void send_result();

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

// read a.txt, check for any input errors 
// and store the username and time intervals in the list and map
void read_file(){
    ifstream infile;
    infile.open("a.txt");
    if(!infile){
        cout << "Error: cannot open file a.txt" << endl;
        exit(1);
    }

    string line;
    while (getline(infile, line)) {
    
        // Split the line into username and time availability
        size_t pos = line.find(';');
        string username = line.substr(0, pos);
        string time_availability = line.substr(pos + 1);

        auto usr_start = username.find_first_not_of(' ');
        auto usr_end = username.find_last_not_of(' ');
        if (usr_start == string::npos || usr_end == string::npos){
            cout << "Error: username cannot be empty" << endl;
            exit(1);
        }
        username = username.substr(usr_start, usr_end - usr_start + 1);
        if (username.find(' ') != string::npos) {
            cout << "Error: username cannot contain space" << endl;
            exit(1);
        } else if (username.length() > 20){
            cout << "Error: username cannot be longer than 20 characters" << endl;
            exit(1);
        } else if (username.length() == 0){
            cout << "Error: username cannot be empty" << endl;
            exit(1);
        } //username can only contain small letters
        else if (username.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != string::npos){
            cout << "Error: username can only contain small letters" << endl;
            exit(1);
        } 
        // Remove spaces from the time availability string
        time_availability.erase(remove_if(time_availability.begin(), time_availability.end(), ::isspace), time_availability.end());

        // Add the username to the list
        username_list.push_back(username);

        // Parse the time intervals
        list<string> intervals;
        regex interval_regex("\\[([0-9]+),([0-9]+)\\]");
        sregex_iterator it(time_availability.begin(), time_availability.end(), interval_regex);
        sregex_iterator end;

        int prev_end_time = -1;
        int interval_count = 0;

        while (it != end) {
            smatch match = *it;
            string start_time_str = match[1].str();
            string end_time_str = match[2].str();
            // Ensure start_time and end_time are integers
            if (!regex_match(start_time_str, regex("[0-9]+")) || !regex_match(end_time_str, regex("[0-9]+"))) {
                cout << "Error: time values must be integers between 0 and 100" << endl;
                exit(1);
            }
            int start_time = stoi(match[1].str());
            int end_time = stoi(match[2].str());
            // Ensure start time is less than end time and previous end time is less than the current start time
            if (start_time > end_time || prev_end_time >= start_time) {
                cout << "Error: start time must be less than end time and previous end time must be less than the current start time" << endl;
                exit(1);
            }
            string time_interval = "[" + start_time_str + ", " + end_time_str + "]";
            intervals.push_back(time_interval);
            ++it;

            prev_end_time = end_time;
            ++interval_count;

            if (interval_count > 10) {
                cout << "Error: total time intervals should not be larger than 10" << endl;
                exit(1);
            }
        }

        // Add the time intervals to the map
        time_interval[username] = intervals;
    }

    infile.close();
}

// print the username list and time intervals for error checking
void print_data() {
    cout << "Username List: ";
    for (const string& username : username_list) {
        cout << username << " ";
    }
    cout << endl;

    cout << "\nTime Intervals:" << endl;
    for (const auto& entry : time_interval) {
        cout << entry.first << ": ";
        for (const string& interval : entry.second) {
            cout << interval << " ";
        }
        cout << endl;
    }
}

void print_result_time_interval(){
    cout << "Result Time Interval: ";
    for (const string& interval : result_time_intervals) {
        cout << interval << " ";
    }
    cout << endl;
}

/**
 * got from Beej's Guide to Network Programming
*/
// create a UDP socket and bind to the port
void create_socket(){
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if((rv = getaddrinfo(LOCAL_HOST, SERVER_A_PORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1){
            perror("serverA: socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            perror("serverA: bind");
            continue;
        }
        break;
    }

    if (p == NULL){
        fprintf(stderr, "serverA: failed to bind socket\n");
        exit(2);
    }

    freeaddrinfo(servinfo);
}

/**
 * got from Beej's Guide to Network Programming
*/
// accept the connection from serverM
// store the username that serverM sent in request_user_list
bool accept_connection(){
    addr_len = sizeof their_addr;
    if((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1, 0,
        (struct sockaddr *)&their_addr, &addr_len)) == -1){
        perror("serverA: accept_connection: recvfrom");
        return false;
    }
    buf[numbytes] = '\0'; // add null terminator
    // store the username that serverM sent in request_user_list
    // and print "Server A received the usernames from Main Server using UDP
    // over SERVER_A_PORT".
    string received_usernames(buf); 
    istringstream iss(received_usernames); 
    string username;
    request_user_list.clear(); // clear the list
    while (getline(iss, username, ' ')) {
        request_user_list.push_back(username);
    }

    cout << "Server A received the usernames from Main Server using UDP over port " << SERVER_A_PORT << "." << endl;
    return true;
}

/**
 * got from Beej's Guide to Network Programming
*/
// send username_list to serverM using UDP
// format: username1 username2 username3 ...
void send_username_list(){
    string username_list_str = "";
    for(const string& username : username_list){
        username_list_str += username + " ";
    }
    username_list_str.pop_back(); // remove the last space

    //initialize the connection to serverM
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    if((rv = getaddrinfo(LOCAL_HOST, SERVER_M_PORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    // Loop through all the results and send using the first valid address
    for (p = servinfo; p != NULL; p = p->ai_next) {
        // Send the username list to serverM
        if ((numbytes = sendto(sockfd, username_list_str.c_str(), username_list_str.length(), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("send_result: sendto");
            exit(1);
        }
        break; // Successfully sent the data, exit the loop
    }    
    
    if (p == NULL) {
        fprintf(stderr, "send_result: failed to send data\n");
        exit(1);
    }
    // Cleanup
    freeaddrinfo(servinfo);
    
    // Server <A or B> finished sending a list of usernames to Main Server
    cout << "The serverA finished sending a list of usernames to Main Server." << endl;
}

// Helper function to find the intersection of two time interval lists
list<string> intersect_intervals(list<string>& time_intervals1, list<string>& time_intervals2) {
    list<string> result;

    auto it1 = time_intervals1.begin();
    auto it2 = time_intervals2.begin();

    while (it1 != time_intervals1.end() && it2 != time_intervals2.end()) {
        string interval1 = *it1;
        string interval2 = *it2;

        int start1 = stoi(interval1.substr(1, interval1.find(',') - 1));
        int end1 = stoi(interval1.substr(interval1.find(',') + 2, interval1.size() - interval1.find(',') - 3));

        int start2 = stoi(interval2.substr(1, interval2.find(',') - 1));
        int end2 = stoi(interval2.substr(interval2.find(',') + 2, interval2.size() - interval2.find(',') - 3));

        int max_start = max(start1, start2);
        int min_end = min(end1, end2);

        if (max_start < min_end) {
            string result_interval = "[" + to_string(max_start) + ", " + to_string(min_end) + "]";
            result.push_back(result_interval);
        }

        if (end1 < end2) {
            it1++;
        } else {
            it2++;
        }
    }

    return result;
}

// Find the intersection of the time intervals of all users in request_user_list
void find_intersection() {
    result_time_intervals.clear(); // Clear any previous results

    // If there is only one user in the request_user_list, copy their time intervals to the result
    if (request_user_list.size() == 1) {
        string user = request_user_list.front();
        result_time_intervals = time_interval[user];
        return;
    }

    // Start with the time intervals of the first user
    result_time_intervals = time_interval[request_user_list.front()];

    // Iterate over the rest of the users in request_user_list
    for (auto it = ++request_user_list.begin(); it != request_user_list.end(); it++) {
        string user = *it;
        list<string>& time_intervals = time_interval[user];

        // Update result_time_intervals with the intersection of its current content and the current user's time intervals
        result_time_intervals = intersect_intervals(result_time_intervals, time_intervals);

        // If there's no intersection, there's no need to continue
        if (result_time_intervals.empty()) {
            break;
        }
    }
    cout << "Found the intersection result: [";
        if(!result_time_intervals.empty()){
            for (const string& interval : result_time_intervals) {
            cout << interval << ", ";
            }
    
            cout << "\b\b] for <";
        } else {
            cout << "] for <";
        }
        
        for (const string& user : request_user_list) {
            cout << user << ", ";
        }
    cout << "\b\b>" << endl;
}

/**
 * got from Beej's Guide to Network Programming
*/
// Send result_time_intervals to serverM using UDP
void send_result(){
    // Convert result_time_intervals to a string
    string result_str = "";
    for (const string& interval : result_time_intervals) {
        result_str += interval + " ";
    }
    result_str.pop_back(); // remove the last space
    // Initialize the connection to serverM
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if((rv = getaddrinfo(LOCAL_HOST, SERVER_M_PORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    
    // Loop through all the results and send using the first valid address
    for (p = servinfo; p != NULL; p = p->ai_next) {
        // Send the result to serverM
        if ((numbytes = sendto(sockfd, result_str.c_str(), result_str.length(), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("send_result: sendto");
            continue;
        }
        break; // Successfully sent the data, exit the loop
    }

    if (p == NULL) {
        fprintf(stderr, "send_result: failed to send data\n");
        exit(1);
    }


    cout << "Server A finished sending the response to Main Server." << endl;

    // Cleanup
    freeaddrinfo(servinfo);
}

int main(){
    read_file();
    create_socket();
    cout << "The Server A is up and running using UDP on port " << SERVER_A_PORT << endl;
    send_username_list();
    while(1){
        if(accept_connection()){
            find_intersection();
            send_result();
        }
    }
    close(sockfd);
    return 0;
}