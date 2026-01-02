#ifndef PIECEMANAGER_HPP
#define PIECEMANAGER_HPP

#include <vector>
#include <shared_mutex>
#include <mutex>
#include <cstdint>

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

    // Getter
    std::vector<uint8_t>& getBitfield();
    std::shared_mutex& getMutex();

private:
    int countSetBits(uint8_t n) const;
};

#endif