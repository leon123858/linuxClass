#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <queue>
#include <vector>
#include<ctime>
#include <windows.h>
#include <assert.h>

#define TEST_LOOP_N 10;
#define FILE_BUFFER_SIZE 1024;
#define THREAD_POOL_N 2
#define ENCRYPTION_OFFSET 3;

enum AIO_EVENT_TYPE
{
	LOCAL_FILE_READ = 1,
	STRING_ENCRYPTION = 2,
	STRING_DECRYPTION = 3
};

using namespace std;

atomic<int>line_count = 0;

struct io_information {
	int type = 0;
	ULONG_PTR ptr = 0;
	HANDLE handle = NULL;
	DWORD length = 0;
};

class node {
public:
	node* next;
	io_information* value;
	node(io_information* value) {
		this->value = value;
		next = nullptr;
	}
};

class stack {
public:
	atomic<node*> head;
	stack() {
		head = NULL;
	}
	node* getHead() {
		return head.load();
	}
	void pushNode(io_information* value) {
		node* n = new node(value);
		do {
			n->next = head.load();
		} while (!head.compare_exchange_weak(n->next, n));
	}
	io_information* popNode() {
		node* curHead = head.load();
		do {
			if (curHead == nullptr)
				return nullptr;
		} while (!head.compare_exchange_weak(curHead, curHead->next));
		return curHead->value;
	}
	~stack() {
		node* delPtr;
		for (node* ptr = this->getHead(); ptr;)
		{
			delPtr = ptr;
			ptr = ptr->next;
			delete delPtr;
		}
	}
};

