#ifndef PIECEMANAGER_HPP
#define PIECEMANAGER_HPP

#include <vector>
#include <shared_mutex>
#include <mutex>
#include <cstdint>
#include <map>
#include <string>
#include <fstream>
#include <atomic>
#include "../parser/TorrentFile.hpp"


class PieceManager {
public:
    
    std::vector<uint8_t> global_bitfield;
    mutable std::shared_mutex rw_mutex;
    uint32_t piece_length;
    long long total_size; 
    std::string download_filename = "output_file.dat";
    std::vector<FileInfo> filesList;
    std::atomic<long long> total_transferred{0}; 

    
    PieceManager(size_t numPieces, uint32_t pLen, long long totalSize);

    bool isPieceNeeded(int byteIndex, uint8_t peerByte) const;

    
    void markAsComplete(int pieceIndex);


    int pickPiece(const std::vector<uint8_t>& peer_bf);
    
    long long getDownloadedBytes() const;
    long long getLeftBytes() const;

    bool addBlock(uint32_t index, uint32_t begin, const uint8_t* blockData, size_t blockSize);

    void setPiecesHashes(const std::string& hashes) {
        this->pieces_hashes = hashes;
    }

    void saveToDisk(uint32_t index, const std::vector<uint8_t>& data);
    void setFilesList(const std::vector<FileInfo>& files) { this->filesList = files; }


    std::vector<uint8_t>& getBitfield();
    std::shared_mutex& getMutex();
    long long getTotalTransferred() const { return total_transferred.load(); }

private:
    int countSetBits(uint8_t n) const;

    void _markAsComplete(int pieceIndex);

    struct PieceProgress {
    std::vector<uint8_t> buffer;
    std::vector<bool> blocks_received; 
    size_t bytes_received = 0;
    };

    std::map<uint32_t, PieceProgress> in_progress; 
    std::string pieces_hashes; 
};

#endif