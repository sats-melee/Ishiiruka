#include "Core/Slippi/SatsArenaRoomClient.h"
#include "Core/Slippi/SatsArenaRoomPayments.h"

#include <chrono>
#include <curl/curl.h>
#include "Common/Common.h"

namespace
{
std::mutex active_mutex;
std::shared_ptr<SatsArenaRoomClient> active_client;
SatsArenaNetplay::Config create_config = [] {
    SatsArenaNetplay::Config config;
    config.player_count = 4;
    return config;
}();

bool Hex(const std::string& value, size_t length)
{
    return value.size() == length && value.find_first_not_of("0123456789ABCDEFabcdef") == std::string::npos;
}

bool ServerUrl(const std::string& url)
{
    const bool tls = url.compare(0, 8, "https://") == 0;
    const bool plain = url.compare(0, 7, "http://") == 0;
    if ((!tls && !plain) || url.find_first_of("@?#\\") != std::string::npos)
        return false;
    for (unsigned char c : url)
        if (c <= 32 || c == 127) return false;
    const auto start = tls ? 8 : 7;
    const auto end = url.find('/', start);
    const auto authority = url.substr(start, end == std::string::npos ? end : end - start);
    if (authority.empty()) return false;
    const auto close = authority.front() == '[' ? authority.find(']') : std::string::npos;
    if (authority.front() == '[' && close == std::string::npos) return false;
    const auto host = close == std::string::npos ? authority.substr(0, authority.find(':')) :
        authority.substr(0, close + 1);
    if (close != std::string::npos && close + 1 < authority.size() && authority[close + 1] != ':') return false;
    if (plain && host != "127.0.0.1" && host != "localhost" && host != "[::1]") return false;
    return true;
}

size_t Receive(char* bytes, size_t size, size_t count, void* opaque)
{
    auto& text = *static_cast<std::string*>(opaque);
    if (size && count > (32768 - text.size()) / size) return 0;
    text.append(bytes, size * count);
    return size * count;
}
}

SatsArenaRoomClient::SatsArenaRoomClient(bool create, const std::string& code, const std::string& endpoint,
                                       unsigned short localPort, const std::string& lanEndpoint)
{
    SatsArenaNetplay::Player parsed;
    m_localPort = localPort ? localPort : (SatsArenaNetplay::Endpoint(endpoint, parsed) ? parsed.port : 0);
    m_endpoint = endpoint;
    m_lanEndpoint = lanEndpoint;
    m_config = CreateConfig();
#ifdef SATS_ARENA
    m_trackerBaseline = SatsArena::GetTracker().Read().match_number;
#endif
    m_thread = std::thread(&SatsArenaRoomClient::Run, this, create, code, endpoint);
}

SatsArenaRoomClient::~SatsArenaRoomClient()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_curl) curl_easy_cleanup(m_curl);
}

SatsArenaRoomClient::Status SatsArenaRoomClient::Poll(SatsArenaNetplay::Room& room, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    room = m_room;
    error = m_error;
    return m_status;
}

std::string SatsArenaRoomClient::GetRoomCode()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_code;
}

std::shared_ptr<SatsArenaRoomClient> SatsArenaRoomClient::Active()
{
    std::lock_guard<std::mutex> lock(active_mutex);
    return active_client;
}

void SatsArenaRoomClient::SetActive(const std::shared_ptr<SatsArenaRoomClient>& client)
{
    std::shared_ptr<SatsArenaRoomClient> previous;
    {
        std::lock_guard<std::mutex> lock(active_mutex);
        previous = std::move(active_client);
        active_client = client;
    }
}

bool SatsArenaRoomClient::SetCreateConfig(const SatsArenaNetplay::Config& config)
{
    SatsArenaNetplay::Config validated;
    if (!SatsArenaNetplay::ParseConfig(SatsArenaNetplay::ConfigJson(config), validated)) return false;
    std::shared_ptr<SatsArenaRoomClient> previous;
    std::lock_guard<std::mutex> lock(active_mutex);
    if (active_client)
    {
        SatsArenaNetplay::Room room;
        std::string error;
        const auto status = active_client->Poll(room, error);
        if (status != Status::Closed && status != Status::Failed) return false;
    }
    previous = std::move(active_client);
    create_config = validated;
    return true;
}

SatsArenaNetplay::Config SatsArenaRoomClient::CreateConfig()
{
    std::lock_guard<std::mutex> lock(active_mutex);
    return create_config;
}

