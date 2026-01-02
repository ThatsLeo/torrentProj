#ifndef TRACKERCLIENT_HPP
#define TRACKERCLIENT_HPP

#include <string>
#include <vector>
#include <cpr/cpr.h>
#include "../parser/Bnode.hpp"

struct Peer {
    std::string ip;
    u_int16_t port;
};

struct UDPTrackerInfo {
    std::string host;
    int port;
};


class TrackerClient {

public:

    TrackerClient(const std::string& announceUrl);
    
    // Esegue la richiesta e restituisce la lista dei peer
    std::vector<Peer> announce(const std::string& infoHash, const std::string& peerId, long long left, int listenPort, long long totalUploaded, long long totalDownloaded);

private:
    std::string url;
    std::string urlEncode(const std::string& binaryData);

    std::vector<Peer> announceHTTP(const std::string& infoHash, const std::string& peerId, long long left, int listenPort, long long totalUploaded, long long totalDownloaded);

    std::vector<Peer> announceUDP(const std::string& infoHash, const std::string& peerId, long long downloaded, long long left, long long uploaded, int event);
    UDPTrackerInfo parseUDPUrl(const std::string& url);

    std::vector<Peer> parseCompactPeers(const std::string& binaryPeers);
};

#endif