#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstring>
#include <thread>
#include <mutex>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

std::mutex log_mutex;
const char* SERVER1_FIFO = "/tmp/server1_log.fifo";
const char* SERVER2_FIFO = "/tmp/server2_log.fifo";

void create_fifo(const char* path) {
    if (mkfifo(path, 0666) == -1 && errno != EEXIST) {
        std::cerr << "Error creating FIFO: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
}

void log_event(const std::string& server_id, const std::string& event_type, const std::string& data) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream log_file("logs/" + server_id + ".log", std::ios::app);
    if (!log_file) {
        std::cerr << "Error opening log file for " << server_id << std::endl;
        return;
    }
    std::time_t now = std::time(nullptr);
    char timestamp[20];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    log_file << "[" << timestamp << "] [" << event_type << "] " << data << std::endl;
}

void handle_fifo(const char* fifo_path, const std::string& server_id) {
    char buffer[1024];
    
    while (true) {
        int fd = open(fifo_path, O_RDONLY);
        if (fd == -1) {
            std::cerr << "Error opening FIFO: " << strerror(errno) << std::endl;
            sleep(1);
            continue;
        }

        ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            std::string message(buffer, bytes_read);
            size_t delim = message.find('|');
            if (delim != std::string::npos) {
                std::string event_type = message.substr(0, delim);
                std::string data = message.substr(delim + 1);
                log_event(server_id, event_type, data);
            }
        }
        close(fd);
    }
}

int main() {
    mkdir("logs", 0777);
    create_fifo(SERVER1_FIFO);
    create_fifo(SERVER2_FIFO);

    std::thread server1_thread(handle_fifo, SERVER1_FIFO, "server1");
    std::thread server2_thread(handle_fifo, SERVER2_FIFO, "server2");

    std::cout << "Log server started" << std::endl;
    
    server1_thread.join();
    server2_thread.join();

    return 0;
}
