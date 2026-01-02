#include "Tracker.hpp"
#include <../parser/TorrentFile.hpp>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> 
#include <endian.h>
#include <unistd.h>


TrackerClient::TrackerClient(const std::string& announceUrl){
    this->url = announceUrl;
}

std::string TrackerClient::urlEncode(const std::string& binaryData) {
    
    char hexChars[] = "0123456789abcdef";
    std::string encoded;

    for (unsigned char c : binaryData){

        if (std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '~') {
            encoded += c;
        }

        else {

            encoded += '%';
            encoded += hexChars[c >> 4];
            encoded += hexChars[c & 0x0F];
        }
    }

    return encoded;
    
}


std::vector<Peer> TrackerClient::announce(const std::string& infoHash, const std::string& peerId, long long left, int listenPort, long long totalUploaded, long long totalDownloaded) {
    
    if (this->url.substr(0, 4) == "http") {
        return announceHTTP(infoHash, peerId, left, listenPort, totalUploaded, totalDownloaded);
    } else if (this->url.substr(0, 3) == "udp") {
        return announceUDP(infoHash, peerId, left, listenPort, totalUploaded, totalDownloaded);
    }

    return {};

}


std::vector<Peer> TrackerClient::announceHTTP(const std::string& infoHash, const std::string& peerId, long long left, int listenPort, long long totalUploaded, long long totalDownloaded) {
    
    cpr::Response r = cpr::Get(
    cpr::Url{this->url},
    cpr::Parameters{
            {"info_hash",  infoHash}, // cpr codifica automaticamente
            {"peer_id",    peerId},
            {"port",       std::to_string(listenPort)},
            {"uploaded",   std::to_string(totalUploaded)},
            {"downloaded", std::to_string(totalDownloaded)},
            {"left",       std::to_string(left)},
            {"compact",    "1"},
            {"numwant",    "50"} // Opzionale: chiedi esplicitamente 50 peer
        },
    cpr::Timeout{15000}
    );

    if (r.status_code != 200) {
        std::cerr << "Errore Tracker (HTTP " << r.status_code << "): " << r.error.message << std::endl;
        return {};
    }


    std::cout << "Risposta ricevuta dal Tracker!" << std::endl;

    char* ptr = const_cast<char*>(r.text.c_str());
    Bnode* root = TorrentFile::parse_element(ptr);

    for (const auto& pair : root->dict_val){
        
        if (pair.first == "peers"){

            return parseCompactPeers(pair.second->str_val);
        }
    }

    delete root;
    return {};
}


UDPTrackerInfo TrackerClient::parseUDPUrl(const std::string& url) {

    UDPTrackerInfo info;

    uint pos = 6; // Salta "udp://"

    for (; pos < url.size(); pos++){
        while (url[pos] != ':'){
            info.host += url[pos];
            pos++;
        }
        pos++; // Salta ':'

        std::string tempPort = "";
        while (url[pos] != '/'){
            tempPort += url[pos];
            pos++;
        }
        info.port = std::stoi(tempPort);
        break;

    }

    return info;
}


