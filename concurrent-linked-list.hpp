#ifndef CONCURRENT_LINKED_LIST_H
#define CONCURRENT_LINKED_LIST_H

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>

#define MAX_THREADS 8
#define ACCESSED_PTRS_PER_THREAD 2

// ------------------------------------------------------
// Optimistic (Lazy) Linked List with Marking
// ------------------------------------------------------
class MarkedList {
private:
    struct Node {
        int value;
        Node* next;
        mutable std::mutex m; // Protects this node
        bool removed;         // 'true' if this node is logically removed

        Node(int val, Node* nxt = nullptr);
    };

    Node* head; // Sentinel node: never removed
    mutable std::mutex retireMutex;
    std::vector<Node*> retireList; // Nodes waiting to be freed
    std::atomic<int> length;
    std::atomic<int> operationCounter;

    bool validate(Node* pred, Node* curr); // Validate that 'pred->next == curr', and that both are not removed
    void storeAccessedPointer(int threadID, Node* node, int index);
    void resetAccessedPointer(int threadID);
    bool isNodeAccessed(Node* node);

    static std::atomic<Node*> accessedPointers[MAX_THREADS][ACCESSED_PTRS_PER_THREAD];

public:
    MarkedList();
    ~MarkedList();

    void insert(int val, int threadID); // Insert 'val' in ascending order
    bool remove(int val, int threadID); // Remove 'val' if it exists
    bool contains(int val, int threadID); // Check if 'val' is in the list

    void scanAndReclaim(); // Scan and Reclaim Memory
    
    void printList(); // Print the list contents in ascending order
    int get_length();
    void printRetireList();
    bool checkList();
};

#endif 
