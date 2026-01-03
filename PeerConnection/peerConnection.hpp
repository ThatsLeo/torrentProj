#ifndef PEERCONNECTION_HPP
#define PEERCONNECTION_HPP

#include <string>
#include <vector>
#include <arpa/inet.h>
#include <shared_mutex>
#include <mutex>
#include "../PieceManager/pieceManager.hpp"

class PeerConnection {
public:

    struct BTMessage {
    uint32_t length;
    uint8_t id;
    std::vector<uint8_t> payload;
    };

    PeerConnection(std::string ip, uint16_t port, std::shared_mutex* bitfield_mutex, std::vector<uint8_t>* global_bitfield, PieceManager* piece_manager);
    ~PeerConnection();

    BTMessage readMessage();

    bool connectToPeer();
    bool sendHandshake(const std::string& infoHash, const std::string& peerId);
    bool receiveHandshake(const std::string& expectedHash);

    void sendRequest(uint32_t index, uint32_t begin, uint32_t length);

    void startMessageLoop();

    void sendBitfield();

private:
    std::string ip;
    uint16_t port;
    int sockfd;
    
    bool peer_choking = true;
    bool peer_interested = false; 
    bool am_choking = true;
    bool am_interested = false;

    void handleMessage(const BTMessage& msg);
    
    void sendInterested();
    bool am_Interested();
    bool readAll(void* buf, size_t len);
    
    std::vector<uint8_t> peer_bitfield;

    PieceManager* piece_manager;
    std::vector<uint8_t>* global_bitfield;
    std::shared_mutex* bitfield_mutex;

};

#endif