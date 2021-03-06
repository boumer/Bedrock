#include "BedrockTester.h"
#include <sys/wait.h>

// Define static vars.
string BedrockTester::defaultDBFile; // Unused, exists for backwards compatibility.
string BedrockTester::defaultServerAddr; // Unused, exists for backwards compatibility.
SData BedrockTester::globalArgs;
mutex BedrockTester::_testersMutex;
set<BedrockTester*> BedrockTester::_testers;
list<string> BedrockTester::locations = {
    "../bedrock",
    "../../bedrock"
};

// Set to 2 or more for duplicated requests.
int BedrockTester::mockRequestMode = 0;

string BedrockTester::getTempFileName(string prefix) {
    string templateStr = "/tmp/" + prefix + "bedrocktest_XXXXXX.db";
    char buffer[templateStr.size() + 1];
    strcpy(buffer, templateStr.c_str());
    int filedes = mkstemps(buffer, 3);
    close(filedes);
    return buffer;
}

string BedrockTester::getServerName() {
    for (auto location : locations) {
        if (SFileExists(location)) {
            return location;
        }
    }
    return "";
}

void BedrockTester::stopAll() {
    lock_guard<decltype(_testersMutex)> lock(_testersMutex);
    for (auto p : _testers) {
        p->stopServer();
    }
}

BedrockTester::BedrockTester(const map<string, string>& args, const list<string>& queries, bool startImmediately, bool keepFilesWhenFinished) :
    BedrockTester(0, args, queries, startImmediately, keepFilesWhenFinished)
{ }

BedrockTester::BedrockTester(int threadID, const map<string, string>& args, const list<string>& queries, bool startImmediately, bool keepFilesWhenFinished)
  : _keepFilesWhenFinished(keepFilesWhenFinished)
{
    {
        lock_guard<decltype(_testersMutex)> lock(_testersMutex);
        _testers.insert(this);
    }

    // Set the ports.
    int serverPort = 8989 + threadID;
    int hostPort = 9889 + threadID;
    int controlPort = 19999 + threadID;

    // Set these values from the arguments if provided, or the defaults if not.
    try {
        _dbName = args.at("-db");
    } catch (...) {
        _dbName = getTempFileName();
    }
    try {
        _serverAddr = args.at("-serverHost");
    } catch (...) {
        _serverAddr = "127.0.0.1:" + to_string(serverPort);
    }

    map <string, string> defaultArgs = {
        {"-db",               _dbName},
        {"-serverHost",       _serverAddr},
        {"-nodeName",         "bedrock_test"},
        {"-nodeHost",         "localhost:" + to_string(hostPort)},
        {"-controlPort",      "localhost:" + to_string(controlPort)},
        {"-priority",         "200"},
        {"-plugins",          "db"},
        {"-workerThreads",    "8"},
        {"-maxJournalSize",   "25000"},
        {"-v",                ""},
        {"-quorumCheckpoint", "50"},
        {"-enableMultiWrite", "true"},
        {"-cacheSize",        "1000"},
    };

    // Set defaults.
    for (auto& row : defaultArgs) {
        _args[row.first] = row.second;
    }

    // And replace with anything specified.
    for (auto& row : args) {
        _args[row.first] = row.second;
    }
    
    _controlAddr = _args["-controlPort"];

    // If the DB file doesn't exist, create it.
    if (!SFileExists(_dbName)) {
        SFileSave(_dbName, "");
    }

    // Run any supplied queries on the DB.
    // We don't use SQLite here, because we specifically want to avoid dealing with journal tables.
    if (queries.size()) {
        sqlite3* _db;
        sqlite3_initialize();
        sqlite3_open_v2(_dbName.c_str(), &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL);
        for (string query : queries) {
            int error = sqlite3_exec(_db, query.c_str(), 0, 0, 0);
            if (error) {
                cout << "Init Query: " << query << ", FAILED. Error: " << error << endl;
            }
        }
        SASSERT(!sqlite3_close(_db));
    }
    if (startImmediately) {
        startServer();
    }
}

