#include <iostream>
#include <atomic>
#include <thread>
#define THREAD_N 100
#define ELEMENT_N 1000
#define INSERT_N THREAD_N*ELEMENT_N

using namespace std;
static atomic_int inserts = 0;
static atomic_int deletes = 0;

/**
		*  標記 (marked) 的原理 :
		*  由於記憶體對齊的原因(朝4的倍數對),
		*  所以指標的第一個位元不包含資訊, 只要在使用時設為0 即可。
		*  標記的目的在於使我們知道一個資料結構是不是已被刪除。
		*  又不想用到多餘的空間,
		*  於是隨意找了資料結構中的一個指標, 其用不到的位元做標記。
*/

// true : 被marked了
static inline bool is_marked_ref(void* i)
{
	return (bool)((uintptr_t)i & 0x1L);
}

static inline void* get_unmarked_ref(void* w)
{
	return (void*)((uintptr_t)w & ~0x1L);
}

static inline void* get_marked_ref(void* w)
{
	return (void*)((uintptr_t)w | 0x1L);
}


class node {
public:
	atomic <node*> next;
	int  value;

	node(int value) {
		this->value = value;
		next.store(nullptr);
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
	atomic <node*> head, tail;
	retireList retireList;
	struct pointerPair
	{
		node* leftNode;
		node* rightNode;
	};

	linkList() {
		head = new node(0);
		tail = new node(INSERT_N + 1);
		head.load()->next.store(tail);
	}

	node* getHead() {
		return head.load();
	}

	pointerPair getLeftNodeAndRightNode(int value) {
		pointerPair pointerPair;
		node* left_node_next = nullptr;
	search_again:
		do {
			node* tmpNode = head.load(); // 當前節點
			node* tmpNode_next = tmpNode->next; // 用來查看當前節點有沒有被標註
			// 查找目標節點, 且將其設為右節點。
			do {
				// 當前節點沒被標註, 左節點前進到當前節點。
				if (!is_marked_ref(tmpNode_next)) {
					pointerPair.leftNode = tmpNode;
					left_node_next = tmpNode_next;
				}
				// 當前節點往下走一個節點。
				tmpNode = (node*)get_unmarked_ref(tmpNode_next);
				// 走到尾退出, leftNode為最後一個沒被marked的節點。
				if (tmpNode == tail) break;
				// 得到新節點的標註
				tmpNode_next = tmpNode->next;
				// 當節點被標註刪除或是數字還沒到就loop again
			} while (is_marked_ref(tmpNode_next) || (tmpNode ->value < value));
			// 右節點為第一個數字大於目標且未被標註刪除的節點 , leftNode為最後一個沒被marked的節點。
			pointerPair.rightNode = tmpNode;
			// 如果左右節點相鄰表示不用繞過, 可以回傳目標節點。
			if (left_node_next == pointerPair.rightNode)
				// 回傳前檢查結果是否錯誤 (在查找的過程目標被刪除)
#pragma warning(disable:6011)
				if ((pointerPair.rightNode != tail) && is_marked_ref(pointerPair.rightNode->next))
					goto search_again;
				else
					return pointerPair;
			// 繞過被標註節點
			if (pointerPair.leftNode->next.compare_exchange_weak(left_node_next, pointerPair.rightNode))
				if ((pointerPair.rightNode != tail) && is_marked_ref(pointerPair.rightNode->next))
					goto search_again;
				else
					return pointerPair; 
		} while (true); 
	}

	void insertNode(int value) {
		node* new_node = new node(value);
		node* right_node = nullptr;
		node* left_node = nullptr;
		do {
			pointerPair pointerPair = getLeftNodeAndRightNode(value);
			left_node = pointerPair.leftNode;
			right_node = pointerPair.rightNode;

			if ((right_node != tail.load()) && (right_node->value == value))
				return; // 節點已存在
			new_node->next.store(right_node);
			if (left_node->next.compare_exchange_weak(right_node, new_node))
				return; // CAS插入
		} while (true);
	}

	void deleteNode(int value) {
		node* right_node = nullptr;
		node* right_node_next = nullptr;
		node* left_node = nullptr;
		do {
			pointerPair pointerPair = getLeftNodeAndRightNode(value);
			left_node = pointerPair.leftNode;
			right_node = pointerPair.rightNode;

			if ((right_node == tail) || (right_node->value != value))
				continue; // do not find target value
			right_node_next = right_node->next.load();
			if (!is_marked_ref(right_node_next))
				if (right_node->next.compare_exchange_weak(right_node_next, (node*)get_marked_ref(right_node_next)))
					break; // 當右節點的next沒被標記, 就標記右節點的next
		} while (true);
		// 試著使左結點跳過右節點往下走, 失敗就再跑一次搜尋, 其可以跳過被標記節點。
		if (!left_node->next.compare_exchange_weak(right_node, right_node_next))
			getLeftNodeAndRightNode(value);
		retireList.pushNode(right_node);
		deletes.fetch_add(1);
		return;
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

static atomic <int> insertCount = 1;
static atomic <int> deleteCount = 1;

void insertNode(linkList* linkList) {
	for (int i = 0; i < ELEMENT_N; i++) {
		linkList->insertNode(insertCount.fetch_add(1));
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