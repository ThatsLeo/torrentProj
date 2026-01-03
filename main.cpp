#include "parser/TorrentFile.hpp"
#include "TrackerClient/Tracker.hpp"
#include "PeerConnection/peerConnection.hpp"
#include "PieceManager/pieceManager.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <list>
#include <algorithm>
#include <iomanip>
#include <atomic>
#include <memory>

// Struttura per monitorare il thread senza bloccare il main
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
    *finished = true; // Segnala al main che il thread ha terminato
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;

    try {
        TorrentFile torrent;
        if (!torrent.load(argv[1])) return 1;

        PieceManager pm(torrent.getPiecesHash().length() / 20, torrent.getPieceLength(), torrent.getTotalSize());
        pm.setPiecesHashes(torrent.getPiecesHash()); 
        pm.setFilesList(torrent.getFilesList());

        TrackerClient tracker(torrent.getAnnounceUrl());
        std::vector<Peer> peers = tracker.announce(torrent.getInfoHashBinary(), "MY_PEER_ID_1234567890", 0, pm.total_size, 0, 6881);
        
        std::list<Peer> peerPool(peers.begin(), peers.end());
        std::vector<std::unique_ptr<ThreadControl>> activeThreads;

        auto startTime = std::chrono::steady_clock::now();

        while (pm.getLeftBytes() > 0) {
            // PULIZIA NON BLOCCANTE: Rimuove solo i thread che hanno finito davvero
            activeThreads.erase(std::remove_if(activeThreads.begin(), activeThreads.end(),
                [](const std::unique_ptr<ThreadControl>& tc) {
                    if (tc->finished->load()) {
                        if (tc->t.joinable()) tc->t.join();
                        return true;
                    }
                    return false;
                }), activeThreads.end());

            // Avvio nuovi peer (fino a 20 contemporanei)
            while (activeThreads.size() < 20 && !peerPool.empty()) {
                auto tc = std::make_unique<ThreadControl>();
                tc->finished = std::make_shared<std::atomic<bool>>(false);
                tc->t = std::thread(runPeer, peerPool.front(), torrent.getInfoHashBinary(), "MY_PEER_ID_1234567890", &pm, tc->finished);
                activeThreads.push_back(std::move(tc));
                peerPool.pop_front();
            }

            // Calcolo Statistiche (Ora non bloccano più grazie al lock.unlock() in addBlock)
            long long downloaded = pm.getDownloadedBytes();
            double progress = (static_cast<double>(downloaded) / pm.total_size) * 100.0;
            
            std::cout << "\r[" << std::fixed << std::setprecision(2) << progress << "%] "
                      << "Scaricati: " << downloaded / (1024 * 1024) << " MB | "
                      << "Peer: " << activeThreads.size() << "    " << std::flush;

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        std::cout << "\nDownload completato!" << std::endl;
        for (auto& tc : activeThreads) if (tc->t.joinable()) tc->t.join();

    } catch (const std::exception& e) { std::cerr << "Errore: " << e.what() << std::endl; }
    return 0;
}