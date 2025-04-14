#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <dirent.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <sstream>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <csignal>
#include <atomic>
#include <cstring>
#include <arpa/inet.h> 
#include <ctime>

std::mutex mtx;
std::atomic<bool> running{true};
std::atomic<int> active_connections{0};

Display* display = nullptr;
Window terminal_window = 0;

const char* LOG_FIFO = "/tmp/server2_log.fifo";

void send_log(const std::string& event_type, const std::string& data) {
    int fd = open(LOG_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd == -1) return;

    std::string message = event_type + "|" + data;
    write(fd, message.c_str(), message.size());
    close(fd);
}

// Helper function to check if a string is numeric
bool is_numeric(const char* str) {
    if (!str || !*str) return false;
    for (int i = 0; str[i]; i++) {
        if (str[i] < '0' || str[i] > '9') return false;
    }
    return true;
}

// Function to count all threads in the system
int count_system_threads() {
    int total_threads = 0;
    DIR* proc_dir = opendir("/proc");
    
    if (!proc_dir) {
        std::cerr << "Ошибка открытия директории /proc" << std::endl;
        return -1;
    }
    
    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        if (entry->d_type != DT_DIR || !is_numeric(entry->d_name)) {
            continue;
        }
        
        std::string status_path = std::string("/proc/") + entry->d_name + "/status";
        std::ifstream status_file(status_path);
        if (!status_file) continue;
        
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.find("Threads:") == 0) {
                int thread_count = 0;
                std::istringstream iss(line.substr(9));
                iss >> thread_count;
                total_threads += thread_count;
                break;
            }
        }
    }
    
    closedir(proc_dir);
    return total_threads;
}

void signal_handler(int) {
    running = false;
}

class ConnectionCounter {
public:
    ConnectionCounter() { active_connections++; }
    ~ConnectionCounter() { active_connections--; }
};

bool init_x11_connection() {
    display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "Ошибка подключения к X Server" << std::endl;
        return false;
    }

    // Try to get window from environment variable
    const char* env_window = std::getenv("WINDOWID");
    if (env_window) {
        terminal_window = std::stoul(env_window, nullptr, 0);
        return true;
    }

    // Search window by PID
    Atom net_wm_pid = XInternAtom(display, "_NET_WM_PID", False);
    Window root = DefaultRootWindow(display);
    Window* children;
    unsigned num_children;
    
    if (!XQueryTree(display, root, &root, &root, &children, &num_children)) {
        return false;
    }

    pid_t pid = getpid();
    for (unsigned i = 0; i < num_children; ++i) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* data = nullptr;

        if (XGetWindowProperty(display, children[i], net_wm_pid,
                             0, 1, False, XA_CARDINAL,
                             &actual_type, &actual_format,
                             &nitems, &bytes_after, &data) == Success) {
            if (actual_type == XA_CARDINAL && actual_format == 32 && nitems == 1) {
                if (*(pid_t*)data == pid) {
                    terminal_window = children[i];
                    XFree(data);
                    break;
                }
            }
            XFree(data);
        }
    }
    XFree(children);

    return terminal_window != 0;
}

bool move_window(int x, int y) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!display || !terminal_window) return false;
    
    XWindowChanges changes;
    changes.x = x;
    changes.y = y;
    
    XConfigureWindow(display, terminal_window, CWX | CWY, &changes);
    XFlush(display);
    return true;
}


// Добавить функцию получения времени
std::string get_current_time() {
    std::time_t now = std::time(nullptr);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(buf);
}

// Модифицированная функция handle_client для сервера 2
void handle_client(int client_socket) {
    ConnectionCounter counter;
    char buffer[1024];

    try {
        while (running) {
            ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) break;

            std::string request(buffer, bytes_read);
            std::string response;
            std::string timestamp = "[" + get_current_time() + "] ";

            send_log("CLIENT_CONNECT", "New client connected");

            if (request == "THREAD_COUNT") {
                int total_threads = count_system_threads();
                response = timestamp + "Всего потоков в системе: " + std::to_string(total_threads);
                send_log("COMMAND", "Received command: " + request);
            }
            else if (request.rfind("MOVE_WINDOW", 0) == 0) {
                size_t space_pos = request.find(' ');
                if (space_pos != std::string::npos) {
                    std::istringstream iss(request.substr(space_pos + 1));
                    int x, y;
                    if (iss >> x >> y) {
                        bool success = move_window(x, y);
                        response = timestamp + (success ? 
                            "OK Окно перемещено в " + std::to_string(x) + "x" + std::to_string(y) :
                            "ERROR Ошибка перемещения");
                           
                    } else {
                        response = timestamp + "ERROR Неверный формат координат";
                    }
                } else {
                    response = timestamp + "ERROR Неверный формат команды";
                }
            }
            else if (request == "EXIT") {
                response = timestamp + " Соединение закрыто";
                send_log("EXIT", "Received command: " + request);
                send(client_socket, response.c_str(), response.size(), 0);
                break;
            }
            else {
                response = timestamp + "ERROR Неизвестная команда";
            }

            send(client_socket, response.c_str(), response.size(), 0);
            send_log("COMMAND", "Received command:"+ response);
        }
    }
    catch(const std::exception& e) {
        std::cerr << "Ошибка в клиенте: " << e.what() << std::endl;
    }
    close(client_socket);
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!init_x11_connection()) {
        std::cerr << "Не удалось подключиться к X Server или найти окно терминала" << std::endl;
        send_log("SERVER_ERROR", "X11 connection failed");
        return 1;
    }

    int lock_fd = open("/tmp/server2.lock", O_CREAT | O_RDWR, 0666);
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "Сервер уже запущен!" << std::endl;
        return 1;
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Ошибка создания сокета: " << strerror(errno) << std::endl;
        return 1;
    }

    // Делаем сокет неблокирующим
    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8081);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Ошибка bind: " << strerror(errno) << std::endl;
        send_log("SERVER_ERROR", "Bind failed");
        return 1;
    }

    listen(server_socket, 5);
    std::cout << "Сервер 2 запущен на порту 8081" << std::endl;
    send_log("SERVER_START", "Server 2 started on port 8081");

    while (running) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
        
        if (client_socket < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Ждем 100ms перед следующей проверкой
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            std::cerr << "Ошибка accept: " << strerror(errno) << std::endl;
            continue;
        }
        
        std::thread([client_socket](){ 
            handle_client(client_socket); 
        }).detach();
    }

    // Корректное завершение
    send_log("SERVER_STOP", "Server 2 stopped");
    std::cout << "Сервер 2 остановлен" << std::endl;
    
    if (display) XCloseDisplay(display);
    close(server_socket);
    close(lock_fd);
    
    return 0;
}