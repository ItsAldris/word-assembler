#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <netdb.h>
#include <poll.h> 
#include <unordered_set>
#include <signal.h>
#include <string.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <condition_variable>

// We're planning to use poll for this project, working on it
// There are issues with waitForPlayers: the game does not start after the countdown reaches 0
// Tried mutex and cv but didn't work either

int serverFd;
int maxDescrCount = 16;
int descrCount = 0;
pollfd * pollFds;
std::mutex mtx;
std::condition_variable cv;

// Those will be put in a configuration file later (hopefully)
int numOfRounds = 3; // Rounds in a single game
int basePoints = 10; // Points for correct word
int bonusPoints = 5; // Bonus for being first
int negativePoints = -10; // Points for providing incorrect answer
int waitForPlayersTime = 10; // How long the server waits for more players to join
int roundTime = 60; // How long one round lasts
int letterCount = 7; // The number of letters generated in one round

short getPort(char * port);

void serverShutdown(int);

void countdown(int seconds, std::function<void(int)> onTick);

void gameLoop();

void waitForPlayers(int waitDuration);

void gameStart(int rounds);

void roundStart(int roundDuration);

char * generateLetters();

int checkIfCorrectWord(char *word);

void sentToAllPlaying();

void newServerEvent(int revents);

void newClientEvent(int clientId);


int main(int argc, char* argv[])
{
    // For now the port number is a command line argument, might change that later
    if (argc < 2)
    {
        printf("Usage: ./server <port>\n");
        exit(1);
    }

    // Server sockaddr_in structure setup
    sockaddr_in serverAddr;
    socklen_t serverAddrSize = sizeof(serverAddr);
    memset(&serverAddr, 0, serverAddrSize);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(getPort(argv[1]));
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // ********** Server socket setup **********
    serverFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverFd == -1)
    {
        error(1, errno, "Failed to create server socket");
        exit(1);
    }

    // Signal handling
    signal(SIGINT, serverShutdown);
    signal(SIGPIPE, SIG_IGN);

    // Set reuseaddr for server socket
    const int one = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof(one));

    int b = bind(serverFd, (sockaddr *) &serverAddr, serverAddrSize);
    if (b == -1)
    {
        error(1, errno, "Failed to bind server socket");
        exit(1);
    }

    int l = listen(serverFd, SOMAXCONN);
    if (l == -1)
    {
        error(1, errno, "Failed to set server socket as listening");
        exit(1);
    }

    // Setup data for poll and add server socket
    pollFds = (pollfd *) malloc(sizeof(pollfd) * maxDescrCount);
    pollFds[0].fd = serverFd;
    pollFds[0].events = POLLIN;
    descrCount++;

    // *****************************************

    // Begin the game logic
    gameLoop();


    // THIS IS UNIMPORTANT, DON'T LOOK

    // sockaddr_in c_addr;
    // socklen_t c_addr_size = sizeof(c_addr);
    
    // memset(&c_addr, 0, c_addr_size);
    // int c = accept(serverFd, (sockaddr *) &c_addr, &c_addr_size);

    // char buffor[20];
    // while(1)
    // {
    //     int message = read(0, buffor, sizeof(buffor));
    //     if (c != -1)
    //     {
    //         int w = write(c, buffor, message);
    //     }
    // }
    // close(c);

    return 0;    
}

short getPort(char * port)
{
    int res = strtol(port, NULL, 10);
    if (!res) 
    {
        error(1, errno, "Invalid port number provided");
        exit(1);
    }
    return (short) res;
}

void serverShutdown(int)
{
    // Shutdown and close all client sockets
    // TODO
    // Close server socket and exit the program
    close(serverFd);
    printf("\nShutting down...\n");
    exit(0);
}

// Countdown for rounds and for waiting for players
void countdown(int seconds, std::function<void(int)> onTick) 
{
    while (seconds > 0) 
    {
        onTick(seconds); 
        std::this_thread::sleep_for(std::chrono::seconds(1));
        --seconds;
    }
    onTick(seconds);
}

