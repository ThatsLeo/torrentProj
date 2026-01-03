#include "Bnode.hpp"

Bnode::Bnode(Btype t) : type(t), int_val(0) {}

Bnode::~Bnode() {
    if (type == LIST) {
        for (Bnode* node : list_val) delete node;
    } 
    else if (type == DICTIONARY) {
        for (auto& pair : dict_val) delete pair.second;
    }
}