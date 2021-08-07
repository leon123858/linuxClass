#include <iostream>
#include <atomic>
#include <thread>
#define THREAD_N 100
#define ELEMENT_N 10000

using namespace std;
static atomic_int inserts = 0;
static atomic_int deletes = 0;

class node {
public:
	node* next;
	int  value;

	node(int value) {
		this->value = value;
		next = nullptr;
		inserts.fetch_add(1);
	}
};


class ptrNode {
public:
	node* nodePtr;
	ptrNode* next;
	ptrNode(node* n) {
		nodePtr = n;
		next = nullptr;
	}
};


class retireList 
{
	atomic <ptrNode*> head;
public:

	ptrNode* getHead() {
		return head.load();
	}

	void pushNode(node* n) {
		ptrNode* newNode = new ptrNode(n);
		do {
			newNode->next = head.load();
		} while (!head.compare_exchange_weak(newNode->next, newNode));
	}

	void deleteNodes() {
		ptrNode* delPtr;
		for (ptrNode* ptr = getHead(); ptr;)
		{
			delPtr = ptr;
			ptr = ptr->next;
			delete delPtr;
		}
		cout << "clean retireList" << endl;
	}

	retireList() {
		head.store(nullptr);
	}
	~retireList() {
		deleteNodes();
	}
};

class linkList {
public:
	atomic <node*> head;
	retireList retireList;
	linkList() {
		head = NULL;
	}

	node* getHead() {
		return head.load();
	}

	void pushNode(int value) {
		node* n = new node(value);
		do {
			n->next = head.load();
		}while (!head.compare_exchange_weak(n->next, n));
		/*
		*  比較 head == n -> next 就令 head = n
		*  若 head != n , 改令 n -> next = head 然後在做下一輪, 做到一樣為止
		*/
	}

	void popNode() {
	start:
		node* curHead = head.load();
		do {
			if (curHead == nullptr)
				goto start; //一定要刪到, 不放棄
		} while (!head.compare_exchange_weak(curHead, curHead->next));
		retireList.pushNode(curHead);
		deletes.fetch_add(1);
	}

	~linkList() {
		node* delPtr;
		for (node* ptr = this->getHead(); ptr;)
		{
			delPtr = ptr;
			ptr = ptr->next;
			delete delPtr;
		}
	}
};

static atomic <int> insertCount = 0;

void insertNode(linkList* linkList) {
	for (int i = 0; i < ELEMENT_N; i++) {
		linkList->pushNode(insertCount.fetch_add(1));
	}
}

void deleteNode(linkList* linkList) {
	for (int i = 0; i < ELEMENT_N; i++) {
		linkList->popNode();
	}
}

int main()
{
	std::cout << "Start!\n";
	linkList* testlinkList = new linkList();

	thread threads_insert[THREAD_N];
	thread threads_delete[THREAD_N];

	for (int i = 0; i < THREAD_N; i++) {
		threads_insert[i] = thread(insertNode, testlinkList);
	}
	for (int i = 0; i < THREAD_N; i++) {
		threads_delete[i] = thread(deleteNode, testlinkList);
	}
	for (int i = 0; i < THREAD_N; i++) {
		threads_insert[i].join();
		threads_delete[i].join();
	}

	int count = 0;
	cout << "finally" << endl;
	for (node* ptr = testlinkList->getHead(); ptr; ptr = ptr->next) {
		//cout << ptr->value << endl;
		count++;
	}
	delete testlinkList;

	cout << "inserts:" << atomic_load(&inserts) << "  count" << count << endl;
	cout << "delete: " << atomic_load(&deletes) << endl;
}