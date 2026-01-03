#include "peerConnection.hpp"
#include <shared_mutex>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <errno.h>

PeerConnection::PeerConnection(std::string ip, uint16_t port, std::shared_mutex* bitfield_mutex, std::vector<uint8_t>* global_bitfield, PieceManager* piece_manager) {
    this->ip = ip;
    this->port = port;
    this->sockfd = -1;
    this->bitfield_mutex = bitfield_mutex;
    this->global_bitfield = global_bitfield;
    this->piece_manager = piece_manager;
}

PeerConnection::~PeerConnection(){
    if (sockfd != -1){
        close(sockfd);
    }
}


bool PeerConnection::connectToPeer() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return false;


    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);


    int res = connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));

    time_t timeout = 1; 

    if (res < 0) {
        if (errno == EINPROGRESS) {
            
            fd_set set;
            FD_ZERO(&set);
            FD_SET(sockfd, &set);

            struct timeval tv;
            tv.tv_sec = timeout;
            tv.tv_usec = 0;

            
            res = select(sockfd + 1, NULL, &set, NULL, &tv);

            if (res <= 0) {
                
                return false;
            } else {
                
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error != 0) return false;
            }
        } else {
            return false;
        }
    }

    
    fcntl(sockfd, F_SETFL, flags);

    
    struct timeval timeout_recv = {timeout, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout_recv, sizeof(timeout_recv));

    return true;
}



bool PeerConnection::sendHandshake(const std::string& infoHash, const std::string& peerId) {
    uint8_t handshake[68];
    
    handshake[0] = 19; 
    std::memcpy(&handshake[1], "BitTorrent protocol", 19); 
    std::memset(&handshake[20], 0, 8); 
    std::memcpy(&handshake[28], infoHash.data(), 20); 
    std::memcpy(&handshake[48], peerId.data(), 20);   

    return send(sockfd, handshake, 68, 0) == 68;
}

bool PeerConnection::receiveHandshake(const std::string& expectedHash) {
    uint8_t response[68];
    ssize_t bytesRead = recv(sockfd, response, 68, 0);

    if (bytesRead < 68) {
        //std::cerr << "Risposta handshake incompleta da " << ip << std::endl;
        return false;
    }


    if (std::memcmp(&response[28], expectedHash.data(), 20) != 0) {
        std::cerr << "Info-hash non corrispondente!" << std::endl;
        return false;
    }

    //std::cout << "Handshake completato con successo con " << ip << std::endl;
    return true;
}




PeerConnection::BTMessage PeerConnection::readMessage() {
    BTMessage msg;
    uint32_t len_net;

    
    if (!readAll(&len_net, 4)) {
        return {0, 0, {}}; 
    }
    msg.length = ntohl(len_net);

    
    if (msg.length == 0) {
        return {0, 0xFF, {}}; 
    }


    if (!readAll(&msg.id, 1)) {
        return {0, 0, {}};
    }


    if (msg.length > 1) {
        msg.payload.resize(msg.length - 1);
        if (!readAll(msg.payload.data(), msg.length - 1)) {
            return {0, 0, {}};
        }
    }

    return msg;
}



void PeerConnection::handleMessage(const BTMessage& msg) {

    const int PIPELINE_SIZE = 100;

    switch (msg.id) {
        case 0: 
            //std::cout << "[Msg] CHOKE: Il peer " << ip << " ci ha bloccati (normale)." << std::endl;
            peer_choking = true;
            break;
        
        case 1: 
            this->peer_choking = false;
            {
                int pieceToRequest = this->piece_manager->pickPiece(this->peer_bitfield);
                if (pieceToRequest != -1) {
                    
                    for (int i = 0; i < PIPELINE_SIZE; ++i) {
                        uint32_t offset = i * 16384;
                        if (offset < this->piece_manager->piece_length) {
                            this->sendRequest(pieceToRequest, offset, 16384);
                        }
                    }
                }
            }
            break;

        case 4: 
            if (msg.payload.size() == 4) {
            uint32_t index = ntohl(*reinterpret_cast<const uint32_t*>(msg.payload.data()));
            size_t byteIdx = index / 8;
            if (byteIdx < peer_bitfield.size()) {
                peer_bitfield[byteIdx] |= (1 << (7 - (index % 8)));
            }
            if (!this->am_interested && am_Interested()) sendInterested();
            }
            break;

        case 5: 
            //std::cout << "[Msg] BITFIELD ricevuto da " << ip << " (" << msg.payload.size() << " bytes)" << std::endl;
            this->peer_bitfield = msg.payload;
            if (!this->am_interested && am_Interested()) {
                sendInterested();
            }
            break;
        
        case 7: { 
            if (msg.payload.size() >= 8) {
            uint32_t index = ntohl(*reinterpret_cast<const uint32_t*>(msg.payload.data()));
            uint32_t begin = ntohl(*reinterpret_cast<const uint32_t*>(msg.payload.data() + 4));
            const uint8_t* blockData = msg.payload.data() + 8;
            size_t blockSize = msg.payload.size() - 8;

            bool isComplete = this->piece_manager->addBlock(index, begin, blockData, blockSize);

            if (!peer_choking) {
                if (isComplete) {
                    
                    int nextPiece = this->piece_manager->pickPiece(this->peer_bitfield);
                    if (nextPiece != -1) {
                        for (int i = 0; i < PIPELINE_SIZE; ++i) {
                            uint32_t offset = i * 16384;
                            if (offset < this->piece_manager->piece_length) {
                                this->sendRequest(nextPiece, offset, 16384);
                            }
                        }
                    }
                } else {
                    
                    uint32_t nextRequestOffset = begin + (PIPELINE_SIZE * 16384);
                    if (nextRequestOffset < this->piece_manager->piece_length) {
                        this->sendRequest(index, nextRequestOffset, 16384);
                    }
                }
            }
        }
        break;
        }
        

        case 0xFF: 
            //std::cout << "[Msg] KEEP-ALIVE ricevuto." << std::endl;
            break;

        default:
            //std::cout << "[Msg] Ricevuto ID sconosciuto: " << (int)msg.id << " Lunghezza: " << msg.length << std::endl;
            break;
    }
}

