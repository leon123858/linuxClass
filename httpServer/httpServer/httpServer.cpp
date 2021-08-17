#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <time.h>   

#define forever while(true)
#define DEFAULT_ERROR_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
#define DEFAULT_PORT 1337
#define REQUEST_BUFFER_SIZE 1024
#define FAIL_CODE -1
#define BUFFER_N 1024
#define THREADPOOL_SIZE 10

using namespace std;

class request {
public:
	enum {
		GET = 0,
		POST = 1,
		UNKNOW = -1
	};
	int messageLength;
	int requestType;
	char clientIP[16];
	u_short clientPort;
	string filePath;
	string body;
	request(sockaddr_in& setting, SOCKET& messageSocket) {
		setIpandPort(setting);
		char buffer[REQUEST_BUFFER_SIZE];
		messageLength = recv(messageSocket, buffer, sizeof(buffer), 0);
		if (messageLength == 0) return;
		destructStr(buffer);
	}
#pragma warning(disable: 26495)
	request(SOCKET& messageSocket) {
		char buffer[REQUEST_BUFFER_SIZE];
		messageLength = recv(messageSocket, buffer, sizeof(buffer), 0);
		if (messageLength == 0) return;
		destructStr(buffer);
	}
private:
	void setIpandPort(sockaddr_in& setting) {
		inet_ntop(AF_INET, &setting.sin_addr, clientIP, sizeof(clientIP));
		clientPort = htons(setting.sin_port);
	}
	void destructStr(char* buffer) {
		string requestStr = buffer;
		int index = 0;
		index = setRequestType(requestStr, index);
		index = setFilePath(requestStr, index);
		index = setBody(requestStr, index);
	}
	int setRequestType(string& str, int start) {
		int firstSpaceIndex = str.find(" ", start);
		if (firstSpaceIndex == -1)
			throw;
		string type = str.substr(0, firstSpaceIndex);
		if (type == "GET")
			requestType = GET;
		else if (type == "POST")
			requestType = POST;
		else
			requestType = UNKNOW;
		return firstSpaceIndex;
	}
	int setFilePath(string& str, int start) {
		int nextSpaceIndex = str.find(" ", start + 1);
		filePath = str.substr(start + 1, nextSpaceIndex - start - 1);
		return nextSpaceIndex;
	}
	int setBody(string& str, int start) {
		//skip
		return 1;
	}
};

class response {
public:
	vector<string> GetRouters;
	vector<string> PostRouters;
	response() {
		// add routers
		GetRouters.push_back("/getFile/");
	}
	int runGetRoute(SOCKET& socket, string route) {
		for (auto i : GetRouters) {
			if (0 == route.find(i)) {
				matchStr = i;
				goto routers;
			}
		}
		return notFound(socket);
	routers:
		if (matchStr == "/getFile/")
			return getFile(socket, route);
		else
			return notFound(socket);
	}
private:
	string matchStr;
	int getFile(SOCKET& socket, string& route) {
		string header = "HTTP/1.1 200 OK\r\nContent-Type: " + getContentType(route) + "; charset=UTF-8\r\n\r\n";
		string path = route.substr(matchStr.length(), route.length());
		string curFilePath = getCurFilePath();
		string goalFilePth = curFilePath + path;
		const char* headerChr = header.c_str();
		char buffer[1024] = { 0 };
		FILE* file;
		int readLength;
		int sendResult;
		errno_t err = fopen_s(&file, path.c_str(), "rb");
		if (err != 0) return notFound(socket);
		sendResult = send(socket, headerChr, strlen(headerChr), 0);
		if (sendResult == SOCKET_ERROR) {
			printf("Error sending header, reconnecting...\n");
			closesocket(socket);
			return -1;
		}
		while ((readLength = fread(buffer, 1, 1024, file)) > 0) {
			sendResult = send(socket, buffer, readLength, 0);
			if (sendResult == SOCKET_ERROR) {
				printf("Error sending body, reconnecting...\n");
				closesocket(socket);
				return -1;
			}
			else if (readLength <= 0)
			{
				printf("Read File Error, End The Program\n");
				closesocket(socket);
				return readLength;
			}
		}
		fclose(file);
		closesocket(socket);
		return 1;
	}
	int notFound(SOCKET& socket) {
		send(socket, DEFAULT_ERROR_404, strlen(DEFAULT_ERROR_404), 0);
		closesocket(socket);
		return 1;
	}
	string getCurFilePath() {
		char filename[1024] = { 0 };
#pragma warning(disable: 6031)
		_getcwd(filename, 1024);
		if (filename[strlen(filename)] != '\\')
			strcat_s(filename, "\\");
		return filename;
	}
	string getContentType(string route)
	{
		int index = route.find_last_of('.');
		if (index == -1)
			return "*/*";
		string extension = route.substr(index);
		if (extension == ".html")
			return "text/html";
		else if (extension == ".ico")
			return "image/webp";
		else if (extension == ".css")
			return "text/css";
		else if (extension == ".jpg")
			return "image/jpeg";
		else if (extension == ".js")
			return "text/javascript";
		return "*/*";
	}
};

