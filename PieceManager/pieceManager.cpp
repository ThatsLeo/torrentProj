#include "pieceManager.hpp"
#include <algorithm> 
#include "../parser/sha1.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>

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

bool PieceManager::addBlock(uint32_t index, uint32_t begin, const uint8_t* blockData, size_t blockSize) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex); 

    // 1. Controllo bitfield (già corretto nel tuo codice)
    size_t byteIdx = index / 8;
    if (byteIdx < global_bitfield.size()) {
        if (global_bitfield[byteIdx] & (1 << (7 - (index % 8)))) return false; 
    }

    // 2. Inizializzazione buffer (già corretto)
    if (in_progress.find(index) == in_progress.end()) {
        uint32_t current_p_len = piece_length;
        long long numPieces = (total_size + piece_length - 1) / piece_length;
        if (index == numPieces - 1) {
            current_p_len = total_size - (index * (long long)piece_length);
        }
        in_progress[index].buffer.resize(current_p_len);
        in_progress[index].bytes_received = 0;
    }

    // 3. Copia dati
    PieceProgress& p = in_progress[index];
    if (begin + blockSize <= p.buffer.size()) {
        std::copy(blockData, blockData + blockSize, p.buffer.begin() + begin);
        p.bytes_received += blockSize;
    }

    // 4. Verifica Hash
    if (p.bytes_received >= p.buffer.size()) {
        sha1 hasher;
        hasher.add(p.buffer.data(), p.buffer.size());
        hasher.finalize();
        
        char calculated_hex[41];
        hasher.print_hex(calculated_hex);

        std::string expected_bin = pieces_hashes.substr(index * 20, 20);
        std::stringstream ss;
        for(unsigned char c : expected_bin) ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
        std::string expected_hex = ss.str();

        if (std::string(calculated_hex) == expected_hex) {
            std::cout << "[OK] Pezzo #" << index << " verificato." << std::endl;
            
            saveToDisk(index, p.buffer);

            // --- FISSA QUI: Aggiorna il bitfield direttamente senza chiamare markAsComplete ---
            if (byteIdx < global_bitfield.size()) {
                global_bitfield[byteIdx] |= (1 << (7 - (index % 8)));
            }
            
            in_progress.erase(index);
            return true;
        } else {
            std::cout << "[FAIL] Pezzo #" << index << " corrotto! Hash errato." << std::endl;
            in_progress.erase(index);
            return false;
        }
    }
    return false;
}

void PieceManager::saveToDisk(uint32_t index, const std::vector<uint8_t>& data) {
    long long pieceGlobalOffset = (long long)index * piece_length;
    long long currentFileStart = 0;
    size_t dataOffset = 0;
    long long bytesRemaining = data.size();

    for (const auto& file : filesList) {
        long long currentFileEnd = currentFileStart + file.length;
        if (pieceGlobalOffset < currentFileEnd && (pieceGlobalOffset + bytesRemaining) > currentFileStart) {
            long long writeOffset = std::max(0LL, pieceGlobalOffset - currentFileStart);
            long long bytesToWrite = std::min(bytesRemaining, file.length - writeOffset);

            std::filesystem::path p(file.path);
            if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

            std::fstream fs(file.path, std::ios::in | std::ios::out | std::ios::binary);
            if (!fs.is_open()) {
                std::ofstream create(file.path, std::ios::binary);
                create.seekp(file.length - 1);
                create.write("\0", 1);
                create.close();
                fs.open(file.path, std::ios::in | std::ios::out | std::ios::binary);
            }
            fs.seekp(writeOffset);
            fs.write(reinterpret_cast<const char*>(&data[dataOffset]), bytesToWrite);
            fs.close();

            dataOffset += bytesToWrite;
            bytesRemaining -= bytesToWrite;
            pieceGlobalOffset += bytesToWrite; 
        }
        currentFileStart += file.length;
        if (bytesRemaining <= 0) break;
    }
}