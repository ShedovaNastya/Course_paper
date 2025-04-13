#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <limits>
#include <fcntl.h>
#include <vector>
#include <cstring>
#include <poll.h>

struct ServerConnection {
    int socket = -1;
    bool connected = false;
    int port = 0;
};

void set_nonblock(int fd, bool nonblock) {
    int flags = fcntl(fd, F_GETFL, 0);
    if(nonblock) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    } else {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
}

int main() {
    std::vector<ServerConnection> servers = {
        {-1, false, 8080},
        {-1, false, 8081}
    };

    std::string server_ip = "127.0.0.1";

    while(true) {
        std::cout << "\n=== Меню клиента ===" << std::endl;
        std::cout << "1. Подключиться к Серверу 1" << std::endl;
        std::cout << "2. Подключиться к Серверу 2" << std::endl;
        std::cout << "3. Подключиться к Обоим" << std::endl;
        std::cout << "4. Отключиться от Сервера 1" << std::endl;
        std::cout << "5. Отключиться от Сервера 2" << std::endl;
        std::cout << "6. Отключиться от Всех" << std::endl;
        std::cout << "7. Отправить запрос" << std::endl;
        std::cout << "8. Выход" << std::endl;
        std::cout << "Выберите опцию: ";
        
        int choice;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "⚠️ Неверный ввод!" << std::endl;
            continue;
        }
        std::cin.ignore();

        auto connect_to_server = [&](int server_idx) {
            if(servers[server_idx].connected) {
                std::cout << "❗️ Уже подключены к Серверу " << server_idx+1 << "!" << std::endl;
                return;
            }

            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                std::cout << "🚫 Ошибка создания сокета: " << strerror(errno) << std::endl;
                return;
            }

            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(servers[server_idx].port);
            inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

            set_nonblock(sock, true);

            int conn_result = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
            if(conn_result < 0 && errno == EINPROGRESS) {
                pollfd pfd;
                pfd.fd = sock;
                pfd.events = POLLOUT;
                
                int poll_res = poll(&pfd, 1, 2000);
                if(poll_res > 0) {
                    int err;
                    socklen_t len = sizeof(err);
                    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
                    
                    if(!err) {
                        servers[server_idx].socket = sock;
                        servers[server_idx].connected = true;
                        std::cout << "✅ Успешное подключение к Серверу " << server_idx+1 << "!" << std::endl;
                        set_nonblock(sock, false);
                        return;
                    }
                    else {
                        std::cout << "🚫 Ошибка подключения: " << strerror(err) << std::endl;
                    }
                }
                else if(poll_res == 0) {
                    std::cout << "⌛️ Таймаут подключения!" << std::endl;
                }
                else {
                    std::cout << "🚫 Ошибка poll: " << strerror(errno) << std::endl;
                }
            }
            else if(conn_result == 0) {
                servers[server_idx].socket = sock;
                servers[server_idx].connected = true;
                std::cout << "✅ Успешное подключение к Серверу " << server_idx+1 << "!" << std::endl;
                set_nonblock(sock, false);
                return;
            }

            std::cout << "🚫 Не удалось подключиться: " << strerror(errno) << std::endl;
            close(sock);
        };

        auto disconnect_server = [&](int server_idx) {
            if(!servers[server_idx].connected) {
                std::cout << "❗️ Нет подключения к Серверу " << server_idx+1 << "!" << std::endl;
                return;
            }
            
            shutdown(servers[server_idx].socket, SHUT_RDWR);
            close(servers[server_idx].socket);
            servers[server_idx].socket = -1;
            servers[server_idx].connected = false;
            std::cout << "🔌 Отключено от Сервера " << server_idx+1 << std::endl;
        };

        switch(choice) {
            case 1: connect_to_server(0); break;
            case 2: connect_to_server(1); break;
            case 3: { 
                connect_to_server(0); 
                connect_to_server(1); 
                break;
            }
            case 4: disconnect_server(0); break;
            case 5: disconnect_server(1); break;
            case 6: { 
                disconnect_server(0); 
                disconnect_server(1); 
                break;
            }
            case 7: {
                std::cout << "Выберите сервер (1/2): ";
                int server_choice;
                std::cin >> server_choice;
                std::cin.ignore();

                if(server_choice < 1 || server_choice > 2 || 
                   !servers[server_choice-1].connected) {
                    std::cout << "❌ Неверный выбор сервера!" << std::endl;
                    break;
                }

                std::cout << "Введите запрос: ";
                std::string request;
                std::getline(std::cin, request);

                int current_socket = servers[server_choice-1].socket;
                ssize_t sent = send(current_socket, request.c_str(), request.size(), MSG_NOSIGNAL);
                if(sent < 0) {
                    std::cout << "🚫 Ошибка отправки: " << strerror(errno) << std::endl;
                    break;
                }

                char buffer[4096] = {0};
                pollfd pfd;
                pfd.fd = current_socket;
                pfd.events = POLLIN;

                int poll_res = poll(&pfd, 1, 5000);
                if(poll_res > 0) {
                    if(pfd.revents & POLLIN) {
                        ssize_t bytes = recv(current_socket, buffer, sizeof(buffer), 0);
                        if(bytes > 0) {
                            std::cout << "📨 Ответ сервера " << server_choice << ":\n" 
                                      << std::string(buffer, bytes) << std::endl;
                        }
                        else {
                            std::cout << "🚫 Соединение закрыто сервером" << std::endl;
                            disconnect_server(server_choice-1);
                        }
                    }
                }
                else if(poll_res == 0) {
                    std::cout << "⌛️ Таймаут ожидания ответа!" << std::endl;
                }
                else {
                    std::cout << "🚫 Ошибка poll: " << strerror(errno) << std::endl;
                }
                break;
            }
            case 8: {
                for(auto& s : servers) {
                    if(s.connected) {
                        shutdown(s.socket, SHUT_RDWR);
                        close(s.socket);
                    }
                }
                std::cout << "👋 До свидания!" << std::endl;
                return 0;
            }
            default: std::cout << "⚠️ Неверный выбор!" << std::endl;
        }
    }
}