std::vector<Peer> TrackerClient::announceUDP(
    const std::string& infoHash, 
    const std::string& peerId, 
    long long downloaded, 
    long long left, 
    long long uploaded, 
    int event) 
{
    UDPTrackerInfo info = parseUDPUrl(this->url);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(info.host.c_str(), std::to_string(info.port).c_str(), &hints, &res) != 0) {
        throw std::runtime_error("DNS failed");
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    // Timeout per evitare che il programma si blocchi se il tracker è offline
    struct timeval tv;
    tv.tv_sec = 5; 
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // --- 1. CONNECTION REQUEST ---
    struct __attribute__((packed)) {
        uint64_t protocol_id; 
        uint32_t action;      
        uint32_t transaction_id;
    } conn_req;

    uint32_t current_transaction_id = std::rand();
    conn_req.protocol_id = htobe64(0x41727101980); 
    conn_req.action = htonl(0); // 0 = Connect
    conn_req.transaction_id = htonl(current_transaction_id);

    sendto(sockfd, &conn_req, sizeof(conn_req), 0, res->ai_addr, res->ai_addrlen);

    struct __attribute__((packed)) {
        uint32_t action;
        uint32_t transaction_id;
        uint64_t connection_id; 
    } conn_res;

    socklen_t addr_len = res->ai_addrlen;
    ssize_t rec = recvfrom(sockfd, &conn_res, sizeof(conn_res), 0, res->ai_addr, &addr_len);

    if (rec < 16 || ntohl(conn_res.transaction_id) != current_transaction_id) {
        close(sockfd);
        freeaddrinfo(res);
        throw std::runtime_error("Errore Connection ID o Timeout");
    }

    uint64_t connection_id = conn_res.connection_id; 

    // --- 2. ANNOUNCE REQUEST ---
    struct __attribute__((packed)) {
        uint64_t connection_id;
        uint32_t action;
        uint32_t transaction_id;
        uint8_t  info_hash[20];
        uint8_t  peer_id[20];
        uint64_t downloaded;
        uint64_t left;
        uint64_t uploaded;
        uint32_t event;
        uint32_t ip_address;
        uint32_t key;
        int32_t  num_want;
        uint16_t port;
    } ann_req;

    uint32_t ann_transaction_id = std::rand();

    ann_req.connection_id = connection_id; // Già in network byte order dalla risposta
    ann_req.action = htonl(1);             // 1 = Announce
    ann_req.transaction_id = htonl(ann_transaction_id);

    std::memcpy(ann_req.info_hash, infoHash.data(), 20);
    std::memcpy(ann_req.peer_id, peerId.data(), 20);

    // Variabili dinamiche
    ann_req.downloaded = htobe64(static_cast<uint64_t>(downloaded));
    ann_req.left       = htobe64(static_cast<uint64_t>(left));
    ann_req.uploaded   = htobe64(static_cast<uint64_t>(uploaded));
    
    /* Eventi UDP:
       0: none; 1: completed; 2: started; 3: stopped 
    */
    ann_req.event      = htonl(event);          
    ann_req.ip_address = htonl(0);          
    ann_req.key        = htonl(std::rand());
    ann_req.num_want   = htonl(-1);         
    ann_req.port       = htons(6881);       

    sendto(sockfd, &ann_req, sizeof(ann_req), 0, res->ai_addr, res->ai_addrlen);

    // --- 3. RECEIVE PEERS ---
    uint8_t buffer[2048]; 
    addr_len = res->ai_addrlen;
    rec = recvfrom(sockfd, buffer, sizeof(buffer), 0, res->ai_addr, &addr_len);

    if (rec < 20) {
        freeaddrinfo(res);
        close(sockfd);
        throw std::runtime_error("Risposta Announce incompleta");
    }

    uint32_t res_action = ntohl(*(uint32_t*)(buffer + 0));
    uint32_t res_trans  = ntohl(*(uint32_t*)(buffer + 4));

    if (res_action != 1 || res_trans != ann_transaction_id) {
        freeaddrinfo(res);
        close(sockfd);
        throw std::runtime_error("Transazione Announce non valida");
    }

    std::string peersBinary((char*)(buffer + 20), rec - 20);
    std::vector<Peer> listaPeer = parseCompactPeers(peersBinary);

    freeaddrinfo(res);
    close(sockfd);
    return listaPeer;
}

std::vector<Peer> TrackerClient::parseCompactPeers(const std::string& binaryPeers) {

    std::vector<Peer> peers;
    size_t numPeers = binaryPeers.size() / 6;

    for (size_t i = 0; i < numPeers; i++) {

        Peer peer;
        uint8_t ipBytes[4];
        uint8_t portBytes[2];

        std::memcpy(ipBytes, binaryPeers.data() + i * 6, 4);
        std::memcpy(portBytes, binaryPeers.data() + i * 6 + 4, 2);

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, ipBytes, ipStr, INET_ADDRSTRLEN);
        peer.ip = std::string(ipStr);

        peer.port = (portBytes[0] << 8) | portBytes[1];

        peers.push_back(peer);

    }

    return peers;
}