BedrockTester::~BedrockTester() {
    if (_db) {
        delete _db;
    }
    if (_serverPID) {
        stopServer();
    }
    if (!_keepFilesWhenFinished) {
        SFileExists(_dbName.c_str()) && unlink(_dbName.c_str());
        SFileExists((_dbName + "-shm").c_str()) && unlink((_dbName + "-shm").c_str());
        SFileExists((_dbName + "-wal").c_str()) && unlink((_dbName + "-wal").c_str());
    }
    lock_guard<decltype(_testersMutex)> lock(_testersMutex);
    _testers.erase(this);
}

string BedrockTester::startServer(bool dontWait) {
    string serverName = getServerName();
    int childPID = fork();
    if (!childPID) {
        // We are the child!
        list<string> args;
        // First arg is path to file.
        args.push_front(getServerName());
        for (auto& row : _args) {
            args.push_back(row.first);
            if (!row.second.empty()) {
                args.push_back(row.second);
            }
        }

        // Convert our c++ strings to old-school C strings for exec.
        char* cargs[args.size() + 1];
        int count = 0;
        for(string arg : args) {
            char* newstr = (char*)malloc(arg.size() + 1);
            strcpy(newstr, arg.c_str());
            cargs[count] = newstr;
            count++;
        }
        cargs[count] = 0;

        // And then start the new server!
        execvp(serverName.c_str(), cargs);
    } else {
        // We'll kill this later.
        _serverPID = childPID;

        // Wait for the server to start up.
        // TODO: Make this not take so long, particularly in Travis. This probably really requires making the server
        // come up faster, not a change in how we wait for it, though it would be nice if we could do something
        // besides this 100ms polling.
        int count = 0;
        bool needSocket = true;
        while (1) {
            count++;
            // Give up after a minute. This will fail the remainder of the test, but won't hang indefinitely.
            if (count > 60 * 10) {
                break;
            }
            if (needSocket) {
                int socket = 0;
                socket = S_socket(dontWait ? _controlAddr : _serverAddr, true, false, true);
                if (socket == -1) {
                    usleep(100000); // 0.1 seconds.
                    continue;
                }
                ::shutdown(socket, SHUT_RDWR);
                ::close(socket);
                needSocket = false;
            }

            // We've successfully opened a socket, so let's try and send a command.
            SData status("Status");
            auto result = executeWaitMultipleData({status}, 1, dontWait);
            if (result[0].methodLine == "200 OK") {
                return result[0].content;
            }
            // This will happen if the server's not up yet. We'll just try again.
            usleep(100000); // 0.1 seconds.
            continue;
        }
    }
    return "";
}

void BedrockTester::stopServer(int signal) {
    if (_serverPID) {
        kill(_serverPID, signal);
        int status;
        waitpid(_serverPID, &status, 0);
        _serverPID = 0;
    }
}

string BedrockTester::executeWaitVerifyContent(SData request, const string& expectedResult, bool control) {
    auto results = executeWaitMultipleData({request}, 1, control);
    if (results.size() == 0) {
        STHROW("No result.");
    }
    if (results[0].methodLine == "") {
        STHROW("Empty response");
    }
    if (!SStartsWith(results[0].methodLine, expectedResult)) {
        STHROW("Expected " + expectedResult + ", but got: " + results[0].methodLine);
    }
    return results[0].content;
}

STable BedrockTester::executeWaitVerifyContentTable(SData request, const string& expectedResult) {
    string result = executeWaitVerifyContent(request, expectedResult);
    return SParseJSONObject(result);
}