class threadPool
{
public:
	stack Stack;
	thread threads[THREAD_POOL_N];
	bool thread_run = true;
	threadPool(HANDLE IOCP_queue) {
		eventQueue = IOCP_queue;
		for (int i = 0; i < THREAD_POOL_N; i++)
			threads[i] = thread(&threadPool::worker, this);
	}
	void pushMission(io_information* io_Info) {
		Stack.pushNode(io_Info);
	}
private:
	HANDLE eventQueue = NULL;
	void worker() {
		io_information* io_Info = nullptr;
		while (thread_run)
		{
			io_Info = Stack.popNode();
			if (!io_Info)
				continue;
			if (io_Info->type == AIO_EVENT_TYPE::STRING_ENCRYPTION)
				encrypt_string(io_Info);
			else if (io_Info->type == AIO_EVENT_TYPE::STRING_DECRYPTION)
				decrypt_string(io_Info);
			reSendMission(io_Info);
		}
	}
	void encrypt_string(io_information* io_Info) {
		string* strPtr = (string*)io_Info->ptr;
		string str = *strPtr;
		for (unsigned int i = 0; (i < str.size() && str[i] != '\0'); i++)
			str[i] = str[i] + ENCRYPTION_OFFSET; //the key for encryption is 3 that is added to ASCII value
		*strPtr = str;
	}
	void decrypt_string(io_information* io_Info) {
		string* strPtr = (string*)io_Info->ptr;
		string str = *strPtr;
		for (unsigned int i = 0; (i < str.size() && str[i] != '\0'); i++)
			str[i] = str[i] - ENCRYPTION_OFFSET;
		*strPtr = str;
	}
	void reSendMission(io_information* io_Info) {
		// send AIO event to eventLoop by IOCP
		PostQueuedCompletionStatus(eventQueue, 0, (ULONG_PTR)io_Info, NULL);
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
	bool thread_run = true;
	eventLoop(HANDLE IOCP_queue) {
		loop = thread(&eventLoop::main_loop, this, IOCP_queue);
	}
	void push_timer_Node(timer_Node tn) {
		timer_heap.push(tn);
	}
private:
	vector <io_information*> pending_list;
	vector <io_information*> endgame_list;
	void main_loop(HANDLE IOCP_queue) {
		while (thread_run)
		{
			timer_stage();
			pending_stage();
			polling_stage(IOCP_queue);
			endGame_stage();
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
			if (tn.timer > now_time)
				return;
			cout << tn.value << endl;
			timer_heap.pop();
			line_count.fetch_add(-1);
			if (timer_heap.empty())
				return;
		}
	}
	void polling_stage(HANDLE IOCP_queue) {
		ULONG_PTR* ipCompletionKey;
		LPOVERLAPPED ipOverlap;
		DWORD ipNumberOfBytes;
		io_information* io_Info;
		int result;
		// get IO status
		result = GetQueuedCompletionStatus(
			IOCP_queue,
			&ipNumberOfBytes,
			(PULONG_PTR)&ipCompletionKey,
			&ipOverlap,
			0);
		if (result == 0)
			return;
		// put io result in  pending_list
		io_Info = (io_information*)ipCompletionKey;
		io_Info->length = ipNumberOfBytes;
		pending_list.push_back(io_Info);
		return;
	}
	void pending_stage() {
		// solve pending_list and push them into endgame_list
		for (auto i : pending_list) {
			switch (i->type)
			{
			case AIO_EVENT_TYPE::LOCAL_FILE_READ:
				console_string(i, i->length, AIO_EVENT_TYPE::LOCAL_FILE_READ);
				line_count.fetch_add(-1);
				break;
			case AIO_EVENT_TYPE::STRING_ENCRYPTION:
				console_string(i, 0, AIO_EVENT_TYPE::STRING_ENCRYPTION);
				line_count.fetch_add(-1);
				break;
			case  AIO_EVENT_TYPE::STRING_DECRYPTION:
				console_string(i, 0, AIO_EVENT_TYPE::STRING_DECRYPTION);
				line_count.fetch_add(-1);
				break;
			default:
				assert(NULL != NULL);
				break;
			}
			endgame_list.push_back(i);
		}
		pending_list.clear();
	}
	void endGame_stage() {
		// release resource or close IO event
		for (auto i : endgame_list) {
			switch (i->type)
			{
			case AIO_EVENT_TYPE::LOCAL_FILE_READ:
				CloseHandle(i->handle);
				free((uint8_t*)(i->ptr));
				break;
			case AIO_EVENT_TYPE::STRING_ENCRYPTION:
				free((string*)(i->ptr));
				break;
			case  AIO_EVENT_TYPE::STRING_DECRYPTION:
				free((string*)(i->ptr));
				break;
			default:
				assert(NULL != NULL);
				break;
			}
			delete i;
		}
		endgame_list.clear();
	}
	void console_string(io_information* io_Info, DWORD setLength, int commentMode) {
		if (commentMode == 2) {
			string* str = (string*)io_Info->ptr;
			cout << "encryption result : " << *str << endl;
		}
		else if (commentMode == 3) {
			string* str = (string*)io_Info->ptr;
			cout << "decryption result : " << *str << endl;
		}
		else
		{
			uint8_t* chars = (uint8_t*)io_Info->ptr;
			string str = string((char*)chars);
			cout << str.substr(0, setLength) << endl;
		}
	}
};

// 5 methods below can be used in this enviroment
// console string with no AIO
void console_directly(string str) {
	cout << str << endl;
}
// console with delay
void console_timer(string str, unsigned int delay, eventLoop& EL) {
	timer_Node tn;
	time_t now_time;
	now_time = time(NULL);
	tn.value = str;
#pragma warning(disable: 4244)
	tn.timer = now_time + delay;
	EL.push_timer_Node(tn);
	line_count.fetch_add(1);
}
// console text in file (ex:txt)
void console_file(string path, HANDLE IOCP_queue) {
	HANDLE file, file_socket;
	OVERLAPPED* overlapped;
	uint8_t* readDataBuf;
	io_information* io_info = new io_information();
	const int bufferSize = FILE_BUFFER_SIZE;
	// set memory
	overlapped = new OVERLAPPED();
	readDataBuf = new uint8_t[bufferSize];
	io_info->type = AIO_EVENT_TYPE::LOCAL_FILE_READ;
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
	ReadFile(file, readDataBuf, bufferSize, NULL, overlapped);
	line_count.fetch_add(1);
}
// console string by simple encryption
void console_encryption(string str, threadPool* TP) {
	io_information* ioInfo = new io_information();
	string* strPtr = new string(str);
	ioInfo->type = AIO_EVENT_TYPE::STRING_ENCRYPTION;
	ioInfo->ptr = (ULONG_PTR)strPtr;
	TP->pushMission(ioInfo);
	line_count.fetch_add(1);
}
// console string by simple decryption
void console_decryption(string str, threadPool* TP) {
	io_information* ioInfo = new io_information();
	string* strPtr = new string(str);
	ioInfo->type = AIO_EVENT_TYPE::STRING_DECRYPTION;
	ioInfo->ptr = (ULONG_PTR)strPtr;
	TP->pushMission(ioInfo);
	line_count.fetch_add(1);
}

int main()
{
	std::cout << "Start!\n";
	// set enviroment
	HANDLE IOCP_queue;
	IOCP_queue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	assert(IOCP_queue != NULL);
	eventLoop* EventLoop = new eventLoop(IOCP_queue);
	threadPool* ThreadPool = new threadPool(IOCP_queue);
	// code start (在這裡, 你可以隨意使用上面的 5 個方法)
	console_file("test.txt", IOCP_queue);
	const int loopTime = TEST_LOOP_N;
	for (int i = 0; i < loopTime; i++) {
		console_timer("test", i, *EventLoop);
		console_directly("test_directly");
		console_encryption("test", ThreadPool);
		console_decryption("whvw", ThreadPool);
	}
	console_file("test2.txt", IOCP_queue);
	for (int i = 0; i < loopTime; i++) {
		console_directly("test_directly_2");
		console_encryption("test2", ThreadPool);
		console_decryption("whvw5", ThreadPool);
	}
	// code end
	// close program
	while (line_count.load() != 0) this_thread::sleep_for(std::chrono::seconds(5)); // sleep
	EventLoop->thread_run = false;
	ThreadPool->thread_run = false;
	return 0;
}
