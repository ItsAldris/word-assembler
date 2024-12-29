#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <netdb.h>
#include <poll.h> 
#include <unordered_set>
#include <unordered_map>
#include <signal.h>
#include <string.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <atomic>
#include <random>
#include <condition_variable>
#include <pthread.h>

// We're planning to use poll and threads, working on it
// Quit program with q+enter (ctrl+c should work too)

#define SIZE 255 // buffer size

int serverFd;
int maxDescrCount = 16;
int descrCount = 0;
pollfd * pollFds;
std::mutex mtx;
bool isGameRunning = false;
bool isRoundRunning = false;
std::atomic_bool stopTimer = false;
std::atomic_bool countdownOn = false;
std::atomic_bool gameEnd = false;
std::default_random_engine gen(std::random_device{}());
std::condition_variable cv;
int answers = 0;

std::thread gameLoopT;
std::thread countDownT;
std::thread timerT;
std::vector<std::thread> threads;

// Structures
std::unordered_map<int,std::string> players;
std::unordered_map<std::string,int> scores;
std::unordered_map<std::string,int> words;
std::unordered_set<int> inGame;

// Those will be put in a configuration file later (hopefully)
int numOfRounds = 3; // Rounds in a single game
int basePoints = 10; // Points for correct word
int bonusPoints = 5; // Bonus for being first
int negativePoints = -10; // Points for providing incorrect answer
int waitForPlayersTime = 10; // How long the server waits for more players to join
int roundTime = 10; // How long one round lasts
int letterCount = 10; // The number of letters chosen in one round

short getPort(char * port);

void serverShutdown(int);

void countdown(int seconds, std::function<void(int)> onTick);

void gameLoop();

void waitForPlayers();

void roundStart();

std::string generateLetters();

bool checkIfCorrectWord(std::string word);

void sendToAllPlaying(std::string message);

void handleServerEvent(int revents);

void handleClientEvent(int clientDescr);

void handleInput(int clientFd);

void getNickname(int clientFd);

void waitingRoom();

void removeClient(int clientId);

void handleStdInput(int revents);

void joinThreads();


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

    // *****************************************

    // Setup data for poll and add server socket and stdin
    pollFds = (pollfd *) malloc(sizeof(pollfd) * maxDescrCount);
    pollFds[0].fd = serverFd;
    pollFds[0].events = POLLIN;
    descrCount++;
    pollFds[1].fd = STDIN_FILENO;
    pollFds[1].events = POLLIN;
    descrCount++;

    // Begin the game logic
    gameLoopT = std::thread(gameLoop);

    // Wait for events
    while(1)
    {
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
                if(pollFds[i].fd == STDIN_FILENO)
                {
                    handleStdInput(pollFds[i].revents);
                }
                if(pollFds[i].fd == serverFd)
                {
                    handleServerEvent(pollFds[i].revents); // Accept new connection
                }
                else
                {
                    handleClientEvent(i); // Some client event
                }
                ready--;
            }
        }
    }

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
    stopTimer = true;
    countdownOn = false;
    gameEnd = true;

    cv.notify_all();

    joinThreads();

    if (countDownT.joinable())
    {
        countDownT.join();
    }

    // if (timerT.joinable())
    // {
    //     timerT.join();
    // }

    if (gameLoopT.joinable())
    {
        gameLoopT.join();
    }

    // Shutdown and close all client sockets
    for (int i = 2; i < descrCount; i++)
    {
        shutdown(pollFds[i].fd, SHUT_RDWR);
        close(pollFds[i].fd);
        printf("Disconnecting with player %d...\n", i-1);
    }
    // Close server socket and exit the program
    free(pollFds);
    close(serverFd);

    printf("Shutting down...\n");
    exit(0);
}

// Countdown for rounds and for waiting for players
void countdown(int seconds, std::function<void(int)> onTick) 
{
    while (seconds > 0) 
    {
        if (stopTimer || gameEnd) { break; }
        onTick(seconds-1); 
        std::this_thread::sleep_for(std::chrono::seconds(1));
        --seconds;
    }
    // std::unique_lock<std::mutex> lock(mtx);
    countdownOn = false;
    cv.notify_all();
}

