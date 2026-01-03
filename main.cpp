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
#include <deque>          
#include <algorithm>
#include <iomanip>
#include <atomic>
#include <memory>
#include <random>         

#define MAX_ACTIVE_PEERS 100 


struct ThreadControl {
    std::thread t;
    std::shared_ptr<std::atomic<bool>> finished;
    Peer peer; 
};

bool isPeerActive(const std::vector<std::unique_ptr<ThreadControl>>& activeThreads, const Peer& p) {
    for (const auto& tc : activeThreads) {
        if (tc->peer.ip == p.ip && tc->peer.port == p.port) {
            return true;
        }
    }
    return false;
}

bool isPeerInPool(const std::deque<Peer>& pool, const Peer& p) {
    for (const auto& waiting : pool) {
        if (waiting.ip == p.ip && waiting.port == p.port) {
            return true;
        }
    }
    return false;
}

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
        
        std::stringstream ss;
        for(unsigned char c : infoHash) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
        }
        std::string infoHashHex = ss.str();

        pm.setStateFile(infoHashHex);
        pm.loadBitfield();
        pm.saveBitfield(); 

        pm.setPiecesHashes(torrent.getPiecesHash()); 
        pm.setFilesList(torrent.getFilesList());
        
        TrackerClient tracker(torrent.getAnnounceUrl());
        
        std::random_device rd;
        std::mt19937 g(rd()); 
        
        std::deque<Peer> peerPool; 
        std::vector<std::unique_ptr<ThreadControl>> activeThreads;

        long long downloaded = pm.getDownloadedBytes();
        long long left = pm.getLeftBytes();

        
        std::vector<Peer> initialPeers = tracker.announce(infoHash, myId, downloaded, left, 0, 6881);
        
        
        std::shuffle(initialPeers.begin(), initialPeers.end(), g);
        for(const auto& p : initialPeers) peerPool.push_back(p);

        auto startTime = std::chrono::steady_clock::now();
        std::cout << "Download avviato per: " << argv[1] << "\n" << std::endl;

        
        long long lastBytes = pm.getTotalTransferred();
        auto lastTime = std::chrono::steady_clock::now();

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
                Peer candidate = peerPool.front();
                peerPool.pop_front();

                
                if (isPeerActive(activeThreads, candidate)) {
                    continue;
                }

                auto tc = std::make_unique<ThreadControl>();
                tc->finished = std::make_shared<std::atomic<bool>>(false);
                tc->peer = candidate; 
                tc->t = std::thread(runPeer, candidate, infoHash, myId, &pm, tc->finished);
                activeThreads.push_back(std::move(tc));
            }

            
            downloaded = pm.getDownloadedBytes();
            double progress = (static_cast<double>(downloaded) / pm.total_size) * 100.0;
            
            // Velocità istantanea precisa
            auto currentTime = std::chrono::steady_clock::now();
            std::chrono::duration<double> dt = currentTime - lastTime;
            long long currentTotal = pm.getTotalTransferred();
            
            double speed = 0.0;
            if (dt.count() >= 1.0) { 
                speed = (currentTotal - lastBytes) / dt.count() / 1024.0; // KB/s
                lastBytes = currentTotal;
                lastTime = currentTime;
            } else {
                 
                 speed = (currentTotal - lastBytes) / std::max(dt.count(), 0.001) / 1024.0;
            }

            std::cout << "\r[" 
                      << std::fixed << std::setprecision(2) << progress << "%] "
                      << "MB: " << downloaded / (1024 * 1024) << " / " << pm.total_size / (1024 * 1024) << " | "
                      << "Peer: " << activeThreads.size() << " (Coda: " << peerPool.size() << ") | "
                      << "Vel: ";

            if (speed > 1024.0) {
                std::cout << std::setprecision(2) << (speed / 1024.0) << " MB/s    ";
            } else {
                std::cout << std::setprecision(1) << speed << " KB/s    ";
            }
            std::cout << std::flush;

            
            if (peerPool.size() < 10 || activeThreads.size() < 5) {
                downloaded = pm.getDownloadedBytes();
                left = pm.getLeftBytes();
                
                auto newPeers = tracker.announce(infoHash, myId, downloaded, left, 0, 6881);
                
                
                std::shuffle(newPeers.begin(), newPeers.end(), g);

                for (const auto& np : newPeers) {
                    
                    if (!isPeerActive(activeThreads, np) && !isPeerInPool(peerPool, np)) {
                        peerPool.push_back(np);
                    }
                }
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