void PeerConnection::startMessageLoop() {
    sendBitfield();


    while (true) {
        BTMessage msg = readMessage();
        
        if (msg.length == 0 && msg.id == 0) { 
             //std::cout << "Connessione chiusa dal peer " << ip << std::endl;
             break;
        }
        
        handleMessage(msg);
    }
}


bool PeerConnection::am_Interested() {
    std::shared_lock<std::shared_mutex> lock(*this->bitfield_mutex);
    
    size_t limit = std::min(peer_bitfield.size(), global_bitfield->size());
    for (size_t i = 0; i < limit; ++i) {
        if ((peer_bitfield[i] & ~((*global_bitfield)[i])) != 0) {
            return true;
        }
    }
    return false;
}

void PeerConnection::sendInterested() {
    
    
    uint32_t length = htonl(1); 
    uint8_t id = 2;             


    uint8_t message[5];
    memcpy(message, &length, 4);
    message[4] = id;

    ssize_t sent = send(sockfd, message, sizeof(message), 0);

    if (sent == sizeof(message)) {
        this->am_interested = true;
        //std::cout << "[Out] Inviato messaggio INTERESTED a " << ip << std::endl;
    } else {
        //std::cerr << "[Err] Errore nell'invio del messaggio INTERESTED" << std::endl;
    }
}


void PeerConnection::sendRequest(uint32_t index, uint32_t begin, uint32_t length) {
    uint32_t msg_len = htonl(13); 
    uint8_t id = 6;
    
    uint32_t n_index = htonl(index);
    uint32_t n_begin = htonl(begin);
    uint32_t n_length = htonl(length);

    uint8_t packet[17]; 
    memcpy(packet, &msg_len, 4);
    packet[4] = id;
    memcpy(packet + 5, &n_index, 4);
    memcpy(packet + 9, &n_begin, 4);
    memcpy(packet + 13, &n_length, 4);

    send(sockfd, packet, 17, 0);
    //std::cout << "[Out] Richiesto pezzo #" << index << " blocco " << begin << std::endl;
}


void PeerConnection::sendBitfield() {

    std::vector<uint8_t> current_bf;
    {
        std::shared_lock<std::shared_mutex> lock(*bitfield_mutex);
        current_bf = *global_bitfield;
    }

    uint32_t msg_len = htonl(1 + current_bf.size());
    uint8_t id = 5;

    std::vector<uint8_t> packet;
    packet.resize(4 + 1 + current_bf.size());
    
    uint8_t* ptr = packet.data();
    memcpy(ptr, &msg_len, 4);
    ptr[4] = id;
    memcpy(ptr + 5, current_bf.data(), current_bf.size());

    send(sockfd, packet.data(), packet.size(), 0);
    //std::cout << "[Out] Inviato il mio BITFIELD (" << current_bf.size() << " byte)" << std::endl;
}


bool PeerConnection::readAll(void* buf, size_t len) {
    size_t total_read = 0;
    char* ptr = static_cast<char*>(buf);

    while (total_read < len) {
        ssize_t n = recv(sockfd, ptr + total_read, len - total_read, 0);
        
        if (n < 0) {
            if (errno == EINTR) continue; 
            return false; 
        }
        if (n == 0) return false; 

        total_read += n;
    }
    return true;
}