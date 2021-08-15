#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>

#define forever while(true)
#define DEFAULT_ERROR_404 "404 Not Found"
#define DEFAULT_PORT 1337
#define REQUEST_BUFFER_SIZE 1024
#define FAIL_CODE -1

using namespace std;

class response {

};

class request {
public:
	enum {
		GET=0,
		POST=1
	};
	int messageLength;
	int requestType;
	char clientIP[16];
	u_short clientPort;
	string filePath;
	string body;
	request(sockaddr_in& setting, SOCKET& messageSocket) {
		inet_ntop(AF_INET, &setting.sin_addr, clientIP, sizeof(clientIP));
		clientPort = htons(setting.sin_port);
		char buffer[REQUEST_BUFFER_SIZE];
		messageLength = recv(messageSocket, buffer, sizeof(buffer), 0);
		string requestStr = buffer;
		// todo : get requestType, filePath , body from requestStr
	}
	~request() {

	}
private:

};

class httpServer {
public:
	struct sockaddr_in localSocketSetting, clientSocketSetting;
	SOCKET listenSocket, messageSocket;
	WSADATA windowsSocketData;
	void start() {
		thread_local int count = 0;
	listen_goto:
		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
			errorHandle("listen()");

		printf("Waiting for connection...\n");

		forever
		{
			int clientSocketSettingLength;
			clientSocketSettingLength = sizeof(clientSocketSetting);
			messageSocket = accept(listenSocket, (struct sockaddr*)&clientSocketSetting, &clientSocketSettingLength);

			if (messageSocket == INVALID_SOCKET || messageSocket == FAIL_CODE)
				errorHandle("accept()");
			/*char clientIP[16];
			inet_ntop(AF_INET, &clientSocketSetting.sin_addr, clientIP, sizeof(clientIP));
			printf("\n\n#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$#$ %d\n\n", ++count);
			printf("Connected to %s:%d\n", clientIP, htons(clientSocketSetting.sin_port));*/

			/*int messageLength;
			char buffer[REQUEST_BUFFER_SIZE];
			string requestStr;
			messageLength = recv(messageSocket, buffer, sizeof(buffer), 0);
			requestStr = buffer;
			cout << "Client requested : " << endl << requestStr << endl << "end Buffer" << endl;*/

			if (messageLength == 0)
				continue;

			int sent = sendResponse(messageSocket);

			closesocket(messageSocket);

			if (sent == 0)
				break;
			else if (sent == -1)
				goto listen_goto;
		}
	}
#pragma warning(disable: 26495)
	httpServer() {
		if (WSAStartup(MAKEWORD(2, 2), &windowsSocketData) == SOCKET_ERROR)
			errorHandle("WSAStartup()");

		// Fill in the address structure
		localSocketSetting.sin_family = AF_INET;
		localSocketSetting.sin_addr.s_addr = INADDR_ANY;
		localSocketSetting.sin_port = htons(DEFAULT_PORT);

		listenSocket = socket(AF_INET, SOCK_STREAM, 0);

		if (listenSocket == INVALID_SOCKET)
			errorHandle("socket()");

		if (bind(listenSocket, (struct sockaddr*)&localSocketSetting, sizeof(localSocketSetting)) == SOCKET_ERROR)
			errorHandle("bind()");
	}
	~httpServer() {
		WSACleanup();
	}

private:
	int sendResponse(SOCKET socket) {
		if (0) {
			send(socket, DEFAULT_ERROR_404, strlen(DEFAULT_ERROR_404), 0);
			return 1;
		}

		/*FILE* f = fopen(response->filepath, "rb");
		char buf[1024] = { 0 };
		int msg_len;

		if (!f) {
			send(socket, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n", 57, 0);
			return 1;
		}*/
		string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n";
		const char* chr = header.c_str();
		send(socket, chr, strlen(chr), 0);

		int result = 19;
		send(socket, "data data data test", result, 0);
		cout << "reponse end" << endl;
		return 1;
	}
	void errorHandle(string str) {
		cout << "error : " << str << endl;
		exit(FAIL_CODE);
	}
};




//struct sockaddr_in {
//    short int sin_family; /* 通信类型 */
//    unsigned short int sin_port; /* 端口 */
//    struct in_addr sin_addr; /* internet 地址 */
//    unsigned char sin_zeros[8]; /* 与sockaddr结构的长度相同 */
//}：
//
//struct in_addr {
//    unsigned long s_addr;
//}

int main(int argc, char** argv)
{
	httpServer server;
	server.start();
}
