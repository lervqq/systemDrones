#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <map>
#include <vector>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "sqlite/sqlite3.h"
#include <stdio.h>
#include <sstream>
#pragma comment(lib, "Ws2_32.lib")

std::vector<std::thread> threads;
std::mutex sendMutex;

class Drones {
private:
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	std::map<int, SOCKET> drns;
	const int port = 8080;
	const char* DBfile;
	SOCKET server_fd;
	sqlite3* db;
	char* err;
	int fail;
public:
	void stopDrone(int id);
	void addDrone(int id);
	void launchDrone(int id);
	void allDronesInfo();
	void droneInfo(int id);
	void stopServer();
	void startServer();
	bool statusCheck(int id);
	bool droneCheck(int id);
	int countWorkingDrones();
	Drones(const char* filename) : DBfile(filename) {
		WSADATA wsaData;
		int opt = 1;

		if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
			std::cerr << "WSA startup failed with error: " << WSAGetLastError() << "\n";
			exit(EXIT_FAILURE);
		}

		if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
			std::cerr << "Socket creation failed with error: " << WSAGetLastError() << "\n";
			WSACleanup();
			exit(EXIT_FAILURE);
		}

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(port);

		if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
			std::cerr << "setsockopt failed with error: " << WSAGetLastError() << "\n";
			closesocket(server_fd);
			WSACleanup();
			exit(EXIT_FAILURE);
		}

		if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
			std::cerr << "Bind failed with error: " << WSAGetLastError() << "\n";
			closesocket(server_fd);
			WSACleanup();
			exit(EXIT_FAILURE);
		}

		if (listen(server_fd, 3) == SOCKET_ERROR) {
			std::cerr << "Listen failed with error: " << WSAGetLastError() << "\n";
			closesocket(server_fd);
			WSACleanup();
			exit(EXIT_FAILURE);
		}

		fail = sqlite3_open(filename, &db);

		if (fail) {
			std::cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
			sqlite3_close(db);
			exit(EXIT_FAILURE);
		}

		fail = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS drones(ID INTEGER, Status TEXT, First REAL, Second REAL)",
			NULL, 0, &err);
		if (fail) {
			std::cerr << "SQL error: " << err << "\n";
			sqlite3_free(err);
			sqlite3_close(db);
			exit(EXIT_FAILURE);
		}

		const char* sql = "DELETE FROM drones";
		fail = sqlite3_exec(db, sql, NULL, NULL, &err);
		if (fail != SQLITE_OK) {
			sqlite3_free(err);
		}
		std::cout << "Succesful connection\n";
	}
};

static int callback(void* data, int argc, char** argv, char** azColName) {
	for (int i = 0; i < argc; i++) {
		std::cout << azColName[i] << ": " << (argv[i] ? argv[i] : "NULL") << "\n";
	}
	std::cout << "\n";
	return 0;
}

static int callback1(void* data, int argc, char** argv, char** azColName) {
	if (argc > 0) {
		std::string* str = static_cast<std::string*>(data);
		str->assign(argv[0]);
	}
	return 0;
}

static int callback2(void* data, int argc, char** argv, char** azColName) {
	*(int*)data = std::atoi(argv[0]);
	return 0;
}

static int callback3(void* data, int argc, char** argv, char** azColName) {
	int* count = static_cast<int*>(data);
	*count = std::atoi(argv[0]);
	return 0;
}

bool Drones::statusCheck(int id) {
	std::string sql = "SELECT Status FROM drones WHERE ID = " + std::to_string(id) + ";", status;
	fail = sqlite3_exec(db, sql.c_str(), callback1, &status, &err);
	if (fail != SQLITE_OK) {
		std::cerr << "SELECT STATUS error\n";
		sqlite3_free(err);
	}
	if (status == "Working")
		return 1;
	return 0;
}

bool Drones::droneCheck(int id) {
	int cnt = 0;
	std::string sql = "SELECT COUNT(*) FROM drones WHERE ID = " + std::to_string(id) + ";";
	fail = sqlite3_exec(db, sql.c_str(), callback2, &cnt, &err);
	if (fail != SQLITE_OK) {
		std::cerr << "Error DRONE CHECK\n";
		sqlite3_free(err);
	}
	return cnt == 0 ? 0 : 1;
}

