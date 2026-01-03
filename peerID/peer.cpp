#include <random>
#include <ctime>
#include <iostream>


std::string generateClientId() {
    std::string id = "-MT0001-";
    
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::srand(std::time(nullptr));
    
    for (int i = 0; i < 12; ++i) {
        id += charset[std::rand() % (sizeof(charset) - 1)];
    }
    
    return id; 
}