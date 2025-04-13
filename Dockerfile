FROM gcc:12.2.0

# устанавливаю зависимости
RUN apt-get update && \
    apt-get install -y \
    build-essential \
    g++ \
    libc6-dev \
    netbase

# компиляция исходный код клиента в контейнер
COPY client.cpp /app/client.cpp

# компилирую клиент
WORKDIR /app
RUN g++ -std=c++17 -o client client.cpp

# точкa входа
CMD ["./client"]
