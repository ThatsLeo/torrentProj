#include "TorrentFile.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

TorrentFile::TorrentFile() : root(nullptr) {}

TorrentFile::~TorrentFile() {
    if (root) delete root;
}

bool TorrentFile::load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    buffer.resize(size);
    if (!file.read(buffer.data(), size)) return false;

    char* ptr = buffer.data();
    root = parse_element(ptr);
    calculateInfoHash();
    return true;
}


Bnode* TorrentFile::parse_element(char* &ptr) {
    switch (*ptr) {
        case 'i': return parse_int(ptr);
        case 'l': return parse_list(ptr);
        case 'd': return parse_dictionary(ptr);
        default:  return parse_string(ptr);
    }
}

Bnode* TorrentFile::parse_int(char* &ptr) {
    Bnode* node = new Bnode(INTEGER);
    node->raw_start = ptr;
    if (*ptr != 'i') throw std::runtime_error("Expected integer");
    ++ptr; // skip 'i'

    char* num_start = ptr;
    while (*ptr != 'e') ++ptr;
    node->int_val = std::stoll(std::string(num_start, ptr - num_start));

    ++ptr; // skip 'e'
    node->raw_end = ptr;
    return node;
}

Bnode* TorrentFile::parse_string(char* &ptr) {
    Bnode* node = new Bnode(STRING);
    node->raw_start = ptr;

    char* len_start = ptr;
    while (*ptr != ':') ++ptr;
    std::string len_str(len_start, ptr - len_start);
    size_t len = std::stoull(len_str);

    ++ptr; // skip ':'
    node->str_val.assign(ptr, len);
    ptr += len;

    node->raw_end = ptr;
    return node;
}

Bnode* TorrentFile::parse_list(char* &ptr) {
    Bnode* node = new Bnode(LIST);
    node->raw_start = ptr;
    ++ptr; // skip 'l'

    while (*ptr != 'e') {
        node->list_val.push_back(parse_element(ptr));
    }

    ++ptr; // skip 'e'
    node->raw_end = ptr;
    return node;
}

Bnode* TorrentFile::parse_dictionary(char* &ptr) {
    Bnode* node = new Bnode(DICTIONARY);
    node->raw_start = ptr;
    ++ptr; // skip 'd'

    while (*ptr != 'e') {
        Bnode* key_node = parse_string(ptr);
        std::string key = key_node->str_val;
        delete key_node;

        node->dict_val.push_back({key, parse_element(ptr)});
    }

    ++ptr; // skip 'e'
    node->raw_end = ptr;
    return node;
}

void TorrentFile::calculateInfoHash() {
    Bnode* infoNode = nullptr;
    for (auto& pair : root->dict_val) {
        if (pair.first == "info") {
            infoNode = pair.second;
            break;
        }
    }

    if (infoNode && infoNode->raw_start && infoNode->raw_end) {
        sha1 checksum;
        checksum.add(infoNode->raw_start, infoNode->raw_end - infoNode->raw_start);
        checksum.finalize();

        // 1. Estrazione dei 20 byte binari (Big-Endian)
        uint8_t raw[20];
        for (int i = 0; i < 5; i++) {

            raw[i * 4 + 0] = (checksum.state[i] >> 24) & 0xFF;
            raw[i * 4 + 1] = (checksum.state[i] >> 16) & 0xFF;
            raw[i * 4 + 2] = (checksum.state[i] >> 8) & 0xFF;
            raw[i * 4 + 3] = (checksum.state[i]) & 0xFF;
        }

        this->infoHashBinary = std::string((char*)raw, 20);

        char hexHash[SHA1_HEX_SIZE];
        checksum.print_hex(hexHash);
        this->infoHash = std::string(hexHash);
    }
}

std::string TorrentFile::getInfoHash() const { return this->infoHash; }

std::string TorrentFile::getInfoHashBinary() const { return this->infoHashBinary;}

void TorrentFile::printStructure() const { print_node(root, 0); }


std::string TorrentFile::getAnnounceUrl() const {

    for (const auto& pair : root->dict_val){
        
        if (pair.first == "announce"){

            return pair.second->str_val;
        }
    }
    return "";
}


long long TorrentFile::getTotalSize() const {

    if (!root) return 0;

    Bnode* infoNode = nullptr;

    for(const auto& pair : root->dict_val){
        if(pair.first == "info"){
            infoNode = pair.second;
            break;
        }
    }

    if (!infoNode) return 0;

    for (const auto& pair : infoNode->dict_val){

        if (pair.first == "length"){
            return pair.second->int_val;
        }

    }


    long long totalSize = 0;

    for (const auto& pair : infoNode->dict_val){

        if (pair.first == "files"){

            Bnode* filesList = pair.second;

            for (Bnode* fileDict : filesList->list_val){

                for (const auto& fPair : fileDict->dict_val){

                    if (fPair.first == "length"){
                        totalSize += fPair.second->int_val;
                    }
                }

            }

            return totalSize;
        }
    }

    return 0;
}


std::string TorrentFile::getPiecesHash() const {
    if (!root) return "";

    Bnode* infoNode = nullptr;
    for (const auto& pair : root->dict_val) {
        if (pair.first == "info") {
            infoNode = pair.second;
            break;
        }
    }

    if (!infoNode) return "";

    for (const auto& pair : infoNode->dict_val) {
        if (pair.first == "pieces") {
            return pair.second->str_val;
        }
    }

    return "";
}



void TorrentFile::print_node(Bnode* node, int indent) const {
    if (!node) return;
    std::string pad(indent * 2, ' ');

    switch (node->type) {
        case INTEGER:
            std::cout << pad << "int: " << node->int_val << '\n';
            break;
        case STRING:
            std::cout << pad << "string: " << node->str_val << '\n';
            break;
        case LIST:
            std::cout << pad << "list:" << '\n';
            for (Bnode* child : node->list_val) print_node(child, indent + 1);
            break;
        case DICTIONARY:
            std::cout << pad << "dict:" << '\n';
            for (auto& kv : node->dict_val) {
                std::cout << pad << "  key: " << kv.first << '\n';
                print_node(kv.second, indent + 2);
            }
            break;
    }
}

long long TorrentFile::getPieceLength() const {

    if (!root) return 0;

    Bnode* infoNode = nullptr;

    // 1. Cerco il nodo "info" nel root
    for(const auto& pair : root->dict_val){
        if(pair.first == "info"){
            infoNode = pair.second;
            break;
        }
    }

    if (!infoNode) return 0;

    // 2. Cerco la chiave "piece length" all'interno del nodo info
    for (const auto& pair : infoNode->dict_val){

        if (pair.first == "piece length"){
            // Ritorna il valore intero associato
            return pair.second->int_val;
        }

    }

    return 0;
}