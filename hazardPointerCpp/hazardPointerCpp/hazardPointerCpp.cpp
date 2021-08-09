#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>

#define TMP_WIDTH_N 3
#define THREAD_N 100
#define ELEMENT_N 100
#define INSERT_N THREAD_N*ELEMENT_N
#define TID_UNKNOWN -1
using namespace std;
static atomic_int inserts = 0;
static atomic_int deletes = 0;

static atomic_int nodeDeleteCount = 0;

static thread_local int tid_v = TID_UNKNOWN;
static atomic_int_fast32_t tid_v_base = ATOMIC_VAR_INIT(0);

static atomic_int k = 0;

static inline int tid(void)
{
	if (tid_v == TID_UNKNOWN) {
		tid_v = atomic_fetch_add(&tid_v_base, 1);
	}
	return tid_v;
}

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

	~node()
	{
		nodeDeleteCount.fetch_add(1);
	}
};


class ptrNode {
public:
	unsigned int nodePtr;
	atomic <ptrNode*> next;
	ptrNode(node* n) {
		nodePtr = (unsigned int)n;
		next.store(nullptr);
	}
	~ptrNode()
	{
		delete (node*)nodePtr;
	}
};


class retireList
{
public:
	ptrNode* head[THREAD_N * 2];
	atomic <unsigned int> tmpNodeTable[THREAD_N*2][TMP_WIDTH_N];
	atomic_bool newTmp = false;
	enum {
		TMP = 1,
		NEXT = 2,
		PRE = 0
	};

	ptrNode* getHead(int tid) {
		return head[tid];
	}

	void addTmpNode(int tid, node* node, int place) {
		tmpNodeTable[tid][place].store((unsigned int)node);
	}

	void clearTmpNodes(int tid) {
		for (int i = 0; i < TMP_WIDTH_N; i++)
			tmpNodeTable[tid][i].store(0);
	}
	//retireNodes
	void deleteNodes_weak(int tid) {
		ptrNode* ptr = getHead(tid);
		ptrNode* delPtr = ptr->next;

		while (true)
		{
			if (!delPtr)
				break;
			bool canDelete = true;
			for (int i = 0; i < THREAD_N * 2; i++) {
				for (int j = 0; j < TMP_WIDTH_N; j++) {
					if (tmpNodeTable[i][j].load() == delPtr->nodePtr) {
						canDelete = false;
						goto outside;
					}
				}
			}
		outside:
			if (canDelete) {
				if (ptr->next.compare_exchange_weak(delPtr, delPtr->next.load())) {
					delete delPtr;
					k.fetch_add(1);
				}
				else
					continue;
			}
			else
				ptr = ptr->next;
			delPtr = ptr->next;
		}
	}

	void pushNode(node* n,int tid) {
		ptrNode* newNode = new ptrNode(n);
		newNode->next.store(head[tid]->next.load());
		head[tid]->next.store(newNode);
		deleteNodes_weak(tid);
	}

