#include <iostream>
#include <atomic>
#include <thread>

#define THREAD_N 100
#define ELEMENT_N 100

using namespace std;
static atomic_int inserts = 0;
static atomic_int deletes = 0;
static thread_local  int local_tid = -1;
static atomic_uintptr_t global_tid = 0;
static atomic_int retireNow = 0;

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

	ptrNode* deleteNode(ptrNode* ptr) {
		ptrNode* front = head.load();
		if (front == ptr) {
			head.store(front->next);
			ptr = ptr->next;
			delete front;
			return ptr;
		}
		for (ptrNode* innerPtr = front->next; innerPtr; innerPtr = innerPtr->next) {
			if (innerPtr == ptr) {
				front->next = innerPtr->next;
				ptr = ptr->next;
				delete innerPtr;
				break;
			}
			front = front->next;
		}
		return ptr;
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
		//cout << "tmp:" << nodeT->value;
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
				ptr->clearNode();
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
		tmpViewTable.addThreadTmpView(tid(), *head.load());
		node* n = new node(value);
		n->next = head.load();
		tmpViewTable.addThreadTmpView(tid(), *head.load());
		while (!head.compare_exchange_weak(n->next, n));
		tmpViewTable.releaseNode(tid());
		if (head == NULL || tail == NULL)
			tail = head = head.load();
	}

	void deleteNode(int value) {
		retireNode();
		while (true)
		{
			for (node* ptr = head.load(); ptr; ptr = ptr->next)
			{
				tmpViewTable.addThreadTmpView(tid(), *ptr);
				if (ptr->value == value) {
					this->retireList.pushNode(*ptr);
					tmpViewTable.releaseNode(tid());
					return;
				}
				if (ptr == tail.load()) break;
			}
		}
	}

	void retireNode() {
		if (retireNow.fetch_add(1) != 0)
			return;
		for (ptrNode* ptr = retireList.head; ptr ; ptr = ptr->next) {
		skip_loop_plus:
			if (ptr == nullptr) {
				retireNow.store(0);
				return;
			}
			bool isDelete = true;
			for (threadViewNode* threadPtr = tmpViewTable.getHead(); threadPtr; threadPtr = threadPtr->next) {
				for (int i = 0; i < 3; i++) {
					if (threadPtr->threadViewNow[i] == ptr->nodePtr->value)
						isDelete = false;
				}
			}
			if (isDelete) {
				node* front = head.load();
				if (front == ptr->nodePtr) {
					cout << "delete:" << ptr->nodePtr->value << endl;
					ptr =  retireList.deleteNode(ptr);
					head.store(front->next);
					delete front;
					deletes.fetch_add(1);
					goto skip_loop_plus;
				}
				for (node* innerPtr = front->next; innerPtr; innerPtr = innerPtr->next) {
					if (innerPtr == ptr->nodePtr) {
						cout << "delete:" << ptr->nodePtr->value << endl;
						ptr = retireList.deleteNode(ptr);
						front->next = innerPtr->next;
						delete innerPtr;
						deletes.fetch_add(1);
						goto skip_loop_plus;
					}
					front = front->next;
				}
			}
		}
		retireNow.store(0);
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