int Drones::countWorkingDrones() {
	int count = 0;
	const char* sql = "SELECT COUNT(*) FROM drones WHERE Status = 'Working';";
	fail = sqlite3_exec(db, sql, callback3, &count, &err);
	if (fail != SQLITE_OK) {
		std::cerr << "Error COUNT\n";
		sqlite3_free(err);
	}
	return count;
}

void Drones::stopServer() {
	sqlite3_close(db);
	for (auto& t : threads)
		t.join();
}

void Drones::addDrone(int id) {
	threads.emplace_back([&, id]() {
		if (!droneCheck(id)) {
			std::cout << "Waiting connection...\n";
			SOCKET newDrone;
			if ((newDrone = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) == INVALID_SOCKET) {
				std::cerr << "Accept failed with error: " << WSAGetLastError() << "\n";
				closesocket(server_fd);
				WSACleanup();
				return;
			}

			drns[id] = newDrone;
			std::string sql("INSERT INTO drones(ID, Status, First, Second) VALUES(" + std::to_string(id) +
				", 'Free', " + std::to_string(360 - 360 / (countWorkingDrones() + 1)) + ", " + std::to_string(360) + ");");

			fail = sqlite3_exec(db, sql.c_str(), NULL, 0, &err);
			if (fail != SQLITE_OK) {
				std::cerr << "Error INSERT\n";
				sqlite3_free(err);
				return;
			}
			std::cout << "Drone successfully added\n";
		}
		else
			std::cout << "Drone's already been added\n";
		});
}


void Drones::launchDrone(int id) {
	threads.emplace_back([&, id]() {
		if (droneCheck(id) && statusCheck(id)) {
			std::cout << "Drone is busy now\n";
			return;
		}
		if (!droneCheck(id)) {
			std::cout << "Drone's not found\n";
			return;
		}

		std::string sql = "UPDATE drones SET Status = 'Working' WHERE ID = " + std::to_string(id) + ";";
		fail = sqlite3_exec(db, sql.c_str(), NULL, 0, &err);
		if (fail != SQLITE_OK) {
			std::cerr << "Error UPDATE\n";
			sqlite3_free(err);
			return;
		}

		double one = 360.0 / countWorkingDrones(), i = 0;
		for (const auto& drone : drns) {
			if (statusCheck(drone.first)) {
				std::string f = std::to_string(one * i), s = std::to_string(one * (i + 1));
				std::string sql2 = "UPDATE drones SET First = " + f + " WHERE ID = " + std::to_string(drone.first);
				std::string sql3 = "UPDATE drones SET Second = " + s + " WHERE ID = " + std::to_string(drone.first);

				fail = sqlite3_exec(db, sql2.c_str(), 0, 0, &err);
				if (fail != SQLITE_OK) {
					std::cerr << "Error UPDATE2\n";
					sqlite3_free(err);
				}

				fail = sqlite3_exec(db, sql3.c_str(), 0, 0, &err);
				if (fail != SQLITE_OK) {
					std::cerr << "Error UPDATE3\n";
					sqlite3_free(err);
				}
				{
					std::string message = std::to_string(drone.first) + " " + f + " " + s;
					std::lock_guard<std::mutex> lock(sendMutex);
					send(drns[drone.first], message.c_str(), strlen(message.c_str()), 0);
				}
				i++;
			}
		}
		printf("The drone %d is getting to work\n", id);
		});
}

