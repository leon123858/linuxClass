#include <iostream>
#include <atomic>
#include <thread>

#define THREAD_N 2
#define ELEMENT_N 2

using namespace std;
static atomic_int inserts = 0;
static atomic_int deletes = 0;
static thread_local  int local_tid = -1;
static atomic_uintptr_t global_tid = 0;
static inline int tid() {
	if (local_tid < 0) {
		local_tid = global_tid.fetch_add(1);
	}
	return local_tid;
}

class node {
public:
	node* next;
	int  value;
	node(int value) {
		this->value = value;
		next = nullptr;
		inserts.fetch_add(1);
	}
	node(int value, node* n) {
		// can not use in atomic
		this->value = value;
		next = n;
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
	ptrNode(node* n, ptrNode* next) {
		nodePtr = n;
		this->next = next;
	}
};

class retireList {
public:
	atomic<ptrNode*>  head, tail;
	retireList() {
		head = tail = nullptr;
	}

	ptrNode* getHead() {
		return head.load();
	}

	void pushNode(node& retireNode) {
		ptrNode* n = new ptrNode(&retireNode);
		n->next = head.load();
		while (!head.compare_exchange_weak(n->next, n));
		if (head == nullptr || tail == nullptr)
			tail = head = head.load();
	}

	void clearNodes() {
		ptrNode* delPtr;
		for (ptrNode* ptr = (ptrNode*)head; ptr;)
		{
			delPtr = ptr;
			ptr = ptr->next;
			delete delPtr;
		}
	}
};

class threadViewNode {
public:
	int tid;
	int status = 0;
	threadViewNode* next;
	int threadViewNow[3];
	threadViewNode(int tid) {
		this->tid = tid;
		next = nullptr;
		for (int i = 0; i < 3; i++) {
			threadViewNow[i] = -1;
		}
	}

	void pushNode(node* nodeT) {
		if (nodeT == nullptr) return;
		threadViewNow[status] = nodeT->value;
		if (++status > 2)
			status = 0;
	}

	void clearNode() {
		status = 0;
		for (int i = 0; i < 3; i++) {
			threadViewNow[i] = -1;
		}
	}
};

class tmpViewTable {
public:
	atomic <threadViewNode*> head, tail;
	tmpViewTable() {
		head = nullptr;
		for (int i = 0; i < THREAD_N; i++) {
			pushNode(i);
		}
	}

	threadViewNode* getHead() {
		return head.load();
	}

	void pushNode(int tid) {
		threadViewNode* n = new threadViewNode(tid);
		n->next = head.load();
		while (!head.compare_exchange_weak(n->next, n));
		if (head == nullptr || tail == nullptr)
			tail = head = head.load();
		//addThreadTmpView(tid, node);
	}

	void addThreadTmpView(int tid, node& node) {
		for (threadViewNode* ptr = head.load(); ptr; ptr = ptr->next) {
			if (ptr->tid == tid) {
				ptr->pushNode(&node);
				return;
			}
		}
	}

	void releaseNode(int tid) {
		for (threadViewNode* ptr = head.load(); ptr; ptr = ptr->next) {
			if (ptr->tid == tid) {
				for (int i = 0; i < 3; i++) {
					ptr->threadViewNow[i] = 0;
				}
				return;
			}
		}
	}
};

class linkList {
public:
	atomic <node*> head, tail;
	retireList retireList;
	tmpViewTable tmpViewTable;
	linkList() {
		head = tail = NULL;
	}

	node* getHead() {
		return head.load();
	}

	void pushNode(int value) {
		//tmpViewTable.addThreadTmpView(tid(), *head.load());
		node* n = new node(value);
		n->next = head.load();
		//tmpViewTable.addThreadTmpView(tid(), *head.load());
		while (!head.compare_exchange_weak(n->next, n));
		//tmpViewTable.releaseNode(tid());
		if (head == NULL || tail == NULL)
			tail = head = head.load();
		//retireNode();
	}

	void deleteNode(int value) {
		while (true)
		{
			for (node* ptr = head.load(); ptr; ptr = ptr->next)
			{
				if (ptr == (node*)(0xdddddddd) ) return;
				//tmpViewTable.addThreadTmpView(tid(), *ptr);
				if (ptr->value == value) {
					//cout << "delete-> " << value << endl;
					this->retireList.pushNode(*ptr);
					//tmpViewTable.releaseNode(tid());
					return;
				}
				if (ptr == tail.load()) break;
			}
		}
	}

	void retireNode() {
		cout << "retire" << endl;
		for (ptrNode* ptr = retireList.head; ptr != retireList.tail; ptr = ptr->next) {
			//if (ptr->nodePtr->value == (0xdddddddd) || ptr == nullptr) return;
			cout  << ptr->nodePtr->value << endl;
			bool isDelete = true;
			for (threadViewNode* threadPtr = tmpViewTable.getHead(); threadPtr != tmpViewTable.tail.load(); threadPtr = threadPtr->next) {
				for (int i = 0; i < 3; i++) {
					cout << "tid:" << threadPtr->tid << endl;
					if (threadPtr->threadViewNow[i] == ptr->nodePtr->value)
						isDelete = false;
				}
			}
			if (isDelete) {
				node* front = head.load();
				tmpViewTable.addThreadTmpView(tid(), *front);
				if (front == ptr->nodePtr) {
					head.store(front->next);
					delete front;
					tmpViewTable.releaseNode(tid());
					deletes.fetch_add(1);
					continue;
				}
				for (node* innerPtr = front->next; innerPtr; innerPtr = innerPtr->next) {
					tmpViewTable.addThreadTmpView(tid(), *innerPtr);
					if (innerPtr == ptr->nodePtr) {
						front->next = innerPtr->next;
						delete innerPtr;
						deletes.fetch_add(1);
						break;
					}
					front = front->next;
				}
				tmpViewTable.releaseNode(tid());
			}
		}
	}
};

static atomic <int> insertCount = 0;
static atomic <int> deleteCount = 0;

void insertNode(linkList* linkList) {
	for (int i = 0; i < ELEMENT_N; i++) {
		linkList->pushNode(insertCount.fetch_add(1));
	}
}

void deleteNode(linkList* linkList) {
	for (int i = 0; i < ELEMENT_N; i++) {
		linkList->deleteNode(deleteCount.fetch_add(1));
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
	testlinkList->retireNode();

	int count = 0;
	cout << "finally" << endl;
	for (node* ptr = testlinkList->getHead(); ptr; ptr = ptr->next) {
		cout << ptr->value << endl;
		count++;
	}

	cout << "inserts:" << atomic_load(&inserts) << "  count" << count << endl;
	cout << "delete: " << atomic_load(&deletes) << endl;
}