SatsArenaNetplay::Config SatsArenaRoomClient::GetConfig()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

nlohmann::json SatsArenaRoomClient::PaymentView()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_payment;
}

int SatsArenaRoomClient::LocalSlot()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_slot;
}

bool SatsArenaRoomClient::HasPayment()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_payment.is_object();
}

bool SatsArenaRoomClient::CanNextMatchLocked() const
{
    if (m_slot != 0 || m_reconnecting || !m_action.is_null() || m_status == Status::Closed || m_status == Status::Failed) return false;
    if (m_payment.is_null()) return m_matchComplete && !m_room.match_id.empty();
    if (m_payment["frozen"].get<bool>() || m_payment["phase"] != 4) return false;
    for (const auto& player : m_payment["players"])
        if (player["pending_sats"] != 0) return false;
    return true;
}

bool SatsArenaRoomClient::CanNextMatch()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return CanNextMatchLocked();
}

bool SatsArenaRoomClient::NextMatch()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!CanNextMatchLocked()) return false;
    m_action = {{"type", "next_match"}, {"match_id", m_payment.is_object() ?
        m_payment["match_id"].get<std::string>() : m_room.match_id}};
    m_error.clear();
    return true;
}

bool SatsArenaRoomClient::ExitRoom(bool close)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_slot < 0 || (close && m_slot != 0) || !m_action.is_null() ||
        m_status == Status::Closed || m_status == Status::Failed) return false;
    m_action = {{"type", close ? "close" : "leave"}};
    StopPeerPings();
    m_error.clear();
    return true;
}

bool SatsArenaRoomClient::LeaveRoom() { return ExitRoom(false); }
bool SatsArenaRoomClient::CloseRoom() { return ExitRoom(true); }
bool SatsArenaRoomClient::IsClosed()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status == Status::Closed;
}

void SatsArenaRoomClient::ReportPeerPing(unsigned slot, std::uint64_t rttUs)
{
    std::lock_guard<std::mutex> lock(m_pingMutex);
    if (m_pings.active && slot < m_pings.players.size())
        m_pings.players[slot] = {rttUs, std::chrono::steady_clock::now()};
}

void SatsArenaRoomClient::ClearPeerPing(int slot)
{
    std::lock_guard<std::mutex> lock(m_pingMutex);
    if (slot < 0) m_pings.players = {};
    else if (static_cast<unsigned>(slot) < m_pings.players.size()) m_pings.players[slot] = {};
}

SatsArenaRoomClient::PingSnapshot SatsArenaRoomClient::PeerPings()
{
    std::lock_guard<std::mutex> lock(m_pingMutex);
    return m_pings;
}

void SatsArenaRoomClient::StopPeerPings()
{
    std::lock_guard<std::mutex> lock(m_pingMutex);
    m_pings = {};
}

bool SatsArenaRoomClient::SetAddress(const std::string& address)
{
    if (address.empty() || address.size() > 127 || address.find_first_of("\r\n") != std::string::npos) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_reconnecting || !m_payment.is_object() || m_payment["frozen"].get<bool>() ||
        !(m_payment["phase"] == 1 || m_payment["phase"] == 2)) return false;
    m_address = address;
    return true;
}

void SatsArenaRoomClient::Fail(const std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_error = error;
    m_status = Status::Failed;
    StopPeerPings();
}

void SatsArenaRoomClient::Reconnecting()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_reconnecting = true;
    m_status = Status::Waiting;
    m_error = "Room server unavailable; reconnecting";
    if (m_payment.is_object()) m_payment["error"] = m_error;
}

bool SatsArenaRoomClient::Wait(unsigned milliseconds)
{
    for (unsigned elapsed = 0; elapsed < milliseconds && !m_stop; elapsed += 100)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return !m_stop;
}

