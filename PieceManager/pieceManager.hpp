#ifndef PIECEMANAGER_HPP
#define PIECEMANAGER_HPP

#include <vector>
#include <shared_mutex>
#include <mutex>
#include <cstdint>
#include <map>
#include <string>

class PieceManager {
public:
    // Campi pubblici per permettere l'accesso diretto (come richiesto per il main)
    std::vector<uint8_t> global_bitfield;
    mutable std::shared_mutex rw_mutex;
    uint32_t piece_length;
    long long total_size; 

    // Costruttore
    PieceManager(size_t numPieces, uint32_t pLen, long long totalSize);

    bool isPieceNeeded(int byteIndex, uint8_t peerByte) const;

    // Segna un pezzo come completato nel bitfield globale
    void markAsComplete(int pieceIndex);

    // Strategia per scegliere quale pezzo chiedere a un peer
    int pickPiece(const std::vector<uint8_t>& peer_bf);
    
    long long getDownloadedBytes() const;
    long long getLeftBytes() const;

    bool addBlock(uint32_t index, uint32_t begin, const uint8_t* blockData, size_t blockSize);

    void setPiecesHashes(const std::string& hashes) {
        this->pieces_hashes = hashes;
    }


    // Getter
    std::vector<uint8_t>& getBitfield();
    std::shared_mutex& getMutex();

private:
    int countSetBits(uint8_t n) const;

    struct PieceProgress {
        std::vector<uint8_t> buffer;
        size_t bytes_received = 0;
    };

    std::map<uint32_t, PieceProgress> in_progress; // Pezzi in fase di scaricamento
    std::string pieces_hashes; // La stringa binaria degli hash dal file .torrent
};

#endif