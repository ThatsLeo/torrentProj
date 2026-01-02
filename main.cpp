#include "parser/TorrentFile.hpp"
#include "peerID/peer.hpp"
#include "TrackerClient/Tracker.hpp"
#include "PeerConnection/peerConnection.hpp"
#include "PieceManager/pieceManager.hpp"
#include <shared_mutex>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>


void runPeer(Peer peer, std::string infoHash, std::string myId, PieceManager* pm) {
    try {
        PeerConnection pc(peer.ip, peer.port, &pm->rw_mutex, &pm->global_bitfield, pm);

        if (pc.connectToPeer()) {
            if (pc.sendHandshake(infoHash, myId) && pc.receiveHandshake(infoHash)) {
                // Ora sendBitfield è pubblica, quindi funziona
                pc.sendBitfield();
                pc.startMessageLoop();
            }
        }
    } catch (...) {
        // I thread silenziano gli errori per non bloccare gli altri
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Uso: ./torrent_app <file.torrent>" << std::endl;
        return 1;
    }

    try {
        TorrentFile torrent;
        if (!torrent.load(argv[1])) return 1;

        std::string infoHash = torrent.getInfoHashBinary();
        std::string myId = generateClientId();

        TrackerClient tracker(torrent.getAnnounceUrl());
        
        size_t numPieces = torrent.getPiecesHash().length() / 20;
        uint32_t pieceLength = torrent.getPieceLength(); // Assicurati che questo metodo esista in TorrentFile
        long long totalSize = torrent.getTotalSize();     // Assicurati che questo metodo esista in TorrentFile

        // 2. Inizializza il PieceManager con i 3 argomenti richiesti
        PieceManager pm(numPieces, pieceLength, totalSize);

        // 3. Quando chiami il tracker, usa i nuovi metodi di PieceManager
        long long downloaded = pm.getDownloadedBytes();
        long long left = pm.getLeftBytes();
        int port = 6881;
        long long uploaded = 0;
        
        std::vector<Peer> peers = tracker.announce(infoHash, myId, downloaded, left, uploaded, port);

        // --- LA LOGICA DEI THREAD DEVE STARE QUI DENTRO ---
        std::vector<std::thread> threads;
        std::cout << "Lancio connessioni verso " << peers.size() << " peer..." << std::endl;

        for (auto& p : peers) {
            // Passiamo pm per puntatore, infoHash e myId per valore
            threads.emplace_back(runPeer, p, infoHash, myId, &pm);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Aspettiamo che i thread finiscano
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

    } catch (const std::exception& e) {
        std::cerr << "Errore fatale: " << e.what() << std::endl;
    }

    return 0;
}