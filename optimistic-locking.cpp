#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <random>
#include <atomic>

#define MAX_THREADS 8
#define ACCESSED_PTRS_PER_THREAD 2
// #define RECLAIM_THRESHOLD 10000

// ------------------------------------------------------
// Optimistic (Lazy) Linked List with Marking
// ------------------------------------------------------
class MarkedList {
private:
    struct Node {
        int value;
        Node* next;
        mutable std::mutex m;  // Protects this node
        bool removed;          // 'true' if this node is logically removed

        Node(int val, Node* nxt = nullptr)
            : value(val), next(nxt), removed(false) {}
    };

    Node* head; // Sentinel node: never removed
    mutable std::mutex retireMutex;
    std::vector<Node*> retireList;  // Nodes waiting to be freed
    std::atomic<int> length{0};
    std::atomic<int> operationCounter{0};

    // Validate that 'pred->next == curr', and that both are not removed
    bool validate(Node* pred, Node* curr) {
        return (!pred->removed && ! (curr && curr->removed) && pred->next == curr);
    }   

// Global Accessed Pointers Table
std::atomic<Node*> accessedPointers[MAX_THREADS][ACCESSED_PTRS_PER_THREAD];

public:
    MarkedList() {
        // Sentinel with dummy value; never removed
        head = new Node(-1);
        for (int i = 0; i < MAX_THREADS; ++i) {
            for (int j = 0; j < ACCESSED_PTRS_PER_THREAD; ++j) {
                accessedPointers[i][j].store(nullptr, std::memory_order_relaxed);
            }
        }
    }

    ~MarkedList() {
        Node* curr = head;
        while (curr) {
            Node* temp = curr;
            curr = curr->next;
            delete temp;
        }
    }

    void storeAccessedPointer(int threadID, Node* node, int index) {
        accessedPointers[threadID][index].store(node, std::memory_order_release);
    }

    void resetAccessedPointer(int threadID) {
        for(int i = 0; i < ACCESSED_PTRS_PER_THREAD; i++){
            accessedPointers[threadID][i].store(nullptr, std::memory_order_release);
        }
    }

    bool isNodeAccessed(Node* node) {
        for (int i = 0; i < MAX_THREADS; ++i) {
            for(int j = 0; j < ACCESSED_PTRS_PER_THREAD; j++){
                if (accessedPointers[i][j].load(std::memory_order_acquire) == node) {
                    return true;
                }
            }
            
        }
        return false;
    }

    // --- Scan and Reclaim Memory ---
    void scanAndReclaim() {
        std::lock_guard<std::mutex> lock(retireMutex);
        std::vector<Node*> newRetireList;

        for (Node* node : retireList) {
            if (!isNodeAccessed(node)) {
                // std::cerr << "Deleted: " << node->value << std::endl;
                delete node;  // Safe to free
            } else {
                newRetireList.push_back(node);
            }
        }

        retireList = std::move(newRetireList);
    }

    // Insert 'val' in ascending order
    void insert(int val, int threadID) {
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
                    // If invalid, unlock & retry
                    resetAccessedPointer(threadID);
                    continue;
                }

                // safely insert b/c 'curr' is either null or has a value >= val
                Node* newNode = new Node(val, curr);
                pred->next = newNode;
            }

            resetAccessedPointer(threadID);

            length.fetch_add(1, std::memory_order_relaxed);
            if (operationCounter.fetch_add(1, std::memory_order_relaxed) + 1 >= length) {
                scanAndReclaim();
                operationCounter.fetch_sub(length, std::memory_order_relaxed);  
            }
            
            // locks unlock automatically at scope exit
            return;
        }
    }

    // Remove 'val' if it exists
    bool remove(int val, int threadID) {
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

    // Check if 'val' is in the list
    bool contains(int val, int threadID) {
        while (true) {
            Node* curr = head->next;
            storeAccessedPointer(threadID, curr, 0);
            while(curr && curr->value < val) {
                curr = curr->next;
                storeAccessedPointer(threadID, curr, 0);
            }

            bool found = (curr && !curr->removed && curr->value == val);
            resetAccessedPointer(threadID);
            return found;
        }
    }

    // Print the list contents in ascending order (not thread-safe if concurrent)
    void printList() {
        Node* curr = head->next;
        while (curr) {
            if (!curr->removed) {
                std::cout << curr->value << " ";
            }
            curr = curr->next;
        }
        std::cout << std::endl;
    }

    int get_length() {
        return length;
    }
    
    void printRetireList() {
        for(Node *node : retireList){
            std::cout << node->value << " ";
        }
        std::cout << std::endl;
    }

    bool checkList(){
        Node* prev = head;
        Node* curr = head->next;
        while (curr) {
            if(curr->value < prev->value){
                return false;
            }
            prev = curr;
            curr = curr->next;
        }
        return true;
    }
};

// --------------------
// Multi-Threaded Test
// --------------------
int main() {
    MarkedList list;
    // multiple inserter and remover threads
    const int numInsertThreads = 4;
    const int numRemoveThreads = 4;
    const int opsPerThread = 1000;
    const int print_update = opsPerThread/10;

    // A random seed
    auto seed = std::random_device{}();

    // Inserter function
    auto inserter = [&](int id) {
        std::mt19937 rng(seed + id);
        std::uniform_int_distribution<int> dist(0, 200);
        for (int i = 0; i < opsPerThread; ++i) {
            int val = dist(rng);
            list.insert(val, id);
            // if(i % print_update == 0){
            //     std::cout << "[Inserter " << id << "]" << "Iteration: " << i << " complete \n";
            // }
        }
        // std::cout << "[Inserter " << id << "] done.\n";
    };

    // Remover function
    auto remover = [&](int id) {
        std::mt19937 rng(seed + 100 + id);
        std::uniform_int_distribution<int> dist(0, 200);
        for (int i = 0; i < opsPerThread; ++i) {
            int val = dist(rng);
            list.remove(val, id);
            // if(i % print_update == 0){
            //     std::cout << "[Remover " << id << "]" << "Iteration: " << i << " complete \n";
            // }
        }
        // std::cout << "[Remover " << id << "] done.\n";
    };

    // Create threads
    std::vector<std::thread> threads;
    threads.reserve(numInsertThreads + numRemoveThreads);

    // Spawn inserter threads
    for (int i = 0; i < numInsertThreads; ++i) {
        threads.emplace_back(inserter, i);
    }
    // spawn remover threads
    for (int i = 0; i < numRemoveThreads; ++i) {
        threads.emplace_back(remover, i + numInsertThreads);
    }

    // Join all
    for (auto &t : threads) {
        t.join();
    }

    // Print final list contents
    std::cout << "Final list contents (unmarked nodes):\n";
    list.printList();

    std::cout << "Length: " << list.get_length() << std::endl;

    int checkVal = 50;
    std::cout << "Contains " << checkVal << "? "
              << (list.contains(checkVal, 0) ? "Yes" : "No") << std::endl;

    if(list.checkList()){
        std::cout << "SORTED" << std::endl;
    }
    list.scanAndReclaim();
    return 0;
}