vector<SData> BedrockTester::executeWaitMultipleData(vector<SData> requests, int connections, bool control, bool returnOnDisconnect) {
    // Synchronize dequeuing requests, and saving results.
    recursive_mutex listLock;

    // Our results go here.
    vector<SData> results;
    results.resize(requests.size());

    // This is the next index of `requests` that needs processing.
    int currentIndex = 0;

    // This is the list of threads that we'll use for each connection.
    list <thread> threads;

    // Spawn a thread for each connection.
    for (int i = 0; i < connections; i++) {
        threads.emplace_back([&, i](){

            // Create a socket.
            int socket = 0;

            int socketSendCount = 0;
            while (true) {
                if (socket <= 0) {
                    socket = S_socket((control ? _controlAddr : _serverAddr), true, false, true);
                    socketSendCount = 0;
                }
                size_t myIndex = 0;
                SData myRequest;
                {
                    SAUTOLOCK(listLock);
                    myIndex = currentIndex;
                    currentIndex++;
                    if (myIndex >= requests.size()) {
                        // No more requests to process.
                        break;
                    } else {
                        myRequest = requests[myIndex];
                    }
                }

                size_t count = 1;
                if (mockRequestMode > 1) {
                    count = mockRequestMode;
                }

                // If the socket failed to open, don't bother trying with it.
                if (socket == -1) {
                    SAUTOLOCK(listLock);
                    SData responseData("002 Socket Failed");
                    results[myIndex] = move(responseData);
                    if (returnOnDisconnect) {
                        return;
                    }
                    continue;
                }

                for (size_t mockCount = 0; mockCount < count; mockCount++) {
                    if (mockCount) {
                        myRequest["mockRequest"] = "true";
                    }

                    // Send some stuff on our socket.
                    string sendBuffer = myRequest.serialize();
                    while (sendBuffer.size()) {
                        bool result = S_sendconsume(socket, sendBuffer);
                        socketSendCount++;
                        if (!result) {
                            cout << "Failed to send! Probably disconnected." << endl;
                            break;
                        }
                    }

                    // Receive some stuff on our socket.
                    string recvBuffer = "";
                    string methodLine, content;
                    STable headers;
                    int timeouts = 0;
                    int count = 0;
                    while (!SParseHTTP(recvBuffer.c_str(), recvBuffer.size(), methodLine, headers, content)) {
                        // Poll the socket, so we get a timeout.
                        pollfd readSock;
                        readSock.fd = socket;
                        readSock.events = POLLIN | POLLHUP;
                        readSock.revents = 0;

                        // wait for a second...
                        poll(&readSock, 1, 1000);
                        count++;
                        if (readSock.revents & POLLIN) {
                            bool result = S_recvappend(socket, recvBuffer);
                            if (!result) {
                                sockaddr_in addr = {0};
                                socklen_t size = 0;
                                getsockname(socket, (sockaddr*)&addr, &size);
                                ::shutdown(socket, SHUT_RDWR);
                                ::close(socket);
                                socket = -1;
                                break;
                            }
                        } else if (readSock.revents & POLLHUP) {
                            ::shutdown(socket, SHUT_RDWR);
                            ::close(socket);
                            socket = -1;
                            break;
                        } else {
                            timeouts++;
                            if (timeouts == 600) {
                                break;
                            }
                        }
                    }

                    // Lock to avoid log lines writing over each other.
                    {
                        SAUTOLOCK(listLock);
                        if (timeouts == 600) {
                            SData responseData = myRequest;
                            responseData.nameValueMap = headers;
                            responseData.methodLine = "000 Timeout";
                            responseData.content = content;
                            if (!mockCount) {
                                results[myIndex] = move(responseData);
                            }
                        } else {
                            // Ok, done, let's lock again and insert this in the results.
                            SData responseData;
                            responseData.nameValueMap = headers;
                            responseData.methodLine = methodLine;
                            responseData.content = content;

                            if (!mockCount) {
                                results[myIndex] = move(responseData);
                            }

                            if (headers["Connection"] == "close") {
                                ::shutdown(socket, SHUT_RDWR);
                                ::close(socket);
                                socket = 0;
                                break;
                            }
                        }
                    }
                }
            }
            if (socket != -1) {
                ::shutdown(socket, SHUT_RDWR);
                ::close(socket);
            }
        });
    }

    // Wait for our threads to finish.
    for (thread& t : threads) {
        t.join();
    }

    // All done!
    return results;
}

SQLite& BedrockTester::getSQLiteDB()
{
    if (!_db) {
        _db = new SQLite(_dbName, 1000000, 0, 3000000, -1, 0);
    }
    return *_db;
}

string BedrockTester::readDB(const string& query)
{
    return getSQLiteDB().read(query);
}

bool BedrockTester::readDB(const string& query, SQResult& result)
{
    return getSQLiteDB().read(query, result);
}
