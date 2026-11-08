#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <curl/curl.h>
#include "Core/Slippi/SatsArenaNetplayRoom.h"

// One room membership, retained while the peer connection is in use.
class SatsArenaRoomClient
{
public:
    struct PeerPing
    {
        std::uint64_t rttUs = 0;
        std::chrono::steady_clock::time_point received = {};
        unsigned Milliseconds(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const
        {
            if (received == std::chrono::steady_clock::time_point{} || now < received ||
                now - received >= std::chrono::seconds(3)) return 0;
            const auto ms = rttUs / 1000 + (rttUs % 1000 != 0);
            return static_cast<unsigned>(ms ? ms : 1);
        }
    };
    struct PingSnapshot
    {
        bool active = false;
        std::array<PeerPing, 4> players{};
        unsigned WorstMilliseconds(int localSlot, unsigned playerCount,
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const
        {
            unsigned worst = 0;
            if (active)
                for (unsigned slot = 0; slot < playerCount && slot < players.size(); ++slot)
                    if (static_cast<int>(slot) != localSlot)
                    {
                        const auto ms = players[slot].Milliseconds(now);
                        if (ms > worst) worst = ms;
                    }
            return worst;
        }
    };
    enum class Status { Waiting, Started, Failed, Closed };
    SatsArenaRoomClient(bool create, const std::string& code, const std::string& endpoint,
                       unsigned short localPort = 0, const std::string& lanEndpoint = "");
    ~SatsArenaRoomClient();
    Status Poll(SatsArenaNetplay::Room& room, std::string& error);
    std::string GetRoomCode();
    static std::shared_ptr<SatsArenaRoomClient> Active();
    static void SetActive(const std::shared_ptr<SatsArenaRoomClient>& client);
    static bool SetCreateConfig(const SatsArenaNetplay::Config& config);
    static SatsArenaNetplay::Config CreateConfig();
    SatsArenaNetplay::Config GetConfig();
    nlohmann::json PaymentView();
    int LocalSlot();
    bool SetAddress(const std::string& address);
    bool HasPayment();
    bool CanNextMatch();
    bool NextMatch();
    bool LeaveRoom();
    bool CloseRoom();
    bool IsClosed();
    void ReportPeerPing(unsigned slot, std::uint64_t rttUs);
    void ClearPeerPing(int slot = -1);
    PingSnapshot PeerPings();
    unsigned short LocalPort() const { return m_localPort; }
    const std::string& GameplayEndpoint() const { return m_endpoint; }
    const std::string& LanEndpoint() const { return m_lanEndpoint; }

private:
    void Run(bool create, std::string code, std::string endpoint);
    bool Request(const std::string& path, const nlohmann::json* body, nlohmann::json& response, std::string& error);
    void Fail(const std::string& error);
    void Reconnecting();
    bool Wait(unsigned milliseconds);
    bool CanNextMatchLocked() const;
    bool ExitRoom(bool close);
    void StopPeerPings();

    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    std::mutex m_mutex;
    Status m_status = Status::Waiting;
    std::string m_code;
    std::string m_error;
    SatsArenaNetplay::Room m_room{};
    std::string m_server;
    std::string m_token;
    unsigned short m_localPort = 0;
    std::string m_endpoint;
    std::string m_lanEndpoint;
    int m_slot = -1;
    SatsArenaNetplay::Config m_config;
    nlohmann::json m_payment = nullptr;
    std::string m_address;
    std::uint64_t m_trackerBaseline = 0;
    unsigned m_round = 1;
    bool m_matchComplete = false;
    nlohmann::json m_action = nullptr;
    CURL* m_curl = nullptr;
    long m_httpStatus = 0;
    bool m_retryable = false;
    bool m_reconnecting = false;
    std::string m_rosterSignature;
    std::uint64_t m_rosterRevision = 0;
    std::mutex m_pingMutex;
    PingSnapshot m_pings{true, {}};
};
