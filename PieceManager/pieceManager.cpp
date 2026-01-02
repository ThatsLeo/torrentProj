#include "pieceManager.hpp"
#include <algorithm> 

// Costruttore aggiornato con inizializzazione di piece_length e total_size
PieceManager::PieceManager(size_t numPieces, uint32_t pLen, long long totalSize) 
    : global_bitfield((numPieces + 7) / 8, 0), 
      piece_length(pLen), 
      total_size(totalSize) 
{
    // Il mutex viene inizializzato automaticamente
}

// Helper privato per contare i bit a 1 (Algoritmo di Brian Kernighan)
int PieceManager::countSetBits(uint8_t n) const {
    int count = 0;
    while (n > 0) {
        n &= (n - 1);
        count++;
    }
    return count;
}

// Restituisce il totale dei byte scaricati basandosi sui pezzi completati
long long PieceManager::getDownloadedBytes() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    long long completedPieces = 0;

    for (uint8_t byte : global_bitfield) {
        completedPieces += countSetBits(byte);
    }

    long long downloaded = completedPieces * piece_length;

    // Protezione: non restituire mai più della dimensione totale del file
    // (L'ultimo pezzo potrebbe essere più corto della piece_length standard)
    return std::min(downloaded, total_size);
}

// Restituisce i byte mancanti alla fine del download
long long PieceManager::getLeftBytes() const {
    // getDownloadedBytes gestisce internamente il lock, quindi qui non serve
    long long downloaded = getDownloadedBytes();
    
    if (downloaded >= total_size) return 0;
    return total_size - downloaded;
}

bool PieceManager::isPieceNeeded(int byteIndex, uint8_t peerByte) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);
    if (byteIndex >= (int)global_bitfield.size()) return false;
    
    // Ritorna true se il peer ha almeno un bit a 1 che noi abbiamo a 0
    return (peerByte & ~global_bitfield[byteIndex]) != 0;
}

void PieceManager::markAsComplete(int pieceIndex) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex);
    size_t byteIdx = pieceIndex / 8;
    if (byteIdx < global_bitfield.size()) {
        // Imposta il bit corretto a 1 (MSB first)
        global_bitfield[byteIdx] |= (1 << (7 - (pieceIndex % 8)));
    }
}

int PieceManager::pickPiece(const std::vector<uint8_t>& peer_bf) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex);

    // Scorriamo i bitfield per trovare pezzi mancanti posseduti dal peer
    for (size_t i = 0; i < global_bitfield.size() && i < peer_bf.size(); ++i) {
        uint8_t needed = peer_bf[i] & ~global_bitfield[i];

        if (needed != 0) {
            for (int bit = 7; bit >= 0; --bit) {
                if (needed & (1 << bit)) {
                    return (i * 8) + (7 - bit);
                }
            }
        }
    }
    return -1; // Nessun pezzo utile trovato
}

// Getter per il riferimento al bitfield
std::vector<uint8_t>& PieceManager::getBitfield() { 
    return global_bitfield; 
}

// Getter per il riferimento al mutex
std::shared_mutex& PieceManager::getMutex() { 
    return rw_mutex; 
}