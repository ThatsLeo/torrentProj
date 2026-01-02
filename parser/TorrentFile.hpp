#ifndef TORRENTFILE_HPP
#define TORRENTFILE_HPP

#include "Bnode.hpp"
#include "sha1.hpp"
#include <vector>
#include <string>

class TorrentFile {
public:
    TorrentFile();
    ~TorrentFile();

    bool load(const std::string& filePath);

    std::string getInfoHash() const;
    std::string getInfoHashBinary() const;
    
    std::string getAnnounceUrl() const;
    long long getTotalSize() const;

    long long getPieceLength() const;

    std::string getPiecesHash() const;

    void printStructure() const;

    static Bnode* parse_element(char* &ptr);
    static Bnode* parse_int(char* &ptr);
    static Bnode* parse_string(char* &ptr);
    static Bnode* parse_list(char* &ptr);
    static Bnode* parse_dictionary(char* &ptr);
private:
    Bnode* root;
    std::vector<char> buffer;
    std::string infoHash;
    std::string infoHashBinary;

    void calculateInfoHash();
    void print_node(Bnode* node, int indent) const;
};

#endif