// Main loop of the game, all logic is handled here
void gameLoop()
{
    printf("Looping...\n");

    // The loop in question
    while(1)
    {
        if (gameEnd) { pthread_exit(nullptr); }
        waitForPlayers();
        joinThreads();
        // Add all players to the game
        for (auto player : players)
        {
            inGame.insert(player.first);
        }

        // Start the game
        isGameRunning = true;
        for (int i = 0; i < numOfRounds; i++)
        {
            // Check if still enough players in game
            if (inGame.size() < 2)
            {
                printf("Not enough players to continue the game.\n");
                break;
            }
            roundStart();
        }
        isGameRunning = false;
        scores.clear();
        inGame.clear();
    }
}

// Called when there are too few players to start the game
void waitForPlayers()
{
    while(1)
    {
        printf("Waiting for players...\n");

        countdownOn = false;
        stopTimer = false;

        // Countdown callback setup
        auto onTick = [](int timeLeft) 
        {
            printf("Time left: %d\n", timeLeft);
        };

        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { return gameEnd || players.size() >= 2; });
        }

        if (gameEnd)
        {
            pthread_exit(nullptr);
        }

        threads.emplace_back(countdown, waitForPlayersTime, onTick);
        countdownOn = true;

        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { return gameEnd || players.size() < 2 || !countdownOn; });
        }

        if (gameEnd)
        {
            pthread_exit(nullptr);
        }
        else if (players.size() < 2)
        {
            stopTimer = true;
            joinThreads();
            countdownOn = false;
            printf("Not enough players, waiting again...\n");
            continue;
        }
        else
        {
            printf("Done waiting!\n");
            return;
        }
    }
}

// The round begins
void roundStart()
{
    if (gameEnd) { pthread_exit(nullptr); }
    printf("Starting the round...\n");
    isRoundRunning = true;
    answers = 0;
    // Generate a random sequence of letters and send it to all players
    // Wait for players to send their words or until the time runs out
    // Assign the scores to players
    std::string letters = generateLetters();
    sendToAllPlaying(letters);
    int countdownLeft = roundTime;
    stopTimer = false;

    // Countdown callback setup
    auto onTick = [&countdownLeft](int timeLeft) 
    {
        {
            printf("Time until round ends: %d\n", timeLeft);
        }
        if (timeLeft == 0)
        {
            stopTimer = true;
            cv.notify_all();
        }
    };

    timerT = std::thread(countdown, roundTime, onTick);
    countdownOn = true;

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return gameEnd || !countdownOn || answers == inGame.size(); });
    }
    //printf("words.size() = %lu\n inGame.size() = %lu\n", words.size(), inGame.size());

    if (gameEnd) 
    {
        stopTimer = true;
        countdownOn = false;
        timerT.join();
        pthread_exit(nullptr);
    }
    
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!stopTimer)
        {
            stopTimer = true;
        }
    }

    countdownOn = false;
    timerT.join();
    stopTimer = false;
    isRoundRunning = false;

    printf("Round ended!\n");
}

// TODO
// Generates a string made from random letters
std::string generateLetters()
{
    std::string picked = "";
    std::string letters = "abcdefghijklmnoprstuwyz";
    printf("Generating letters...\n");
    for (int i = 0; i < letterCount; i++)
    {
        std::uniform_int_distribution<int> dist(0, letters.length()-1);
        int index = dist(gen);
        picked += letters.at(index);
        letters.erase(index, 1);
    }
    return picked;
}

void sendToAllPlaying(std::string message)
{
    message += '\n';
    for (auto playing : inGame)
    {
        write(playing, message.c_str(), message.size());
    }
}

// TODO
// Handle the event that occured on serverFd
void handleServerEvent(int revents)
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

