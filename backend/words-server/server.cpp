#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
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
std::string message;
std::string letters;

std::thread gameLoopT;
std::vector<std::thread> threads;

// Structures
std::unordered_map<int,std::string> players;
std::unordered_map<std::string,int> scores;
std::unordered_set<std::string> inGame;
std::unordered_set<std::string> words;
std::unordered_set<std::string> dictionary;

// Those will be put in a configuration file later (hopefully)
int numOfRounds = 3; // Rounds in a single game
int basePoints = 10; // Points for correct word
int bonusPoints = 5; // Bonus for being first
int negativePoints = -10; // Points for providing incorrect answer
int waitForPlayersTime = 10; // How long the server waits for more players to join
int roundTime = 10; // How long one round lasts
int letterCount = 10; // The number of letters chosen in one round
std::string dictPath = "en_US.dic";

short getPort(char * port);

void serverShutdown(int);

void countdown(int seconds, std::function<void(int)> onTick);

void gameLoop();

void waitForPlayers();

void roundStart(int roundNum);

std::string generateLetters();

bool checkIfCorrectWord(std::string word);

void sendToAllPlaying(std::string message);

void sendToAll(std::string message);

void handleServerEvent(int revents);

void handleClientEvent(int clientDescr);

void handleInput(int clientFd);

void getNickname(int clientFd, int descr);

void removeClient(int clientId);

void handleStdInput(int revents);

void joinThreads();

void readDictionary(std::string path);


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

    // Load the dictionary file
    readDictionary(dictPath);

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
    message = "Server shutting down";
    sendToAll(message);
    stopTimer = true;
    countdownOn = false;
    gameEnd = true;

    cv.notify_all();

    joinThreads();

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
        waitForPlayers();
        joinThreads();
        message = "New game starts!\nPlayers in this game: ";
        sendToAll(message);
        printf("%s\n", message.c_str());
        // Add all players to the game
        for (auto player : players)
        {
            std::pair<int,std::string> pl (player.first, player.second);
            inGame.insert(player.second);
            message = player.second;
            sendToAll(message);
            printf("%s\n", message.c_str());
        }

        // Start the game
        isGameRunning = true;
        for (int i = 1; i <= numOfRounds; i++)
        {
            // Check if still enough players in game
            if (inGame.size() < 2)
            {
                break;
            }
            sendToAll("Round " + std::to_string(i) + " of " + std::to_string(numOfRounds) + " begins!");
            roundStart(i);
            sendToAll("Round " + std::to_string(i) + " of " + std::to_string(numOfRounds) + " ended!\nCurrent scores:");
            for (auto s : scores)
            {
                // Only show scores of players currently in game
                if (inGame.find(s.first) != inGame.end())
                {
                    sendToAll(s.first + ": " + std::to_string(s.second));
                }
            }
            // Show correct answers after the round ends
            message = "Correct words from this round:";
            sendToAll(message);
            for (std::string word : words)
            {
                sendToAll(word);
            }
            words.clear();
            joinThreads();
        }
        isGameRunning = false;
        message = "Game ended! Top 3 players:";
        sendToAll(message);
        printf("%s\n", message.c_str());
        std::vector<std::pair<std::string, int>> highscores(scores.begin(), scores.end());
        std::sort(highscores.begin(), highscores.end(), [](auto a, auto b) { return a.second > b.second; });
        for (int i = 0; i < 3 && i < highscores.size(); i++)
        {
            if (inGame.find(highscores[i].first) == inGame.end()) { continue; }
            message = highscores[i].first + " with " + std::to_string(highscores[i].second) + " points!";
            sendToAll(message);
            printf("%s\n", message.c_str());
        }
        for (auto & s : scores)
        {
            s.second = 0;
        }
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
            message = "The game starts in: " + std::to_string(timeLeft);
            sendToAll(message);
            printf("%s\n", message.c_str());
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
            message = "Not enough players, waiting again...";
            sendToAll(message);
            printf("%s\n", message.c_str());
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
void roundStart(int roundNum)
{
    printf("Starting the round...\n");
    isRoundRunning = true;
    answers = 0;
    // Generate a random sequence of letters and send it to all players
    // Wait for players to send their words or until the time runs out
    letters = generateLetters();
    sendToAllPlaying(letters);
    stopTimer = false;

    // Countdown callback setup
    auto onTick = [&roundNum](int timeLeft) 
    {
        message = "Time until round " + std::to_string(roundNum) + " ends: " + std::to_string(timeLeft);
        sendToAll(message);
        printf("%s\n", message.c_str());
    };

    threads.emplace_back(countdown, roundTime, onTick);
    countdownOn = true;

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return gameEnd || !countdownOn || answers == inGame.size() || inGame.size() < 2; });
    }

    if (gameEnd) 
    {
        stopTimer = true;
        countdownOn = false;
        pthread_exit(nullptr);
    }
    else if (inGame.size() < 2)
    {
        message = "Not enough players to continue the game.";
        sendToAllPlaying(message);
        printf("%s\n", message.c_str());
    }
    
    if (!stopTimer)
    {
        stopTimer = true;
    }

    joinThreads();
    countdownOn = false;
    stopTimer = false;
    isRoundRunning = false;

    printf("Round ended!\n");
}

