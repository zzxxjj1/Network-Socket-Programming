# Network-Socket-Programming
Network socket program system via UDP and TCP which schedules time interval for meeting.

I just did the basic requirement of the project, not including the registration part.
The project includes four .cpp files: client.cpp serverM serverA serverB
    client.cpp: communicate serverM via TCP, sends username written by the user
    and receive the result time intersection from serverM.

    serverM.cpp: takes usernames from client and sends usernames to respective
    backend server (serverA, serverB) via UDP, receives the time intersection from
    backend server via UDP, calculates the final time intersection, and sends the
    the result back to client via TCP.

    serverA/B.cpp: reads and stores the respective .txt database, sends the 
    usernames to serverM via UDP, receives the request from serverM to calculate
    the intersections, and sends the result back to serverM via UDP.

The format of client input is 1-10 usernames that are all small letter, separated
by spaces. ie. "john jane james amy"

No idiosyncrasy of the project, just enter the username in the format described 
above, the program should work just fine.

Reused code: no reused code from the internet expect from Beej's Guide. If a 
function uses the code from Beej' Guide, I included a comment on top of the
function saying some of the code in this function got from Beej's Guide.