void handleClientEvent(int clientId)
{
    int clientFd = pollFds[clientId].fd;
    int revents = pollFds[clientId].revents;
    
    if (revents & POLLRDHUP)
    {
        // Client disconnected
        removeClient(clientFd);
    }
    else if (revents & POLLIN)
    {
        // New player
        if (players.find(clientFd) == players.end())
        {
            // Obtain player nickname
            getNickname(clientFd);
            std::string message;
            if (isGameRunning)
            {
                message = "Waiting for the current game to end...\n";
            }
            else
            {
                message = "Waiting for the game to start...\n";
            }
            write(clientFd, message.c_str(), message.size());
        }
        // Player sent a word during current round
        else if (isRoundRunning && inGame.find(clientFd) != inGame.end())
        {
            handleInput(clientFd);
        }
    }
}

void getNickname(int clientFd)
{
    char buffer[SIZE];
    std::string nick;

    while(1)
    {
        memset(buffer, 0, SIZE);
        int r = read(clientFd, buffer, SIZE);
        buffer[strlen(buffer)-1] = '\0';
        if (r > 0)
        {
            nick = buffer;
            //printf("%s\n", nick.c_str());
            // Nickname was already picked
            if (std::any_of(players.begin(), players.end(), [&](const auto& pair) { return pair.second == nick; }))
            {
                std::string message = "Provided nickname is already in use. Try again.\n";
                write(clientFd, message.c_str(), message.size());
            }
            else
            {
                //nick[nick.length()-1] = '\0';
                std::pair<int,std::string> player (clientFd, nick);
                players.insert(player);
                std::pair<std::string,int> score (nick, 0);
                scores.insert(score);
                printf("Hello, %s!\n", nick.c_str());
                cv.notify_all();
                return;
            }
        }
        // else if (r == -1)
        // {
        //     removeClient(clientFd);
        // }
    }
    
}

// TODO
// Handles user input during game
void handleInput(int clientFd)
{
    char buffer[SIZE];
    memset(buffer, 0, SIZE);
    std::string word;
    int score;
    int r = read(clientFd, buffer, SIZE);
    buffer[strlen(buffer)-1] = '\0';
    word = buffer;

    if (checkIfCorrectWord(word))
    {
        score = basePoints;
        std::pair<std::string,int> wordScore (word, score);
        words.insert(wordScore);
        scores[players[clientFd]] += score;
        answers += 1;
        cv.notify_all();
        printf("%s answered: %s\n", players[clientFd].c_str(), word.c_str());
    }
}

// TODO
// Checks if the word provided by the player is a valid one
bool checkIfCorrectWord(std::string word)
{
    return true;
}

void removeClient(int clientFd)
{
    for (int i = 2; i < descrCount; i++)
    {
        if (pollFds[i].fd == clientFd)
        {
            pollFds[i] = pollFds[descrCount-1];
            descrCount--;
        }
    }
    shutdown(clientFd, SHUT_RDWR);
    close(clientFd);
    
    // Clear structures
    std::unordered_map<int,std::string>::const_iterator gotPlayer = players.find(clientFd);
    if (gotPlayer != players.end())
    {
        std::unordered_map<std::string, int>::const_iterator gotScore = scores.find(players[clientFd]);
        if (gotScore != scores.end())
        {
            scores.erase(players[clientFd]);
        }
        std::unordered_set<int>::const_iterator gotInGame = inGame.find(clientFd);
        if (gotInGame != inGame.end())
        {
            inGame.erase(clientFd);
        }
        players.erase(clientFd);
    }
    cv.notify_all();
    printf("Player disconnected\n");
}

void handleStdInput(int revents)
{
    if (revents & POLLIN)
    {
        char c[2];
        int r = read(0, c, 2);
        if (c[0] == 'q') { serverShutdown(SIGINT); }
    }
}

void joinThreads()
{
    for (std::thread & t : threads) 
    {
        t.join();
    }
    if (gameEnd) { gameLoopT.join(); }

    threads.clear();
}