void Drones::stopDrone(int id) {
	threads.emplace_back([&, id]() {
		if (!droneCheck(id)) {
			std::cout << "Drone's not found\n";
			return;
		}
		if (!statusCheck(id)) {
			std::cout << "Drone is free now\n";
			return;
		}
		std::string sql("UPDATE drones SET Status = 'Free' WHERE ID = " + std::to_string(id) + ";");
		std::string sql1("UPDATE drones SET First = '0' WHERE ID = " + std::to_string(id) + ";");
		std::string sql2("UPDATE drones SET Second = '0' WHERE ID = " + std::to_string(id) + ";");
		fail = sqlite3_exec(db, sql.c_str(), NULL, 0, &err);
		if (fail != SQLITE_OK) {
			std::cerr << "Error STOP\n";
			sqlite3_free(err);
			return;
		}

		fail = sqlite3_exec(db, sql1.c_str(), NULL, 0, &err);
		if (fail != SQLITE_OK) {
			std::cerr << "Error STOP\n";
			sqlite3_free(err);
			return;
		}

		fail = sqlite3_exec(db, sql2.c_str(), NULL, 0, &err);
		if (fail != SQLITE_OK) {
			std::cerr << "Error STOP\n";
			sqlite3_free(err);
			return;
		}
		{
			std::string message("STOP");
			std::lock_guard<std::mutex> lock(sendMutex);
			send(drns[id], message.c_str(), strlen(message.c_str()), 0);
		}
		printf("The drone %d has finished its work\n", id);
		if (countWorkingDrones() == 0)
			return;
		double one = 360.0 / countWorkingDrones(), i = 0;
		for (const auto& drone : drns) {
			if (statusCheck(drone.first)) {
				std::string f = std::to_string(one * (i)), s = std::to_string(one * (i + 1));
				std::string sql2 = "UPDATE drones SET First = " + f + " WHERE ID = " + std::to_string(drone.first);
				std::string sql3 = "UPDATE drones SET Second = " + s + " WHERE ID = " + std::to_string(drone.first);

				fail = sqlite3_exec(db, sql2.c_str(), 0, 0, &err);
				if (fail != SQLITE_OK) {
					std::cerr << "Error UPDATE2\n";
					sqlite3_free(err);
				}

				fail = sqlite3_exec(db, sql3.c_str(), 0, 0, &err);
				if (fail != SQLITE_OK) {
					std::cerr << "Error UPDATE3\n";
					sqlite3_free(err);
				}
				{
					std::string message = std::to_string(drone.first) + " " + f + " " + s;
					std::lock_guard<std::mutex> lock(sendMutex);
					send(drone.second, message.c_str(), strlen(message.c_str()), 0);
				}
				i++;
			}
		}

		});
}

void Drones::allDronesInfo() {
	std::string sql("SELECT * FROM drones;");
	fail = sqlite3_exec(db, sql.c_str(), callback, NULL, &err);
	if (fail != SQLITE_OK) {
		std::cerr << "Error INFO\n";
		sqlite3_free(err);
	}
}

void Drones::droneInfo(int id) {
	std::string sql("SELECT * FROM DRONES WHERE ID = " + std::to_string(id) + ";");
	fail = sqlite3_exec(db, sql.c_str(), callback, NULL, &err);
	if (fail != SQLITE_OK) {
		std::cerr << "Error INFO\n";
		sqlite3_free(err);
	}
}

void Drones::startServer() {
	puts("Add (ID)/Start (ID)/Stop (ID)/All Info/Drone Info (ID)/STOP SERVER\n");
	while (true) {
		std::string message;
		std::getline(std::cin, message);
		if (message.substr(0, 4) == "STOP") {
			stopServer();
			return;
		}
		if (message.substr(0, 3) == "Add") {
			std::string id;
			std::stringstream ssin(message);
			int i = 0;
			while (ssin.good() && i < 2) {
				ssin >> id;
				i++;
			}
			addDrone(std::stoi(id));
			continue;
		}
		if (message.substr(0, 5) == "Start") {
			std::string id;
			std::stringstream ssin(message);
			int i = 0;
			while (ssin.good() && i < 2) {
				ssin >> id;
				i++;
			}
			launchDrone(std::stoi(id));
			continue;
		}
		if (message.substr(0, 4) == "Stop") {
			std::string id;
			std::stringstream ssin(message);
			int i = 0;
			while (ssin.good() && i < 2) {
				ssin >> id;
				i++;
			}
			stopDrone(std::stoi(id));
			continue;
		}
		if (message.substr(0, 3) == "All") {
			if (countWorkingDrones()) std::cout << "DRONES INFO\n";
			allDronesInfo();
			continue;
		}
		if (message.substr(0, 5) == "Drone") {
			std::string id;
			std::stringstream ssin(message);
			int i = 0;
			while (ssin.good() && i < 3) {
				ssin >> id;
				i++;
			}
			droneInfo(std::stoi(id));
			continue;
		}
		std::cout << "Invalid input\n";
	}
}

int main() {
	Drones house1("house1.db");
	house1.startServer();
}