	retireList() {
		for(int tid=0;tid <THREAD_N*2;tid++)
			head[tid] = new ptrNode(NULL);
	}
	~retireList() {
		deleteNodes();
	}

private:
	void deleteNodes() {
		ptrNode* delPtr;
		int total = 0;
		for (int tid = 0; tid < THREAD_N*2; tid++) {
			for (ptrNode* ptr = getHead(tid); ptr;)
			{
				if(ptr->nodePtr != NULL)
					total++;
				delPtr = ptr;
				ptr = ptr->next;
				delete delPtr;
			}
		}
		cout << "final node in retireList : " << total << endl;
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
	pointerPair getLeftNodeAndRightNode(int value, node* delNode) {
		pointerPair pointerPair;
		node* left_node_next = nullptr;
	search_again:
		do {
			atomic <node*> tmpNode;
			tmpNode.store(head.load()); // 當前首節點
			retireList.addTmpNode(tid(), tmpNode.load(), retireList.TMP);
			node* tmpNode_next = tmpNode.load()->next; // 用來查看當前節點有沒有被標註
			retireList.addTmpNode(tid(), (node*)get_unmarked_ref(tmpNode_next), retireList.NEXT);
			if (tmpNode.load()->next !=tmpNode_next)
				goto search_again;
			// 查找目標節點, 且將其設為右節點。
			do {
				// 當前節點沒被標註, 左節點前進到當前節點。
				if (!is_marked_ref(tmpNode_next)) {
					pointerPair.leftNode = tmpNode.load();
					left_node_next = tmpNode_next;
				}
				if (tmpNode.load()->next != tmpNode_next)
					goto search_again;
				// 當前節點往下走一個節點。
				retireList.addTmpNode(tid(), tmpNode, retireList.PRE);
				tmpNode.store((node*)get_unmarked_ref(tmpNode_next));
				retireList.addTmpNode(tid(), tmpNode, retireList.TMP);
				// 走到尾退出, leftNode為最後一個沒被marked的節點。
				if (tmpNode == tail.load()) break;
				// 得到新節點的標註
				if (tmpNode.load() == nullptr)
					goto search_again;
				tmpNode_next = tmpNode.load()->next;
				retireList.addTmpNode(tid(), (node*)get_unmarked_ref(tmpNode_next), retireList.NEXT);
				// 當節點被標註刪除或是數字還沒到就loop again
			} while (is_marked_ref(tmpNode_next) || (tmpNode.load()->value < value));
			// 右節點為第一個數字大於目標且未被標註刪除的節點 , leftNode為最後一個沒被marked的節點。
			pointerPair.rightNode = tmpNode.load();
			// 如果左右節點相鄰表示不用繞過, 可以回傳目標節點。
			if (left_node_next == pointerPair.rightNode)
				// 回傳前檢查結果是否錯誤 (在查找的過程目標被刪除)
#pragma warning(disable:6011)
				if ((pointerPair.rightNode != tail) && is_marked_ref(pointerPair.rightNode->next))
					goto search_again;
				else {
					retireList.pushNode(delNode,tid());
					return pointerPair;
				}
			// 繞過被標註節點
			if (pointerPair.leftNode->next.compare_exchange_weak(left_node_next, pointerPair.rightNode))
				if ((pointerPair.rightNode != tail) && is_marked_ref(pointerPair.rightNode->next))
					goto search_again;
				else {
					retireList.pushNode(delNode,tid());
					return pointerPair;
				}
		} while (true);
	}

	pointerPair getLeftNodeAndRightNode(int value) {
		pointerPair pointerPair;
		node* left_node_next = nullptr;
	search_again:
		do {
			atomic <node*> tmpNode;
			tmpNode.store(head.load()); // 當前首節點
			retireList.addTmpNode(tid(), tmpNode.load(), retireList.TMP);
			node* tmpNode_next = tmpNode.load()->next; // 用來查看當前節點有沒有被標註
			retireList.addTmpNode(tid(), (node*)get_unmarked_ref(tmpNode_next), retireList.NEXT);
			if (tmpNode.load()->next !=tmpNode_next)
				goto search_again;
			// 查找目標節點, 且將其設為右節點。
			do {
				// 當前節點沒被標註, 左節點前進到當前節點。
				if (!is_marked_ref(tmpNode_next)) {
					pointerPair.leftNode = tmpNode.load();
					left_node_next = tmpNode_next;
				}
				if (tmpNode.load()->next != tmpNode_next)
					goto search_again;
				// 當前節點往下走一個節點。
				retireList.addTmpNode(tid(), tmpNode, retireList.PRE);
				tmpNode.store((node*)get_unmarked_ref(tmpNode_next));
				retireList.addTmpNode(tid(), tmpNode, retireList.TMP);
				// 走到尾退出, leftNode為最後一個沒被marked的節點。
				if (tmpNode == tail.load()) break;
				// 得到新節點的標註
				if (tmpNode.load() == nullptr)
					goto search_again;
				tmpNode_next = tmpNode.load()->next;
				retireList.addTmpNode(tid(), (node*)get_unmarked_ref(tmpNode_next), retireList.NEXT);
				// 當節點被標註刪除或是數字還沒到就loop again
			} while (is_marked_ref(tmpNode_next) || (tmpNode.load()->value < value));
			// 右節點為第一個數字大於目標且未被標註刪除的節點 , leftNode為最後一個沒被marked的節點。
			pointerPair.rightNode = tmpNode.load();
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
				break; // 節點已存在
			new_node->next.store(right_node);
			if (left_node->next.compare_exchange_weak(right_node, new_node))
				break; // CAS插入
		} while (true);
		retireList.clearTmpNodes(tid());
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
				return; // do not find target value
			right_node_next = right_node->next.load();
			if (!is_marked_ref(right_node_next))
				if (right_node->next.compare_exchange_weak(right_node_next, (node*)get_marked_ref(right_node_next)))
					break; // 當右節點的next沒被標記, 就標記右節點的next
		} while (true);
		node* tmp = right_node;
		// 試著使左結點跳過右節點往下走, 失敗就再跑一次搜尋, 其可以跳過被標記節點。
		if (!left_node->next.compare_exchange_weak(right_node, right_node_next))
			getLeftNodeAndRightNode(value,tmp);
		else
			retireList.pushNode(tmp, tid());
		deletes.fetch_add(1);
		retireList.clearTmpNodes(tid());
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
	cout << "delete retireList node by __weak count : " << k << endl;
	cout << "inserts : " << atomic_load(&inserts) << "  after opration count : " << count << endl;
	cout << "logic delete : " << atomic_load(&deletes) << endl;
	cout << "phycical delete : " << nodeDeleteCount.load() << endl;
}