// TODO
// Main loop of the game, all logic is handled here
void gameLoop()
{
    printf("Looping...\n");

    // The loop in question
    while(1)
    {
        waitForPlayers(waitForPlayersTime);
        break;
    }
}

// TODO
// Called when there are too few players to start the game
void waitForPlayers(int waitDuration)
{
    printf("Waiting for players...\n");

    int countdownLeft = 0;
    int countdownOn = false;

    // Countdown callback setup
    auto onTick = [&countdownLeft](int timeLeft) 
    {
        {
            printf("Time left: %d\n", timeLeft);
            std::unique_lock<std::mutex> lock(mtx);
            countdownLeft = timeLeft;
        }
    };

    std::thread countDownT;
    while (1)
    {
        // Check if there are at least 2 players and start the game
        if (countdownOn && countdownLeft == 0 && descrCount > 2)
        {
            countdownOn = false;
            countDownT.join();
            printf("Done waiting!\n");
            gameStart(numOfRounds);
        }
        // If more than 2 players begin the countdown
        else if (descrCount > 2 && !countdownOn) 
        {
            // Begin the countdown when at least 2 players are connected
            countDownT = std::thread(countdown, waitDuration, onTick);
            countdownOn = true;
        }
        // Still waiting for players
        else if (countdownOn == 0 || countdownLeft > 0)
        {
            // Poll
            int ready = poll(pollFds, descrCount, -1);
            if (ready == -1)
            {
                error(0, errno, "Poll failure");
                serverShutdown(SIGINT);
            }

            for (int i = 0; i < descrCount && ready > 0; ++i)
            {
                if (pollFds[i].revents)
                {
                    if(pollFds[i].fd == serverFd)
                    {
                        newServerEvent(pollFds[i].revents); // Accept new connection
                    }
                    else
                    {
                        newClientEvent(i); // Some client event (like disconnection)
                    }
                    ready--;
                }
            }
            
        }
    }
}

// TODO
// The game begins
void gameStart(int rounds)
{
    printf("Starting the game...\n");
    // Start a game consisting of as many rounds as specified
    // At the end of the game reset all scores
    // Allow players to join only before the game starts or after it ends
    while(1);
}

// TODO
// The round begins
void roundStart(int roundDuration)
{
    printf("Starting the round...\n");
    // Generate a random sequence of letters and send it to all players
    // Wait for players to send their words or until the time runs out
    // Assign the scores to players
    // Check if there are still enough players in game 
    // -> if not then go back to waitForPlayers and then start a new game
}

// TODO
// Generates a string made from random letters
char * generateLetters(int letterCount)
{
    printf("Generating letters...\n");
    char *letters = (char *)"aguhjsk";
    return letters;
}

// TODO
void sentToAllPlaying()
{

}

// TODO
// Checks if the word provided by the player is a valid one
int checkIfCorrectWord(char *word)
{
    return 0;
}

// TODO
// Handle the event that occured on serverFd
void newServerEvent(int revents)
{
    if (revents & POLLIN)
    {
        // Accept new connection from client
        sockaddr_in clientAddr {};
        socklen_t clientAddrSize = sizeof(clientAddr);
        
        int clientFd = accept(serverFd, (sockaddr *) &clientAddr, &clientAddrSize);
        if (clientFd == -1)
        {
            error(0, errno, "Error when receiving new connection from client");
        }
        
        // TODO
        // Add resizable pollFds
        
        pollFds[descrCount].fd = clientFd;
        pollFds[descrCount].events = POLLIN|POLLRDHUP;
        descrCount++;
        
        printf("New player connected: %s on port %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
    }
    else
    {
        error(0, errno, "Unexpected event %d on server socket", revents);
        serverShutdown(SIGINT);
    }
}

// TODO
void newClientEvent(int clientId)
{

}