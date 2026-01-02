#include "parser/TorrentFile.hpp"
#include "peerID/peer.hpp"
#include "TrackerClient/Tracker.hpp"
#include "PeerConnection/peerConnection.hpp"
#include "PieceManager/pieceManager.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <list>
#include <algorithm>
#include <iomanip>

void runPeer(Peer peer, std::string infoHash, std::string myId, PieceManager* pm) {
    try {
        PeerConnection pc(peer.ip, peer.port, &pm->rw_mutex, &pm->global_bitfield, pm);
        if (pc.connectToPeer()) {
            if (pc.sendHandshake(infoHash, myId) && pc.receiveHandshake(infoHash)) {
                pc.startMessageLoop();
            }
        }
    } catch (...) {}
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
        PieceManager pm(torrent.getPiecesHash().length() / 20, torrent.getPieceLength(), torrent.getTotalSize());
        pm.setPiecesHashes(torrent.getPiecesHash()); 
        pm.setFilesList(torrent.getFilesList());

        TrackerClient tracker(torrent.getAnnounceUrl());
        std::vector<Peer> peers = tracker.announce(infoHash, myId, 0, pm.total_size, 0, 6881);
        std::list<Peer> peerPool(peers.begin(), peers.end());
        std::vector<std::thread> activeThreads;

        long long lastDownloaded = 0;
        auto startTime = std::chrono::steady_clock::now();

        std::cout << "Download avviato per: " << argv[1] << "\n" << std::endl;

        while (pm.getLeftBytes() > 0) {
            // Gestione Thread
            activeThreads.erase(std::remove_if(activeThreads.begin(), activeThreads.end(),
                [](std::thread &t) { if (t.joinable()) { t.join(); return true; } return false; }), 
                activeThreads.end());

            while (activeThreads.size() < 20 && !peerPool.empty()) {
                activeThreads.emplace_back(runPeer, peerPool.front(), infoHash, myId, &pm);
                peerPool.pop_front();
            }

            // Calcolo Statistiche
            long long downloaded = pm.getDownloadedBytes();
            double progress = (static_cast<double>(downloaded) / pm.total_size) * 100.0;
            
            auto currentTime = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = currentTime - startTime;
            double speed = (elapsed.count() > 0) ? (downloaded / elapsed.count() / 1024.0) : 0; // KB/s

            // Stampa ad riga singola (Sovrascrittura)
            std::cout << "\r[" 
                      << std::fixed << std::setprecision(2) << progress << "%] "
                      << "Scaricati: " << downloaded / (1024 * 1024) << " MB / " << pm.total_size / (1024 * 1024) << " MB | "
                      << "Peer: " << activeThreads.size() << " | "
                      << "Velocità: " << std::setprecision(1) << speed << " KB/s    " 
                      << std::flush;

            if (peerPool.empty() && activeThreads.size() < 5) {
                auto newPeers = tracker.announce(infoHash, myId, pm.getDownloadedBytes(), pm.getLeftBytes(), 0, 6881);
                for (auto& np : newPeers) peerPool.push_back(np);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::cout << "\n\nDownload completato!" << std::endl;
        for (auto& t : activeThreads) if (t.joinable()) t.join();

    } catch (const std::exception& e) {
        std::cerr << "\nErrore: " << e.what() << std::endl;
    }
    return 0;
}