bool SatsArenaRoomClient::Request(const std::string& path, const nlohmann::json* body,
                                nlohmann::json& response, std::string& error)
{
    if (!m_curl) m_curl = curl_easy_init();
    auto* curl = m_curl;
    m_httpStatus = 0;
    m_retryable = false;
    error.clear();
    if (!curl) { error = "Cannot initialize room connection"; return false; }
    // Reset request options while keeping the worker's TCP/TLS connection cache.
    curl_easy_reset(curl);
    std::string text;
    const auto url = m_server + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    // Plain HTTP is allowed only on loopback and must not go through a proxy.
    if (m_server.compare(0, 7, "http://") == 0) curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &text);
    auto* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!m_token.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + m_token).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    std::string payload;
    if (body)
    {
        payload = body->dump();
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    }
    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    m_httpStatus = status;
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
    curl_slist_free_all(headers);
    if (result != CURLE_OK)
    {
        m_retryable = result == CURLE_COULDNT_RESOLVE_PROXY || result == CURLE_COULDNT_RESOLVE_HOST ||
            result == CURLE_COULDNT_CONNECT || result == CURLE_OPERATION_TIMEDOUT ||
            result == CURLE_SEND_ERROR || result == CURLE_RECV_ERROR || result == CURLE_GOT_NOTHING ||
            result == CURLE_PARTIAL_FILE || result == CURLE_SSL_CONNECT_ERROR || result == CURLE_HTTP2;
        error = "Room server unavailable";
        return false;
    }
    if (status != 200)
    {
        m_retryable = status >= 500 && status < 600;
        error = status == 404 ? "Room not found or expired" :
                status == 409 ? "Room full, started, or incompatible" : "Room server rejected request";
        const auto rejected = nlohmann::json::parse(text, nullptr, false);
        std::string detail;
        if (SatsArenaNetplay::Text(rejected, "error", detail, 120)) error = detail;
        return false;
    }
    response = nlohmann::json::parse(text, nullptr, false);
    if (response.is_discarded()) { error = "Invalid room server response"; return false; }
    return true;
}

