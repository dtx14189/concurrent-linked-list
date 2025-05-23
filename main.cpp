#include <iostream>
#include <thread>
#include <vector>
#include <random>

#include "concurrent-linked-list.hpp"

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