class httpServer {
public:
	struct sockaddr_in localSocketSetting;
	SOCKET listenSocket;
	WSADATA windowsSocketData;
	vector<thread> threadPool;
	vector<vector<SOCKET>> tasksList;
	void start(int poolNumber) {
		printf("Start.......\n");
	rebuild:
		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
			errorHandle("listen");
		forever
		{
			SOCKET messageSocket = getSocket(poolNumber);
			if (messageSocket == INVALID_SOCKET || messageSocket == FAIL_CODE)
				goto rebuild;
			request req = request(messageSocket);
			cout << endl << "thread " << poolNumber << " : " << req.filePath;
			if (req.messageLength == 0)
				continue;
			int sentResult = responseClient(req, messageSocket);
			if (sentResult == 0)
				break;
			else if (sentResult == FAIL_CODE)
				goto rebuild;
		}
	}
	void accepter() {
		forever{
			struct sockaddr_in clientSocketSetting;
			int clientSocketSettingLength;
			clientSocketSettingLength = sizeof(clientSocketSetting);
			SOCKET socket = accept(listenSocket, (struct sockaddr*)&clientSocketSetting, &clientSocketSettingLength);
			int poolNumber = rand() % THREADPOOL_SIZE;
			tasksList[poolNumber].push_back(socket);
		}
	}
	void go() {
		thread worker = thread(&httpServer::accepter, this);
		for (int i = 0; i < THREADPOOL_SIZE; i++)
			threadPool.push_back(thread(&httpServer::start, this, i));
		for (auto& i : threadPool)
			i.join();
		worker.join();
	}
#pragma warning(disable: 26495)
	httpServer() {
		for (int i = 0; i < THREADPOOL_SIZE; i++) {
			vector<SOCKET> tasks;
			tasksList.push_back(tasks);
		}
		if (WSAStartup(MAKEWORD(2, 2), &windowsSocketData) == SOCKET_ERROR)
			errorHandle("WSAStartup");
		// Fill in the address structure
		localSocketSetting.sin_family = AF_INET;
		localSocketSetting.sin_addr.s_addr = INADDR_ANY;
		localSocketSetting.sin_port = htons(DEFAULT_PORT);
		listenSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (listenSocket == INVALID_SOCKET)
			errorHandle("socket");
		if (bind(listenSocket, (struct sockaddr*)&localSocketSetting, sizeof(localSocketSetting)) == SOCKET_ERROR)
			errorHandle("bind");
	}
	~httpServer() {
		WSACleanup();
	}
private:
	void errorHandle(string str) {
		cout << "error : " << str << endl;
		exit(FAIL_CODE);
	}
	int responseClient(request req, SOCKET& messageSocket) {
		response response;
		if (req.requestType == req.GET)
			return response.runGetRoute(messageSocket, req.filePath);
		else
			return response.runGetRoute(messageSocket, "/404");
	}
	SOCKET getSocket(int poolNumber) {
		forever{
			if (tasksList[poolNumber].size() == 0)
				continue;
			SOCKET node = tasksList[poolNumber].back();
			tasksList[poolNumber].pop_back();
			return node;
		}
	}
};

int main(int argc, char** argv)
{
	srand(time(NULL));
	httpServer server;
	server.go();
}
