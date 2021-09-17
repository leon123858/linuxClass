#include <iostream>
#include <string>
#include <thread>
#include <algorithm>
#include <queue>
#include <vector>
#include<ctime>
#include <windows.h>
#include <assert.h>


using namespace std;


struct io_information {
	int type = 0;
	ULONG_PTR ptr = 0;
	HANDLE handle = NULL;
};

class threadPool
{
public:
	threadPool() {

	}
	~threadPool() {

	}
	int pushMission() {

	}
private:
	void worker() {

	}
	int reSendMission() {

	}
};

struct timer_Node {
	unsigned int timer = 0;
	string value;
};

struct comparator
{
	bool operator()(timer_Node a, timer_Node b)
	{
		return a.timer > b.timer;
	}
};

class eventLoop
{
public:
	priority_queue<timer_Node, vector<timer_Node>, comparator> timer_heap;
	thread loop;
	eventLoop(HANDLE IOCP_queue) {
		loop = thread(&eventLoop::main_loop, this, IOCP_queue);
	}
	~eventLoop() {
	}
	void push_timer_Node(timer_Node tn) {
		timer_heap.push(tn);
	}
private:
	void main_loop(HANDLE IOCP_queue) {
		while (true)
		{
			timer_stage();
			polling_stage(IOCP_queue);
		}
	}
	void timer_stage() {
		if (timer_heap.empty())
			return;
		time_t now_time;
		now_time = time(NULL);
		while (true)
		{
			timer_Node tn = timer_heap.top();
			if (tn.timer > now_time) {
				cout << tn.value << endl;
				break;
			}
			cout << tn.value << endl;
			timer_heap.pop();
			if (timer_heap.empty())
				break;
		}
	}
	void polling_stage(HANDLE IOCP_queue) {
		ULONG_PTR* ipCompletionKey;
		LPOVERLAPPED ipOverlap;
		DWORD ipNumberOfBytes;
		io_information* io_Info;
		int result;
		while (true)
		{
			// get IO status
			result = GetQueuedCompletionStatus(
				IOCP_queue,
				&ipNumberOfBytes,
				(PULONG_PTR)&ipCompletionKey,
				&ipOverlap,
				0);
			if (result == 0)
				continue;
			io_Info = (io_information*)ipCompletionKey;
			if (io_Info->type == 1) {
				uint8_t* chars = (uint8_t*)io_Info->ptr;
				string str = string((char*)chars);
				cout << str.substr(0, ipNumberOfBytes) << endl;
				CloseHandle(io_Info->handle);
			}
			
		}
	}
};

void console_directly(string str) {
	cout << str << endl;
}

void console_timer(string str, unsigned int delay, eventLoop& EL) {
	timer_Node tn;
	time_t now_time;
	now_time = time(NULL);
	tn.value = str;
#pragma warning(disable: 4244)
	tn.timer = now_time + delay;
	EL.push_timer_Node(tn);
}


void console_file(string path, HANDLE IOCP_queue) {
	HANDLE file, file_socket;
	OVERLAPPED* overlapped;
	uint8_t* readDataBuf;
	io_information* io_info = new io_information();
	// set memory
	overlapped = new OVERLAPPED();
	readDataBuf = new uint8_t[1024];
	io_info->type = 1;
	io_info->ptr = (ULONG_PTR)readDataBuf;
	// string to LPCWSTR
	std::wstring stemp = std::wstring(path.begin(), path.end());
	LPCWSTR sw = stemp.c_str();
	// IOCP bind
	file = CreateFile(sw, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	assert(file != INVALID_HANDLE_VALUE);
	io_info->handle = file;
	file_socket = CreateIoCompletionPort(file, IOCP_queue, (ULONG_PTR)io_info, 0);
	assert(file_socket != NULL);
	// AIO read file
#pragma warning(disable: 6031)
	ReadFile(file, readDataBuf, 1024, NULL, overlapped);
}

void console_encryption(string str) {

}

int main()
{
	std::cout << "Start!\n";
	HANDLE IOCP_queue;
	IOCP_queue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	assert(IOCP_queue != NULL);
	eventLoop* EventLoop = new eventLoop(IOCP_queue);
	console_file("test.txt", IOCP_queue);
	console_timer("test1", 5, *EventLoop);
	EventLoop->loop.join();
}
