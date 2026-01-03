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
#include <atomic>
#include <memory>

#define MAX_ACTIVE_PEERS 100

struct ThreadControl {
    std::thread t;
    std::shared_ptr<std::atomic<bool>> finished;
};

void runPeer(Peer peer, std::string infoHash, std::string myId, PieceManager* pm, std::shared_ptr<std::atomic<bool>> finished) {
    try {
        PeerConnection pc(peer.ip, peer.port, &pm->rw_mutex, &pm->global_bitfield, pm);
        if (pc.connectToPeer()) {
            if (pc.sendHandshake(infoHash, myId) && pc.receiveHandshake(infoHash)) {
                pc.startMessageLoop();
            }
        }
    } catch (...) {}
    *finished = true; 
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
        
        pm.setStateFile(torrent.getInfoHash());
        pm.loadBitfield();

        pm.setPiecesHashes(torrent.getPiecesHash()); 
        pm.setFilesList(torrent.getFilesList());
        
        

        TrackerClient tracker(torrent.getAnnounceUrl());
        std::vector<Peer> peers = tracker.announce(infoHash, myId, 0, pm.total_size, 0, 6881);
        std::list<Peer> peerPool(peers.begin(), peers.end());
        
        std::vector<std::unique_ptr<ThreadControl>> activeThreads;

        auto startTime = std::chrono::steady_clock::now();
        std::cout << "Download avviato per: " << argv[1] << "\n" << std::endl;

        while (pm.getLeftBytes() > 0) {
            
            activeThreads.erase(std::remove_if(activeThreads.begin(), activeThreads.end(),
                [](const std::unique_ptr<ThreadControl>& tc) {
                    if (tc->finished->load()) { 
                        if (tc->t.joinable()) tc->t.join(); 
                        return true;
                    }
                    return false; 
                }), 
                activeThreads.end());

            
            while (activeThreads.size() < MAX_ACTIVE_PEERS && !peerPool.empty()) {
                auto tc = std::make_unique<ThreadControl>();
                tc->finished = std::make_shared<std::atomic<bool>>(false);
                tc->t = std::thread(runPeer, peerPool.front(), infoHash, myId, &pm, tc->finished);
                activeThreads.push_back(std::move(tc));
                peerPool.pop_front();
            }

            
            long long downloaded = pm.getDownloadedBytes();
            double progress = (static_cast<double>(downloaded) / pm.total_size) * 100.0;
            
            auto currentTime = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = currentTime - startTime;
            double speed = (elapsed.count() > 0) ? (downloaded / elapsed.count() / 1024.0) : 0;

            
            std::cout << "\r[" 
                      << std::fixed << std::setprecision(2) << progress << "%] "
                      << "MB: " << downloaded / (1024 * 1024) << " / " << pm.total_size / (1024 * 1024) << " | "
                      << "Peer: " << activeThreads.size() << " | "
                      << "Vel: " << std::setprecision(1) << speed << " KB/s    " 
                      << std::flush;

            
            if (peerPool.empty() && activeThreads.size() < 5) {
                auto newPeers = tracker.announce(infoHash, myId, pm.getDownloadedBytes(), pm.getLeftBytes(), 0, 6881);
                for (auto& np : newPeers) peerPool.push_back(np);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        std::cout << "\n\nDownload completato!" << std::endl;
        for (auto& tc : activeThreads) if (tc->t.joinable()) tc->t.join();

    } catch (const std::exception& e) {
        std::cerr << "\nErrore: " << e.what() << std::endl;
    }
    return 0;
}