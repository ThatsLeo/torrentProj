#ifndef BNODE_HPP
#define BNODE_HPP

#include <vector>
#include <string>
#include <utility>

enum Btype { INTEGER, STRING, LIST, DICTIONARY };

struct Bnode {
    Btype type;
    long long int_val;
    std::string str_val;
    
    std::vector<Bnode*> list_val;
    std::vector<std::pair<std::string, Bnode*>> dict_val;

    char* raw_start = nullptr;
    char* raw_end = nullptr;

    Bnode(Btype t);
    ~Bnode();
};

#endif