void SatsArenaRoomClient::Run(bool create, std::string code, std::string endpoint)
{
    const char* server = std::getenv("SATS_ARENA_ROOM_SERVER_URL");
    m_server = server ? server : "";
    while (!m_server.empty() && m_server.back() == '/') m_server.pop_back();
    if (!ServerUrl(m_server)) { Fail("Configure the room server URL (HTTPS or loopback HTTP)"); return; }
    if (!create && !SatsArenaNetplay::NormalizeRoomCode(code, code)) { Fail("Room code must contain eight letters A-F or digits"); return; }
    for (auto& c : code) if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
    SatsArenaNetplay::Player address;
    if (!SatsArenaNetplay::Endpoint(endpoint, address)) { Fail("Invalid LAN gameplay address"); return; }
    const char* name = std::getenv("SATS_ARENA_ROOM_NAME");
    nlohmann::json admission = {{"name", name && *name ? name : "Player"},
        {"endpoint", endpoint}, {"build", "arena-rooms-3:" + scm_rev_git_str}, {"rules", "arena-stock-1"}};
    if (!m_lanEndpoint.empty()) admission["lan_endpoint"] = m_lanEndpoint;
    if (create) admission["config"] = SatsArenaNetplay::ConfigJson(m_config);
    nlohmann::json response;
    std::string error;
    const auto path = create ? "/netplay/rooms" : "/netplay/rooms/" + code + "/join";
    if (!Request(path, &admission, response, error)) { Fail(error); return; }
    std::string token;
    std::string roomCode;
    if (!SatsArenaNetplay::Text(response, "token", token, 64) || !Hex(token, 64) ||
        !response.count("slot") || !response["slot"].is_number_integer() ||
        response["slot"] < 0 || response["slot"] > 3 || !response.count("room") ||
        !SatsArenaNetplay::Text(response["room"], "code", roomCode, 8) || !Hex(roomCode, 8))
    { Fail("Invalid room admission"); return; }
    SatsArenaNetplay::Config config;
    if (!response["room"].count("config") ||
        !SatsArenaNetplay::ParseConfig(response["room"]["config"], config) ||
        response["slot"] >= config.player_count)
    { Fail("Invalid room configuration"); return; }
    const int slot = response["slot"].get<int>();
    m_token = token;
    { std::lock_guard<std::mutex> lock(m_mutex); m_code = roomCode; m_slot = slot; m_config = config; }
    const std::string roomPath = "/netplay/rooms/" + roomCode;
    auto room = response["room"];
    std::string financialPath;
    bool roomAvailable = true;
    bool reportEnded = false;
    bool freeFinished = false;
    bool exited = false;
    bool firstRound = true;
    bool remoteClosed = false;
    auto remoteCloseDeadline = std::chrono::steady_clock::time_point::max();
    // Stop before the server's 120-second membership lease; payment deadlines stay server-owned.
    auto roomDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    std::uint64_t reportRevision = 0;
    std::uint64_t reportNext = 0;
#ifdef SATS_ARENA
    auto flushDeadline = std::chrono::steady_clock::time_point::max();
#endif
    while (true)
    {
        bool requestFailed = false;
        if (m_stop)
        {
#ifdef SATS_ARENA
            const auto match = SatsArena::GetTracker().Read();
            if (financialPath.empty() || reportEnded || !match.online || !match.game_ended ||
                !match.result_complete || match.match_number <= m_trackerBaseline) break;
            const auto now = std::chrono::steady_clock::now();
            if (flushDeadline == std::chrono::steady_clock::time_point::max())
                flushDeadline = now + std::chrono::seconds(3);
            if (now >= flushDeadline) break;
#else
            break;
#endif
        }
        if (!remoteClosed && std::chrono::steady_clock::now() >= roomDeadline)
        { Fail("Room connection lost; rejoin after checking settlement status"); break; }
        if (!room.is_object() || !room.count("players") || !room["players"].is_array() ||
            room["players"].empty() || room["players"].size() > config.player_count ||
            !room.count("config") || room["config"] != SatsArenaNetplay::ConfigJson(config) ||
            !room.count("match_id") ||
            !SatsArenaNetplay::Text(room, "code", code, 8) || code != roomCode ||
            !room.count("build") || room["build"] != admission["build"] ||
            !room.count("rules") || room["rules"] != admission["rules"])
        { Fail("Invalid or unsupported room roster"); break; }
        unsigned round = 1;
        if (room.count("round"))
        {
            if (!room["round"].is_number_integer() || room["round"] < 1 || room["round"] > 1000000)
            { Fail("Invalid room round"); break; }
            round = room["round"].get<unsigned>();
        }
        if (firstRound) { m_round = round; firstRound = false; }
        if (round < m_round || round > m_round + 1) { Fail("Room round changed unexpectedly"); break; }
        if (round != m_round)
        {
            // A new pot cannot replace money still being settled or reviewed.
            if (!financialPath.empty())
            {
                if (!Request(financialPath, nullptr, response, error))
                {
                    if (!m_retryable) { Fail(error); break; }
                    Reconnecting();
                    Wait(250);
                    continue;
                }
                if (!SatsArenaNetplay::Payment(response) ||
                    response["phase"] != 4 || response["frozen"].get<bool>())
                { Fail("Previous room match is not settled"); break; }
                bool pending = false;
                for (const auto& player : response["players"]) pending |= player["pending_sats"] != 0;
                if (pending) { Fail("Previous room payouts are still pending"); break; }
            }
            financialPath.clear();
            reportEnded = freeFinished = false;
            reportRevision = reportNext = 0;
#ifdef SATS_ARENA
            m_trackerBaseline = SatsArena::GetTracker().Read().match_number;
            flushDeadline = std::chrono::steady_clock::time_point::max();
#endif
            std::lock_guard<std::mutex> lock(m_mutex);
            m_round = round;
            m_status = Status::Waiting;
            m_payment = nullptr;
            m_action = nullptr;
            m_address.clear();
            m_error.clear();
        }
        if (room.count("match_complete") && !room["match_complete"].is_boolean())
        { Fail("Invalid room completion state"); break; }
        { std::lock_guard<std::mutex> lock(m_mutex); m_matchComplete = room.value("match_complete", false); }
        nlohmann::json roster = nlohmann::json::array();
        for (const auto& player : room["players"])
            if (player.is_object() && player.count("slot") && player.count("name") && player.count("endpoint"))
                roster.push_back({player["slot"], player["name"], player["endpoint"],
                    player.count("lan_endpoint") ? player["lan_endpoint"] : nlohmann::json(nullptr)});
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto signature = roster.dump();
            if (signature != m_rosterSignature) { m_rosterSignature = signature; ++m_rosterRevision; }
        }
        nlohmann::json payment = room.count("payment") ? room["payment"] : nlohmann::json(nullptr);
        if (!financialPath.empty())
        {
            if (Request(financialPath, nullptr, response, error)) payment = response;
            else
            {
                if (!m_retryable) { Fail(error); break; }
                Reconnecting();
                requestFailed = true;
                roomAvailable = false;
                std::lock_guard<std::mutex> lock(m_mutex);
                payment = m_payment;
                if (payment.is_object()) payment["error"] = error;
            }
        }
        if (!payment.is_null())
        {
            if (!SatsArenaNetplay::Payment(payment)) { Fail("Invalid payment state from room server"); break; }
            const auto id = payment["match_id"].get<std::string>();
            if (!financialPath.empty() && financialPath != "/netplay/matches/" + id)
            { Fail("Room payment identity changed"); break; }
            financialPath = "/netplay/matches/" + id;
            { std::lock_guard<std::mutex> lock(m_mutex); m_payment = payment; }
            std::string pendingAddress;
            { std::lock_guard<std::mutex> lock(m_mutex); pendingAddress.swap(m_address); }
            if (!pendingAddress.empty())
            {
                const nlohmann::json command = {{"type", "set_address"}, {"address", pendingAddress}};
                if (!Request(financialPath, &command, response, error))
                {
                    if (m_retryable) { Reconnecting(); requestFailed = true; roomAvailable = false; }
                    std::lock_guard<std::mutex> lock(m_mutex); m_payment["error"] = error;
                }
            }
#ifdef SATS_ARENA
            const auto match = SatsArena::GetTracker().Read();
            const auto next = payment["own_next_sequence"][slot].get<std::uint64_t>();
            if (!payment["frozen"].get<bool>() && !reportEnded && match.online &&
                match.match_number > m_trackerBaseline &&
                (reportRevision == 0 || match.total_credit_events >= next || match.game_ended || match.credit_stream_incomplete) &&
                (match.revision != reportRevision || next != reportNext))
            {
                nlohmann::json command;
                if (SatsArenaNetplay::Observation(match, id, next, command, error))
                {
                    if (Request(financialPath, &command, response, error))
                    {
                        reportRevision = match.revision;
                        reportNext = next;
                        reportEnded = !command["report"]["end"].is_null();
                    }
                    else if (m_retryable) { Reconnecting(); requestFailed = true; roomAvailable = false; }
                }
                if (!error.empty())
                { std::lock_guard<std::mutex> lock(m_mutex); m_payment["error"] = error; }
            }
#endif
        }
#ifdef SATS_ARENA
        else if (!room["match_id"].is_null() && !freeFinished)
        {
            const auto match = SatsArena::GetTracker().Read();
            if (match.online && match.game_ended && match.match_number > m_trackerBaseline &&
                room["match_id"] == match.match_id)
            {
                const nlohmann::json finish = {{"type", "finish"}, {"match_id", match.match_id}};
                if (Request(roomPath, &finish, response, error)) freeFinished = true;
                else
                {
                    if (m_retryable) { Reconnecting(); requestFailed = true; roomAvailable = false; }
                    else { std::lock_guard<std::mutex> lock(m_mutex); m_error = error; }
                }
            }
        }
#endif
        if (remoteClosed)
        {
            if (financialPath.empty() || reportEnded || (payment.is_object() && payment["phase"] == 4) ||
                std::chrono::steady_clock::now() >= remoteCloseDeadline)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status = Status::Closed;
                m_error = "Room closed or membership ended";
                exited = true;
                break;
            }
            Wait(100);
            continue;
        }
        nlohmann::json action;
        { std::lock_guard<std::mutex> lock(m_mutex); action = m_action; }
        if (!action.is_null())
        {
            bool flush = false;
#ifdef SATS_ARENA
            const auto match = SatsArena::GetTracker().Read();
            flush = action["type"] != "next_match" && !financialPath.empty() && !reportEnded &&
                match.online && match.game_ended && match.result_complete && match.match_number > m_trackerBaseline;
            if (flush)
            {
                const auto now = std::chrono::steady_clock::now();
                if (flushDeadline == std::chrono::steady_clock::time_point::max())
                    flushDeadline = now + std::chrono::seconds(3);
                flush = now < flushDeadline;
            }
#endif
            if (!flush)
            {
                if (!Request(roomPath, &action, response, error))
                {
                    if (m_retryable) { Reconnecting(); requestFailed = true; roomAvailable = false; }
                    else
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_error = error;
                        if (m_payment.is_object()) m_payment["error"] = error;
                        m_action = nullptr;
                        if (action["type"] != "next_match")
                        {
                            std::lock_guard<std::mutex> pingLock(m_pingMutex);
                            m_pings.active = true;
                        }
                    }
                }
                else if (action["type"] == "next_match")
                {
                    room = response;
                    continue;
                }
                else
                {
                    if (!financialPath.empty() && Request(financialPath, nullptr, response, error) &&
                        SatsArenaNetplay::Payment(response))
                    { std::lock_guard<std::mutex> lock(m_mutex); m_payment = response; }
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_status = Status::Closed;
                    m_error = action["type"] == "close" ? "Room closed" : "You left the room";
                    m_action = nullptr;
                    exited = true;
                    break;
                }
            }
        }
        if (roomAvailable && !room["match_id"].is_null() && room["players"].size() == config.player_count)
        {
            SatsArenaNetplay::Room parsed;
            if (!SatsArenaNetplay::Parse({{"slot", slot}, {"room", room}}, parsed, error)) { Fail(error); break; }
            if (parsed.players[slot].address != address.address || parsed.players[slot].port != address.port)
            { Fail("Room changed the local gameplay address"); break; }
            { std::lock_guard<std::mutex> lock(m_mutex); parsed.roster_revision = m_rosterRevision; m_room = parsed; m_status = Status::Started; }
        }
        else if (roomAvailable)
        {
            // The sidebar needs names before shared Start; gameplay still waits
            // for the complete descriptor parser and Status::Started below.
            SatsArenaNetplay::Room preview{};
            preview.slot = slot;
            preview.config = config;
            if (room["match_id"].is_string()) preview.match_id = room["match_id"].get<std::string>();
            preview.players.resize(config.player_count);
            unsigned seen = 0;
            bool namesValid = true;
            for (const auto& player : room["players"])
            {
                std::string name;
                if (!player.is_object() || !player.count("slot") || !player["slot"].is_number_integer() ||
                    player["slot"] < 0 || player["slot"] >= config.player_count ||
                    !SatsArenaNetplay::Text(player, "name", name, 64))
                { namesValid = false; break; }
                const auto index = player["slot"].get<unsigned>();
                if (seen & (1u << index)) { namesValid = false; break; }
                seen |= 1u << index;
                preview.players[index].name = name;
            }
            if (namesValid)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                preview.roster_revision = m_rosterRevision;
                m_room = std::move(preview);
                m_status = Status::Waiting;
            }
            bool own = false, ownReady = false;
            bool allReady = room["players"].size() == config.player_count;
            for (const auto& player : room["players"])
            {
                if (!player.is_object() || !player.count("slot") || !player["slot"].is_number_integer() ||
                    !player.count("ready") || !player["ready"].is_boolean())
                { error = "Invalid room player"; break; }
                if (player["slot"] == slot) { own = true; ownReady = player["ready"].get<bool>(); }
                allReady = allReady && player["ready"].get<bool>();
            }
            if (!own) { Fail("Invalid room player"); break; }
            bool funded = payment.is_null();
            if (!payment.is_null())
            {
                funded = !payment["frozen"].get<bool>() && payment["phase"] == 2;
                for (unsigned i = 0; i < config.player_count; ++i)
                    funded = funded && payment["players"][i]["verified"].get<bool>() &&
                        payment["players"][i]["paid_sats"] >= payment["invoice_sats"];
            }
            if (room["match_id"].is_null() && (!ownReady || (allReady && funded && slot == 0)))
            {
                const nlohmann::json command = allReady && funded && slot == 0 ? nlohmann::json{{"type", "start"}} :
                    nlohmann::json{{"type", "ready"}, {"ready", true}};
                if (!Request(roomPath, &command, response, error))
                {
                    if (m_retryable) { Reconnecting(); requestFailed = true; roomAvailable = false; }
                    else if (financialPath.empty()) { Fail(error); break; }
                }
            }
        }
        // A normal result gets a bounded final flush even when cleanup interrupts polling.
        if (!Wait(250)) continue;
        if (!Request(roomPath, nullptr, response, error))
        {
            if (m_httpStatus == 404 || m_httpStatus == 401)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status = Status::Waiting;
                m_error = "Room closed; finishing the last payment report";
                remoteClosed = true;
                StopPeerPings();
                roomAvailable = false;
                remoteCloseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                continue;
            }
            if (!m_retryable) { Fail(error); break; }
            Reconnecting();
            roomAvailable = false; // The financial record outlives the temporary room.
        }
        else
        {
            room = response;
            roomAvailable = true;
            if (!requestFailed)
            {
                roomDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_reconnecting)
                {
                    m_reconnecting = false;
                    m_error.clear();
                    if (m_payment.is_object()) m_payment.erase("error");
                }
            }
        }
        error.clear();
    }
    const nlohmann::json leave = {{"type", "leave"}};
    if (!exited) Request(roomPath, &leave, response, error);
}
