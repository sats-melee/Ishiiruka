#include "Common/Common.h"
#include "Common/Logging/Log.h"

#include "SlippiUser.h"
#include "SatsArenaNetplayRoom.h"

#include "SlippiRustExtensions.h"

// Takes a RustChatMessages pointer and extracts messages from them, then
// frees the underlying memory safely.
std::vector<std::string> ConvertChatMessagesFromRust(RustChatMessages *rsMessages)
{
	std::vector<std::string> chatMessages;

	for (int i = 0; i < rsMessages->len; i++)
	{
		std::string message = std::string(rsMessages->data[i]);
		chatMessages.push_back(message);
	}

	slprs_user_free_messages(rsMessages);

	return chatMessages;
}

SlippiUser::SlippiUser(uintptr_t rs_exi_device_ptr)
{
	slprs_exi_device_ptr = rs_exi_device_ptr;
	if (SatsArenaNetplay::Enabled())
	{
		m_arenaUserInfo.uid = "arena-room-local";
		m_arenaUserInfo.displayName = "Arena Player";
		const char* roomName = std::getenv("SATS_ARENA_ROOM_NAME");
		if (roomName && *roomName && std::string(roomName).size() <= 31)
			m_arenaUserInfo.displayName = roomName;
		m_arenaUserInfo.connectCode = "ARENA#0";
		m_arenaUserInfo.latestVersion = "0.0.0";
		SatsArenaNetplay::Room room;
		std::string error;
		if (SatsArenaNetplay::Load(room, error))
		{
			m_arenaUserInfo.uid = "arena-lan-" + std::to_string(room.slot);
			m_arenaUserInfo.displayName = room.players[room.slot].name;
			m_arenaUserInfo.connectCode = "ARENA#" + std::to_string(room.slot);
			m_arenaUserInfo.port = room.slot + 1;
		}
	}
}

SlippiUser::~SlippiUser() {}

bool SlippiUser::AttemptLogin()
{
	return SatsArenaNetplay::Enabled() || slprs_user_attempt_login(slprs_exi_device_ptr);
}

void SlippiUser::OpenLogInPage()
{
	if (!SatsArenaNetplay::Enabled())
		slprs_user_open_login_page(slprs_exi_device_ptr);
}

void SlippiUser::ListenForLogIn()
{
	if (!SatsArenaNetplay::Enabled())
		slprs_user_listen_for_login(slprs_exi_device_ptr);
}

bool SlippiUser::UpdateApp()
{
	return !SatsArenaNetplay::Enabled() && slprs_user_update_app(slprs_exi_device_ptr);
}

void SlippiUser::LogOut()
{
	if (!SatsArenaNetplay::Enabled())
		slprs_user_logout(slprs_exi_device_ptr);
}

void SlippiUser::OverwriteLatestVersion(std::string version)
{
	slprs_user_overwrite_latest_version(slprs_exi_device_ptr, version.c_str());
}

SlippiUser::UserInfo SlippiUser::GetUserInfo()
{
	if (SatsArenaNetplay::Enabled())
		return m_arenaUserInfo;
	SlippiUser::UserInfo userInfo;

	RustUserInfo *info = slprs_user_get_info(slprs_exi_device_ptr);
	userInfo.uid = std::string(info->uid);
	userInfo.playKey = std::string(info->play_key);
	userInfo.displayName = std::string(info->display_name);
	userInfo.connectCode = std::string(info->connect_code);
	userInfo.latestVersion = std::string(info->latest_version);
	slprs_user_free_info(info);

	return userInfo;
}

std::vector<std::string> SlippiUser::GetDefaultChatMessages()
{
	RustChatMessages *chatMessages = slprs_user_get_default_messages(slprs_exi_device_ptr);
	return ConvertChatMessagesFromRust(chatMessages);
}

std::vector<std::string> SlippiUser::GetUserChatMessages()
{
	RustChatMessages *chatMessages = slprs_user_get_messages(slprs_exi_device_ptr);
	return ConvertChatMessagesFromRust(chatMessages);
}

bool SlippiUser::IsLoggedIn()
{
	return SatsArenaNetplay::Enabled() || slprs_user_get_is_logged_in(slprs_exi_device_ptr);
}
