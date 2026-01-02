cmake_minimum_required(VERSION 3.15)
project(TorrentClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.11.0
)
FetchContent_MakeAvailable(cpr)

# FORZIAMO il compilatore a guardare dentro la cartella degli header di CPR
include_directories(
    parser
    peerID
    TrackerClient
    ${cpr_SOURCE_DIR}/include  # <--- Questo è il "percorso assoluto" dinamico
)

add_executable(torrent_app 
    main.cpp
    parser/Bnode.cpp
    parser/TorrentFile.cpp
    peerID/peer.cpp
    TrackerClient/Tracker.cpp
    PeerConnection/peerConnection.cpp
    PieceManager/pieceManager.cpp
)

# Assicurati che il nome del target sia corretto
target_link_libraries(torrent_app PRIVATE cpr::cpr)