// Generates a string made from random letters
std::string generateLetters()
{
    std::string picked = "";
    std::string letters1 = "aeiouy";
    std::string letters2 = "bcdfghjklmnprstvwxz";
    printf("Generating letters...\n");
    // Pick at least two vowels
    std::uniform_int_distribution<int> dist(2, letters1.length());
    int vowels = dist(gen);
    for (int i = 0; i < vowels; i++)
    {
        std::uniform_int_distribution<int> dist1(0, letters1.length()-1);
        int index = dist1(gen);
        picked += letters1.at(index);
        letters1.erase(index, 1);
    }
    int consonants = letterCount - vowels;
    for (int i = 0; i < consonants; i++)
    {
        std::uniform_int_distribution<int> dist2(0, letters2.length()-1);
        int index = dist2(gen);
        picked += letters2.at(index);
        letters2.erase(index, 1);
    }
    return picked;
}

void sendToAllPlaying(std::string message)
{
    message += '\n';
    for (auto player : players)
    {
        if (inGame.find(player.second) != inGame.end())
        {
            write(player.first, message.c_str(), message.size());
        }
    }
}

void sendToAll(std::string message)
{
    message += '\n';
    for (auto player : players)
    {
        write(player.first, message.c_str(), message.size());
    }
}

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

        // Resizable pollFds
        if (descrCount == maxDescrCount)
        {
            maxDescrCount *= 2;
            pollFds = (pollfd *)realloc(pollFds, maxDescrCount*sizeof(pollfd));
        }
        
        pollFds[descrCount].fd = clientFd;
        pollFds[descrCount].events = POLLIN|POLLRDHUP;
        descrCount++;
        
        printf("New player connected: %s on port %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

        // Get nickname from player
        threads.emplace_back(getNickname, clientFd, descrCount-1);
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
        // Player sent a word during current round
        if (isRoundRunning && inGame.find(players[clientFd]) != inGame.end())
        {
            handleInput(clientFd);
        }
    }
}

void getNickname(int clientFd, int descr)
{
    char buffer[SIZE];
    std::string nick;

    while(1)
    {
        // Disconnection or end of game
        if (pollFds[descr].fd != clientFd || gameEnd)
        {
            return;
        }
        memset(buffer, 0, SIZE);
        int r = read(clientFd, buffer, SIZE);
        // Disconnection also
        if (r <= 0)
        {
            return;
        }
        buffer[strlen(buffer)-1] = '\0';
        if (r > 0)
        {
            nick = buffer;
            // Nickname was already picked
            if (std::any_of(players.begin(), players.end(), [&](const auto& pair) { return pair.second == nick; }))
            {
                message = "Provided nickname is already in use. Try again.\n";
                write(clientFd, message.c_str(), message.size());
            }
            else
            {
                std::pair<int,std::string> player (clientFd, nick);
                players.insert(player);
                std::pair<std::string,int> score (nick, 0);
                scores.insert(score);
                message = nick + " connected";
                sendToAll(message);
                printf("%s\n", message.c_str());
                cv.notify_all();

                if (isGameRunning)
                {
                    message = "Waiting for the current game to end...\n";
                }
                else
                {
                    message = "Waiting for the game to start...\n";
                }
                write(clientFd, message.c_str(), message.size());
                return;
            }
        }
        
    }
    
}

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
        // First correct answer this round
        if (words.empty())
        {
            score += bonusPoints;
        }
        words.insert(word);
    }
    else
    {
        score = negativePoints;
    }

    scores[players[clientFd]] += score;
    answers += 1;
    cv.notify_all();
    message = players[clientFd] + " answered!";
    sendToAllPlaying(message);
    printf("%s answered: %s\n", players[clientFd].c_str(), word.c_str());
}

// Checks if the word provided by the player is a valid one
bool checkIfCorrectWord(std::string word)
{
    // Word not found in dictionary
    if (dictionary.find(word) == dictionary.end())
    {
        return false;
    }
    
    for (char letter : word)
    {
        // Word contains different letters than provided
        if (letters.find(letter) == std::string::npos)
        {
            return false;
        }
    }

    return true;
}

// Client disconnected
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
    std::string message;
    
    // Clear structures
    std::unordered_map<int,std::string>::const_iterator gotPlayer = players.find(clientFd);
    if (gotPlayer != players.end())
    {
        message = players[clientFd] + " disconnected";
        std::unordered_map<std::string, int>::const_iterator gotScore = scores.find(players[clientFd]);
        if (gotScore != scores.end())
        {
            scores.erase(players[clientFd]);
        }
        std::unordered_set<std::string>::const_iterator gotInGame = inGame.find(players[clientFd]);
        if (gotInGame != inGame.end())
        {
            inGame.erase(players[clientFd]);
        }
        players.erase(clientFd);
    }
    else
    {
        message = "Player disconnected";
    }
    cv.notify_all();
    sendToAll(message);
    printf("%s\n", message.c_str());
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
    bool clearable = true;
    for (std::thread & t : threads) 
    {
        if (t.joinable()) { t.join(); }
        else if (clearable) { clearable = false; }
    }
    if (gameEnd) { gameLoopT.join(); }

    if (clearable)
    {
        threads.clear();
    }
}

// Read dictionary file into unordered_set for fast look-up
void readDictionary(std::string path)
{
    std::ifstream file(path);

    if (!file.is_open()) 
    {
        perror("Failed to open dictionary file");
        exit(1);
    }

    std::string line;
    while (std::getline(file, line))
    {
        int endOfWord = line.find_first_of("/");
        std::string word = line.substr(0, endOfWord);
        dictionary.insert(word);
    }

    file.close();
}