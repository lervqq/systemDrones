#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <mutex>
#pragma comment(lib, "Ws2_32.lib")


class Drone {
private:
	SOCKET sock;
	struct sockaddr_in serv_addr;
public:
	void run();
	Drone(const char* address, int port) {
		WSAData wsaData;

		if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
			std::cerr << "WSA startup failed with error: " << WSAGetLastError() << "\n";
			exit(EXIT_FAILURE);
		}

		if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
			std::cerr << "Socket creation failed" << "\n";
			WSACleanup();
			exit(EXIT_FAILURE);
		}

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(port);

		if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
			std::cerr << "Invalid address: Address not supported" << "\n";
			closesocket(sock);
			WSACleanup();
			exit(EXIT_FAILURE);
		}

		if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
			std::cerr << "Connection failed" << "\n";
			closesocket(sock);
			WSACleanup();
			exit(EXIT_FAILURE);
		}
		std::cout << "ON\n";
	}
};

void Drone::run() {
	while (true) {
		char buffer[1024] = { 0 };
		int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
		if (bytes_received == SOCKET_ERROR) {
			std::cerr << "Server is down\n" << WSAGetLastError() << "\n";
			return;
		}
		else {
			char* new_buffer = new char[bytes_received + 1];
			memcpy(new_buffer, buffer, bytes_received);
			new_buffer[bytes_received] = { 0 };
			std::string stop;
			std::stringstream s(new_buffer);
			s >> stop;
			if (stop == "STOP") {
				std::cout << "STOP\n";
				continue;
			}
			std::stringstream ssin(new_buffer);
			std::string id, first, second;
			int i = 0;

			while (ssin.good() && i < 3) {
				ssin >> (i == 0 ? id : (i == 1 ? first : second));
				i++;
			}
			printf("Drone %d is controlling the area from %f to %f\n", std::stoi(id), std::stod(first), std::stod(second));
		}
	}
}



int main() {
	Drone newDrone("127.0.0.1", 8080);
	newDrone.run();
	system("pause");
}