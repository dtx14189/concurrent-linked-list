#include "concurrent-linked-list.hpp"

std::atomic<MarkedList::Node*> MarkedList::accessedPointers[MAX_THREADS][ACCESSED_PTRS_PER_THREAD];

MarkedList::Node::Node(int val, Node* nxt)
    : value(val), next(nxt), removed(false) {}

MarkedList::MarkedList() : length(0), operationCounter(0) {
    head = new Node(-1); // Sentinel with dummy value; never removed
    for (int i = 0; i < MAX_THREADS; ++i) {
        for (int j = 0; j < ACCESSED_PTRS_PER_THREAD; ++j) {
            accessedPointers[i][j].store(nullptr, std::memory_order_relaxed);
        }
    }
}

MarkedList::~MarkedList() {
    Node* curr = head;
    while (curr) {
        Node* temp = curr;
        curr = curr->next;
        delete temp;
    }
}

bool MarkedList::validate(Node* pred, Node* curr) {
    return (!pred->removed && !(curr && curr->removed) && pred->next == curr);
}

void MarkedList::storeAccessedPointer(int threadID, Node* node, int index) {
    accessedPointers[threadID][index].store(node, std::memory_order_release);
}

void MarkedList::resetAccessedPointer(int threadID) {
    for(int i = 0; i < ACCESSED_PTRS_PER_THREAD; i++) {
        accessedPointers[threadID][i].store(nullptr, std::memory_order_release);
    }
}

bool MarkedList::isNodeAccessed(Node* node) {
    for (int i = 0; i < MAX_THREADS; ++i) {
        for(int j = 0; j < ACCESSED_PTRS_PER_THREAD; j++) {
            if (accessedPointers[i][j].load(std::memory_order_acquire) == node) {
                return true;
            }
        }
    }
    return false;
}

void MarkedList::scanAndReclaim() {
    std::lock_guard<std::mutex> lock(retireMutex);
    std::vector<Node*> newRetireList;

    for (Node* node : retireList) {
        if (!isNodeAccessed(node)) {
            // std::cerr << "Deleted: " << node->value << std::endl;
            delete node; // Safe to free
        } else {
            newRetireList.push_back(node);
        }
    }

    retireList = std::move(newRetireList);
}

void MarkedList::insert(int val, int threadID) {
    while (true) {
        Node* pred = head;
        storeAccessedPointer(threadID, pred->next, 0);
        Node* curr = pred->next;
        
        // (1) Traverse without locks
        while (curr && curr->value < val) {
            storeAccessedPointer(threadID, curr, 0);
            pred = curr;
            storeAccessedPointer(threadID, curr->next, 1);
            curr = curr->next;
        }
        
        // (2) Lock pred
        {
            std::unique_lock<std::mutex> lockPred(pred->m);

            // (3) Lock curr if it's not null
            std::unique_lock<std::mutex> lockCurr;
            if (curr) {
                lockCurr = std::unique_lock<std::mutex>(curr->m);
            }
            
            // (4) Validate links and removed flags
            if (!validate(pred, curr)) {
                resetAccessedPointer(threadID);
                continue;
            }
            
            // safely insert b/c 'curr' is either null or has a value >= val
            Node* newNode = new Node(val, curr);
            pred->next = newNode;
        }
        // locks unlock automatically at scope exit

        resetAccessedPointer(threadID);

        length.fetch_add(1, std::memory_order_relaxed);
        if (operationCounter.fetch_add(1, std::memory_order_relaxed) + 1 >= length) {
            scanAndReclaim();
            operationCounter.fetch_sub(length, std::memory_order_relaxed);
        }

        return;
    }
}

bool MarkedList::remove(int val, int threadID) {
    while (true) {
        Node* pred = head;
        Node* curr = pred->next;
        storeAccessedPointer(threadID, pred->next, 0);
        
        // (1) Traverse without locks
        while (curr && curr->value < val) {
            storeAccessedPointer(threadID, curr, 0);
            pred = curr;
            storeAccessedPointer(threadID, curr->next, 1);
            curr = curr->next;
        }

        // (2) Lock pred
        {
            std::unique_lock<std::mutex> lockPred(pred->m);

            // (3) Lock curr if not null
            std::unique_lock<std::mutex> lockCurr;
            if (curr) {
                lockCurr = std::unique_lock<std::mutex>(curr->m);
            }

            // (4) Validate
            if (!validate(pred, curr)) {
                resetAccessedPointer(threadID);
                continue;
            }
            
            // If 'curr' is null or doesn't match val, not found
            if (!curr || curr->value != val) {
                resetAccessedPointer(threadID);
                return false;
            }
            
            // (5) Logically remove by setting 'removed = true'
            curr->removed = true;

            // (6) Physically unlink from pred
            pred->next = curr->next;

             // Add to retire list instead of freeing immediately
            {
                std::lock_guard<std::mutex> lock(retireMutex);
                retireList.push_back(curr);
            }
            resetAccessedPointer(threadID);
        }

        length.fetch_sub(1, std::memory_order_relaxed);
        if (operationCounter.fetch_add(1, std::memory_order_relaxed) + 1 >= length) {
            scanAndReclaim();
            operationCounter.fetch_sub(length, std::memory_order_relaxed);
        }
        // 'curr' is not freed; it remains for potential safe reclamation
        
        return true;
    }
}

bool MarkedList::contains(int val, int threadID) {
    while (true) {
        Node* curr = head->next;
        storeAccessedPointer(threadID, curr, 0);
        while (curr && curr->value < val) {
            curr = curr->next;
            storeAccessedPointer(threadID, curr, 0);
        }

        bool found = (curr && !curr->removed && curr->value == val);
        resetAccessedPointer(threadID);
        return found;
    }
}

void MarkedList::printList() {
    Node* curr = head->next;
    while (curr) {
        if (!curr->removed) {
            std::cout << curr->value << " ";
        }
        curr = curr->next;
    }
    std::cout << std::endl;
}

int MarkedList::get_length() {
    return length;
}

void MarkedList::printRetireList() {
    for (Node* node : retireList) {
        std::cout << node->value << " ";
    }
    std::cout << std::endl;
}

bool MarkedList::checkList() {
    Node* prev = head;
    Node* curr = head->next;
    while (curr) {
        if (curr->value < prev->value) {
            return false;
        }
        prev = curr;
        curr = curr->next;
    }
    return true;
}
