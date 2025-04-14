#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <fcntl.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <system_error>
#include <fstream>
#include <sstream>
#include <cstring>
#include <arpa/inet.h> 
#include <ctime> // Для inet_pton

std::mutex mtx;

const char* LOG_FIFO = "/tmp/server1_log.fifo";

void send_log(const std::string& event_type, const std::string& data) {
    int fd = open(LOG_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;
    
    std::string message = event_type + "|" + data;
    write(fd, message.c_str(), message.size());
    close(fd);
}

struct MouseInfo {
    std::string name;
    std::string devnode;
    int buttons;
};

std::vector<MouseInfo> detect_mice() {
    std::vector<MouseInfo> mice;
    const std::string input_dir = "/dev/input/";

    DIR *dir = opendir(input_dir.c_str());
    if (!dir) {
        throw std::system_error(errno, std::system_category(), "opendir failed");
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_CHR) continue;

        std::string path = input_dir + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct libevdev *dev = nullptr;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc != 0) {
            close(fd);
            continue;
        }

        if (libevdev_has_event_type(dev, EV_REL) &&
            libevdev_has_event_code(dev, EV_KEY, BTN_LEFT)) {
            
            MouseInfo info;
            info.name = libevdev_get_name(dev);
            info.devnode = path;
            info.buttons = 0;

            for (int code = BTN_LEFT; code <= BTN_TASK; ++code) {
                if (libevdev_has_event_code(dev, EV_KEY, code)) {
                    info.buttons++;
                }
            }

            mice.push_back(info);
        }

        libevdev_free(dev);
        close(fd);
    }
    closedir(dir);

    return mice;
}

size_t get_free_memory() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    size_t free_mem = 0;
    while (getline(meminfo, line)) {
        if (line.find("MemFree") != std::string::npos) {
            std::istringstream iss(line);
            iss >> line >> free_mem;
            free_mem *= 1024;
            break;
        }
    }
    return free_mem;
}


std::string get_current_time() {
    std::time_t now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(buf);
}

void handle_client(int client_socket) {
    while(true) {
        char buffer[1024] = {0};
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
        
        if(bytes_read <= 0) break;

        std::string command(buffer, bytes_read);
        std::string response;
        std::string timestamp = "[" + get_current_time() + "] ";
        
        send_log("CLIENT_CONNECT", "New client connected");
        
        if (command == "MEMORY") {
            size_t free_mem = get_free_memory();
            response = timestamp + "Free memory: " + std::to_string(free_mem) + " bytes\n";
            send_log("COMMAND", "Received command: " + command);
        }
        else if (command == "MOUSE_KEYS") {
            try {
                auto mice = detect_mice();
                if (mice.empty()) {
                    response = timestamp + "Mouse devices: 0\n";
                } else {
                    response = timestamp + "Mouse devices: " + std::to_string(mice.size()) + "\n";
                    for (const auto& mouse : mice) {
                        response += timestamp + mouse.name + ": " + std::to_string(mouse.buttons) + "\n";
                        send_log("COMMAND", "Received command: " + command);
                    }
                }
            } 
            catch (const std::exception& e) {
                response = timestamp + "Error: " + std::string(e.what()) + "\n";
            }
        }
        else if (command == "EXIT") {
            response = timestamp + "Connection closed";
            send_log("EXIT", "Received command: " + response);
            send(client_socket, response.c_str(), response.size(), 0);
            break;
        }
        else {
            response = timestamp + "Invalid command\n";
        }

        if(send(client_socket, response.c_str(), response.size(), 0) <= 0) break;
    }
    close(client_socket);
}



int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0) {
        std::cerr << "Socket creation error: " << strerror(errno) << std::endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind error: " << strerror(errno) << std::endl;
        close(server_socket);
        return 1;
    }

    if(listen(server_socket, 5) < 0) {
        std::cerr << "Listen error: " << strerror(errno) << std::endl;
        close(server_socket);
        return 1;
    }
    
    std::cout << "Server 1 started on port 8080" << std::endl;
    send_log("SERVER_START", "Server 1 started on port 8080");

    while (true) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if(client_socket < 0) {
            std::cerr << "Accept error: " << strerror(errno) << std::endl;
            continue;
        }
        
        std::thread(handle_client, client_socket).detach();
    }

    close(server_socket);
    return 0;
}