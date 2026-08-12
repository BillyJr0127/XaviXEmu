// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "core/drgqst_core.h"
#include "core/drgqst_state.h"
#include "cursor_presentation.h"
#include "persistence.h"
#include "resource.h"
#include "rom_loader.h"
#include "screenshot.h"
#include "win_audio.h"
#include "xavix2/xavix2_machine.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum
{
	FRAME_WIDTH = 256,
	FRAME_HEIGHT = 224,
	DEFAULT_SCALE = 3,
	ID_EMULATION_TIMER = 1,
	ID_FILE_OPEN = 100,
	ID_FILE_SCREENSHOT,
	ID_FILE_EXIT,
	ID_STATE_SAVE,
	ID_STATE_LOAD,
	ID_VIEW_SCALE_1,
	ID_VIEW_SCALE_2,
	ID_VIEW_SCALE_3,
	ID_VIEW_SCALE_4,
	ID_VIEW_MAXIMIZE,
	ID_VIEW_STRETCH_4_3,
	ID_VIEW_FULLSCREEN,
	ID_LANGUAGE_ZH_TW,
	ID_LANGUAGE_ENGLISH,
	ID_HELP_ABOUT
};

enum interface_language
{
	LANGUAGE_ZH_TW,
	LANGUAGE_ENGLISH
};

enum window_status
{
	WINDOW_STATUS_IDLE,
	WINDOW_STATUS_RUNNING,
	WINDOW_STATUS_STATE_SAVED,
	WINDOW_STATUS_STATE_LOADED,
	WINDOW_STATUS_SCREENSHOT_SAVED
};

typedef struct interface_strings
{
	const wchar_t *window_title_idle;
	const wchar_t *window_title_running;
	const wchar_t *window_title_state_saved;
	const wchar_t *window_title_state_loaded;
	const wchar_t *window_title_screenshot_saved;
	const wchar_t *menu_file;
	const wchar_t *menu_open;
	const wchar_t *menu_screenshot;
	const wchar_t *menu_exit;
	const wchar_t *menu_state;
	const wchar_t *menu_save_state;
	const wchar_t *menu_load_state;
	const wchar_t *menu_view;
	const wchar_t *menu_scale_1;
	const wchar_t *menu_scale_2;
	const wchar_t *menu_scale_3;
	const wchar_t *menu_scale_4;
	const wchar_t *menu_maximize;
	const wchar_t *menu_stretch_4_3;
	const wchar_t *menu_fullscreen;
	const wchar_t *menu_language;
	const wchar_t *menu_zh_tw;
	const wchar_t *menu_english;
	const wchar_t *menu_help;
	const wchar_t *menu_about;
	const wchar_t *about_title;
	const wchar_t *about_text;
	const wchar_t *about_facebook_button;
	const wchar_t *about_support_text;
	const wchar_t *about_subscribe_button;
	const wchar_t *about_close_button;
	const wchar_t *open_filter;
	const wchar_t *eeprom_save_title;
	const wchar_t *eeprom_save_error;
	const wchar_t *state_save_title;
	const wchar_t *state_save_memory_error;
	const wchar_t *state_save_error;
	const wchar_t *state_load_title;
	const wchar_t *state_load_memory_error;
	const wchar_t *state_load_error;
	const wchar_t *state_incompatible_error;
	const wchar_t *rom_open_title;
	const wchar_t *rom_open_error;
	const wchar_t *core_initialize_error;
	const wchar_t *screenshot_save_title;
	const wchar_t *screenshot_save_error;
	const wchar_t *storage_directory_error;
	const wchar_t *link_open_title;
	const wchar_t *link_open_error;
} interface_strings;

typedef struct display_viewport
{
	int x;
	int y;
	int width;
	int height;
	int scale;
} display_viewport;

static const wchar_t WINDOW_CLASS_NAME[] = L"XaviXEmuWindow";
static const wchar_t FACEBOOK_URL[] =
	L"https://www.facebook.com/61579382638861/";
static const wchar_t SUBSCRIBE_URL[] =
	L"https://www.facebook.com/61579382638861/subscribe/";
static const interface_strings INTERFACE_TEXT[] =
{
	{
		L"XaviXEmu",
		L"XaviXEmu - 遊戲執行中",
		L"XaviXEmu - 即時存檔完成",
		L"XaviXEmu - 即時存檔已讀取",
		L"XaviXEmu - 截圖已儲存",
		L"檔案(&F)",
		L"開啟 ROM ZIP(&O)...",
		L"抓取畫面(&P)\tF8",
		L"結束(&X)",
		L"存檔(&S)",
		L"即時存檔(&S)\tF5",
		L"讀取即時存檔(&L)\tF7",
		L"顯示(&V)",
		L"1 倍視窗(&1)",
		L"2 倍視窗(&2)",
		L"3 倍視窗(&3)",
		L"4 倍視窗(&4)",
		L"最大化視窗(&M)",
		L"4:3 顯示比例(&A)",
		L"全螢幕(&F)\tAlt+Enter",
		L"語言(&L)",
		L"繁體中文(&T)",
		L"English (&E)",
		L"說明(&H)",
		L"關於 XaviXEmu(&A)",
		L"關於 XaviXEmu",
		L"XaviXEmu 是一款 XaviX模擬器\r\n"
		L"目前支援：\r\n"
		L"剣神ドラゴンクエスト 甦りし伝説の剣\r\n"
		L"ONE PIECE パンチバトル\r\n"
		L"闘印奥義 陰陽大戦記 ～目指せ最強闘神士～\r\n"
		L"The Lord of the Rings: Warrior of Middle-Earth\r\n"
		L"Star Wars Saga Edition: Lightsaber Battle Game\r\n"
		L"Star Wars Saga Edition: Lightsaber Battle Game (Japan)\r\n"
		L"Ham-chans Dai Shuugou Dance Surunoda! Hashirunoda!（實驗支援）\r\n"
		L"TV-PC Doraemon（實驗支援）\r\n"
		L"NARUTO 忍者体感 ～だってばよ～（實驗支援）\r\n\r\n"
		L"航海王：左鍵左拳／○，右鍵右拳／×，同按雙拳\r\n"
		L"索隆：按住右鍵後左右或斜向拖曳來揮劍\r\n"
		L"陰陽大戰記：移動滑鼠畫印；左鍵○，右鍵×，空白鍵背面反光\r\n"
		L"勇者鬥惡龍：左鍵防禦，右鍵超必\r\n"
		L"魔戒／星際大戰：移動滑鼠揮劍與操作遊戲游標\r\n"
		L"哈姆太郎：左／右鍵搖左右鈴，空白鍵搖雙鈴，Enter／中鍵確認\r\n"
		L"TV-PC 哆啦A夢：滑鼠點選；上下移動或方向鍵／WASD 操作遊戲\r\n\r\n"
		L"Billy Jr",
		L"Billy Jr. 的模擬器世界",
		L"如果願意支持我的話\r\n"
		L"也歡迎訂閱我的粉專",
		L"訂閱粉專（可隨時取消）",
		L"確定",
		L"支援的 ROM ZIP (*.zip)\0*.zip\0所有檔案 (*.*)\0*.*\0\0",
		L"無法儲存遊戲進度",
		L"無法寫入遊戲內建存檔，請檢查儲存空間與資料夾權限。",
		L"即時存檔失敗",
		L"記憶體不足，無法建立即時存檔。",
		L"無法寫入即時存檔，請檢查儲存空間與資料夾權限。",
		L"讀取即時存檔失敗",
		L"記憶體不足，無法讀取即時存檔。",
		L"找不到或無法讀取即時存檔。",
		L"即時存檔不相容或已損壞。",
		L"無法開啟 ROM",
		L"無法開啟這個 ROM ZIP。請確認已選擇 XaviXEmu 支援的 ROM。",
		L"無法啟動模擬器核心。",
		L"抓圖失敗",
		L"無法將 PNG 圖片儲存到程式旁的 snap 資料夾。",
		L"無法取得 XaviXEmu.exe 所在的資料夾。",
		L"無法開啟網頁",
		L"無法使用預設瀏覽器開啟這個連結。"
	},
	{
		L"XaviXEmu",
		L"XaviXEmu - game running",
		L"XaviXEmu - state saved",
		L"XaviXEmu - state loaded",
		L"XaviXEmu - screenshot saved",
		L"&File",
		L"&Open ROM ZIP...",
		L"Save &screenshot\tF8",
		L"E&xit",
		L"&State",
		L"&Save state\tF5",
		L"&Load state\tF7",
		L"&View",
		L"&1x window",
		L"&2x window",
		L"&3x window",
		L"&4x window",
		L"&Maximize window",
		L"&4:3 display aspect",
		L"&Fullscreen\tAlt+Enter",
		L"&Language",
		L"繁體中文 (&T)",
		L"&English",
		L"&Help",
		L"&About XaviXEmu",
		L"About XaviXEmu",
		L"XaviXEmu is a XaviX emulator.\r\n"
		L"Currently supported:\r\n"
		L"Kenshin Dragon Quest: Yomigaerishi Densetsu no Ken\r\n"
		L"One Piece Punch Battle\r\n"
		L"Touin Ougi Onmyou Taisenki\r\n"
		L"The Lord of the Rings: Warrior of Middle-Earth\r\n"
		L"Star Wars Saga Edition: Lightsaber Battle Game\r\n"
		L"Star Wars Saga Edition: Lightsaber Battle Game (Japan)\r\n"
		L"Ham-chans Dai Shuugou Dance Surunoda! Hashirunoda! (experimental)\r\n"
		L"TV-PC Doraemon (experimental)\r\n"
		L"NARUTO Ninja Taikan: Dattebayo (experimental)\r\n\r\n"
		L"One Piece: left-click = left punch/O; right-click = right punch/X.\r\n"
		L"Press both for a double punch.\r\n"
		L"Zoro: hold right-click and drag horizontally or diagonally.\r\n"
		L"Onmyou Taisenki: move to draw seals; left = O, right = X, Space = back.\r\n"
		L"Dragon Quest: left-click to guard; right-click for the special.\r\n"
		L"LOTR / Star Wars: move the mouse to swing and control the in-game cursor.\r\n"
		L"Ham-chans: left/right = shake each bell; Space = both; Enter/middle = confirm.\r\n"
		L"TV-PC Doraemon: click to select; move vertically or use arrows/WASD in games.\r\n\r\n"
		L"Billy Jr",
		L"Billy Jr.'s Emulator World",
		L"If you'd like to support my work,\r\n"
		L"you're welcome to subscribe to my page.",
		L"Subscribe (cancel anytime)",
		L"OK",
		L"Supported ROM ZIP (*.zip)\0*.zip\0All files (*.*)\0*.*\0\0",
		L"The in-game save could not be stored",
		L"The in-game save could not be written. Check the available storage and folder permissions.",
		L"Save state failed",
		L"There is not enough memory to save the state.",
		L"The state could not be written. Check the available storage and folder permissions.",
		L"Load state failed",
		L"There is not enough memory to load the state.",
		L"The state could not be found or read.",
		L"The state file is incompatible or damaged.",
		L"ROM could not be opened",
		L"This ROM ZIP could not be opened. Select a ROM supported by XaviXEmu.",
		L"The emulator core could not be initialized.",
		L"Screenshot failed",
		L"The PNG image could not be saved to the snap folder beside the executable.",
		L"The folder containing XaviXEmu.exe could not be determined.",
		L"Web page could not be opened",
		L"The link could not be opened with the default web browser."
	}
};
static const uint8_t DRGQST_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x16, 0x77, 0xe8, 0x1c, 0xed, 0xcf, 0x34, 0x9d, 0xe7, 0xbf,
	0x09, 0x1a, 0x23, 0x2d, 0xc8, 0x2c, 0x64, 0x24, 0xef, 0xba
};
static const uint8_t BAN_ONEP_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xdb, 0x85, 0xf6, 0xcc, 0x48, 0xd7, 0x7c, 0x5a, 0x49, 0x67,
	0xb9, 0xb8, 0xe2, 0x99, 0x91, 0x67, 0xe3, 0xdf, 0xc8, 0xc8
};
static const uint8_t BAN_OMT_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xd0, 0xcf, 0x13, 0x45, 0xb7, 0x65, 0xd6, 0x6c, 0xa9, 0xa0,
	0x87, 0x0e, 0xe6, 0xd0, 0xe3, 0xcc, 0xd8, 0x4a, 0x8c, 0x0b
};
static const uint8_t TTV_LOTR_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x26, 0x4a, 0x9d, 0x43, 0x27, 0xaf, 0x0a, 0x07, 0x58, 0x41,
	0xad, 0x61, 0x29, 0xdb, 0x67, 0xd8, 0x2c, 0xf7, 0x41, 0xf1
};
static const uint8_t TTV_SW_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x1e, 0xd8, 0xd5, 0x56, 0xf3, 0x1b, 0x41, 0x82, 0x25, 0x9c,
	0xa8, 0xc7, 0x66, 0xd6, 0x0c, 0x82, 0x4d, 0x8d, 0x97, 0x44
};
static const uint8_t TTV_SWJ_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x40, 0x6f, 0x0b, 0xcc, 0xb0, 0x1c, 0xd4, 0xa2, 0x6f, 0xe4,
	0xa5, 0x67, 0x5d, 0x7e, 0xbe, 0xcc, 0x78, 0xc5, 0x81, 0x47
};
static const uint8_t TTV_MX_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x13, 0x7f, 0x97, 0xd7, 0xd8, 0x57, 0x69, 0x7a, 0x13, 0xe0,
	0xc8, 0x98, 0x45, 0x09, 0x99, 0x4d, 0xc7, 0xbc, 0x5f, 0xc5
};
static const uint8_t TOM_JUMP_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xbc, 0xa7, 0x53, 0x5b, 0xaa, 0x6a, 0x54, 0xad, 0x3e, 0xe0,
	0x92, 0x9b, 0xd3, 0xb7, 0x4a, 0x22, 0xcb, 0x51, 0x39, 0xda
};
static const uint8_t EPO_SDB_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x47, 0xa9, 0x68, 0x22, 0xd4, 0xd7, 0xd6, 0xa0, 0xf6, 0xbe,
	0x5c, 0xd7, 0x29, 0xc3, 0x74, 0x7d, 0xba, 0xb6, 0x59, 0x79
};
static const uint8_t EPO_BOWL_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xeb, 0xe3, 0x79, 0x21, 0x72, 0xdc, 0x43, 0x90, 0x4b, 0x92,
	0x26, 0xbe, 0xb2, 0x7f, 0x1d, 0xa8, 0x9d, 0x23, 0x88, 0xcc
};
static const uint8_t TAK_CHQ_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xa3, 0x08, 0x84, 0xda, 0x55, 0x54, 0x48, 0x3e, 0xbf, 0xd0,
	0x00, 0x9c, 0xf5, 0xdd, 0x17, 0x68, 0xbe, 0x8a, 0x99, 0xcb
};
static const uint8_t EPO_EBOX_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x7f, 0x7b, 0x61, 0x3f, 0x0a, 0xb8, 0xf4, 0x3f, 0x5c, 0xad,
	0x0d, 0x13, 0xde, 0x53, 0x89, 0x21, 0xe7, 0x7c, 0xae, 0x9c
};
static const uint8_t EPO_ES2J_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xad, 0x52, 0x44, 0x9f, 0xfc, 0x13, 0xaf, 0x5f, 0x4c, 0x67,
	0xb2, 0xc3, 0xcf, 0x43, 0x8e, 0x7e, 0xcd, 0x80, 0xb9, 0xfb
};
static const uint8_t EPO_HAMC_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xed, 0x01, 0x09, 0x6e, 0xbb, 0x63, 0xb7, 0x22, 0x67, 0xad,
	0x7e, 0x0b, 0x21, 0x15, 0x22, 0x4b, 0xba, 0xb6, 0x40, 0x11
};
static const uint8_t EPO_HAMD_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xc6, 0x1d, 0x43, 0x6d, 0x6b, 0x80, 0x37, 0x17, 0xb8, 0xc8,
	0x4d, 0x20, 0x22, 0x49, 0x93, 0x80, 0xf7, 0x1c, 0xce, 0xd8
};
static const uint8_t TVPC_DOR_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x98, 0xfa, 0x86, 0xf8, 0x5e, 0x00, 0xaa, 0x40, 0xe7, 0xa5,
	0x85, 0xff, 0x0b, 0xc9, 0x30, 0xcb, 0x5c, 0xa8, 0x83, 0x62
};
static const uint8_t TVPC_HAM_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x59, 0x98, 0xc0, 0x32, 0x92, 0xa1, 0x61, 0x07, 0xd0, 0xd7,
	0xae, 0x00, 0xf7, 0x76, 0x77, 0x58, 0x26, 0x80, 0xf3, 0x23
};
static const uint8_t TVPC_HK_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x29, 0xa2, 0x84, 0xb9, 0x07, 0xab, 0xec, 0x17, 0x5d, 0x42,
	0x89, 0xd2, 0x90, 0x49, 0x0a, 0xf1, 0x7a, 0x2a, 0x96, 0x3f
};

static drgqst_rom_image g_rom;
static drgqst_core *g_core;
static xavix2_machine_t *g_xavix2;
static win_audio g_audio_output;
static uint32_t g_idle_framebuffer[FRAME_WIDTH * FRAME_HEIGHT];
static const uint32_t *g_framebuffer = g_idle_framebuffer;
static unsigned g_frame_width = FRAME_WIDTH;
static unsigned g_frame_height = FRAME_HEIGHT;
static unsigned g_frame_stride = FRAME_WIDTH;
static HMENU g_menu;
static enum interface_language g_language = LANGUAGE_ZH_TW;
static enum window_status g_window_status = WINDOW_STATUS_IDLE;
static int g_fullscreen;
static int g_stretch_4_3 = 1;
static int g_window_scale = DEFAULT_SCALE;
static WINDOWPLACEMENT g_windowed_placement;
static LONG_PTR g_windowed_style;
static HDC g_capture_dc;
static HBITMAP g_capture_bitmap;
static HGDIOBJ g_capture_old_bitmap;
static int g_capture_width;
static int g_capture_height;
static LARGE_INTEGER g_counter_frequency;
static LONGLONG g_next_frame_counter;
static LONGLONG g_frame_counter_step;
static int g_timing_diagnostics;
static LONGLONG g_timing_window_counter;
static LONGLONG g_timing_core_counter;
static uint64_t g_timing_frames;
static uint64_t g_timing_dropped_frames;
static uint64_t g_timing_guest_cycles;
static uint64_t g_timing_interrupts;
static uint8_t g_mouse_x = 0x80;
static uint8_t g_mouse_y = 0x80;
static uint8_t g_tvpc_mouse_counter_x;
static uint8_t g_tvpc_mouse_counter_y;
static int g_tvpc_mouse_position_valid;
static drgqst_cursor_presentation g_cursor_presentation;
static int g_left_button;
static int g_right_button;
static int g_naruto_joined_hands;
static unsigned g_naruto_execute_delay;
static unsigned g_naruto_execute_frames;
static int g_omt_backside;
static int g_ttv_spin_held;
static unsigned g_ttv_spin_phase;
static unsigned g_ttv_sw_motion_frames;
static unsigned g_hamd_left_pulse_frames;
static unsigned g_hamd_right_pulse_frames;
static unsigned g_hamd_confirm_frames;
static uint8_t g_tvpc_keyboard_rows[8];
static uint8_t g_tvpc_mouse_key_pending;
static uint8_t g_tvpc_mouse_key_active;
static uint8_t g_ban_onep_menu_input;
static unsigned g_ban_onep_menu_input_frames;
static uint32_t g_eeprom_generation;
static unsigned g_eeprom_settle_frames;
static uint32_t g_parallel_nvram_generation;
static unsigned g_parallel_nvram_settle_frames;
static int g_parallel_nvram_save_pending;
static int g_eeprom_error_shown;
static wchar_t g_executable_directory[MAX_PATH];

static int emulator_loaded(void)
{
	return g_core != NULL || g_xavix2 != NULL;
}

static const interface_strings *interface_text(void)
{
	return &INTERFACE_TEXT[g_language];
}

static int rom_uses_camera(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_BAN_ONEP || kind == DRGQST_ROM_BAN_OMT ||
		kind == DRGQST_ROM_TTV_LOTR || kind == DRGQST_ROM_TTV_SW ||
		kind == DRGQST_ROM_TTV_SWJ || kind == DRGQST_ROM_EPO_BOWL;
}

static int rom_uses_digital_direction_input(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_TTV_MX || kind == DRGQST_ROM_TOM_JUMP ||
		kind == DRGQST_ROM_EPO_EBOX;
}

static int rom_uses_parallel_nvram(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_EPO_SDB || kind == DRGQST_ROM_EPO_EBOX;
}

static int rom_has_internal_cursor(enum drgqst_rom_kind kind)
{
	if (kind == DRGQST_ROM_TTV_LOTR || kind == DRGQST_ROM_TTV_SW ||
		kind == DRGQST_ROM_TTV_SWJ)
		return drgqst_core_internal_cursor_visible(g_core);
	return kind == DRGQST_ROM_BAN_OMT || kind == DRGQST_ROM_BAN_NARU;
}

static enum drgqst_core_profile core_profile_for_rom(
	enum drgqst_rom_kind kind)
{
	switch (kind)
	{
	case DRGQST_ROM_BAN_ONEP:
		return DRGQST_CORE_BAN_ONEP;
	case DRGQST_ROM_BAN_OMT:
		return DRGQST_CORE_BAN_OMT;
	case DRGQST_ROM_TTV_LOTR:
	case DRGQST_ROM_TTV_SW:
	case DRGQST_ROM_TTV_SWJ:
		return kind == DRGQST_ROM_TTV_LOTR ?
			DRGQST_CORE_TTV_CU5501_24C02 :
			DRGQST_CORE_TTV_CU5501A_24C02;
	case DRGQST_ROM_TTV_MX:
	case DRGQST_ROM_TOM_JUMP:
	case DRGQST_ROM_TAK_CHQ:
		return DRGQST_CORE_XAVIX2000_I2C_24C04;
	case DRGQST_ROM_EPO_BOWL:
		return DRGQST_CORE_EPO_BOWL_SENSOR_24C04;
	case DRGQST_ROM_EPO_SDB:
		return DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB;
	case DRGQST_ROM_EPO_EBOX:
		return DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM;
	case DRGQST_ROM_EPO_ES2J:
		return DRGQST_CORE_XAVIX2000_PLAIN;
	case DRGQST_ROM_EPO_HAMC:
		return DRGQST_CORE_EPO_HAMC_SENSOR;
	case DRGQST_ROM_EPO_HAMD:
		return DRGQST_CORE_XAVIX_BASE;
	case DRGQST_ROM_TVPC_DOR:
	case DRGQST_ROM_TVPC_HAM:
	case DRGQST_ROM_TVPC_HK:
		return DRGQST_CORE_XAVIX_I2C_24C16;
	case DRGQST_ROM_DRAGON_QUEST:
	case DRGQST_ROM_UNKNOWN:
	default:
		return DRGQST_CORE_DRAGON_QUEST;
	}
}

static const uint8_t *rom_sha1_for_kind(enum drgqst_rom_kind kind)
{
	switch (kind)
	{
	case DRGQST_ROM_BAN_ONEP:
		return BAN_ONEP_ROM_SHA1;
	case DRGQST_ROM_BAN_OMT:
		return BAN_OMT_ROM_SHA1;
	case DRGQST_ROM_TTV_LOTR:
		return TTV_LOTR_ROM_SHA1;
	case DRGQST_ROM_TTV_SW:
		return TTV_SW_ROM_SHA1;
	case DRGQST_ROM_TTV_SWJ:
		return TTV_SWJ_ROM_SHA1;
	case DRGQST_ROM_TTV_MX:
		return TTV_MX_ROM_SHA1;
	case DRGQST_ROM_TOM_JUMP:
		return TOM_JUMP_ROM_SHA1;
	case DRGQST_ROM_EPO_SDB:
		return EPO_SDB_ROM_SHA1;
	case DRGQST_ROM_EPO_BOWL:
		return EPO_BOWL_ROM_SHA1;
	case DRGQST_ROM_TAK_CHQ:
		return TAK_CHQ_ROM_SHA1;
	case DRGQST_ROM_EPO_EBOX:
		return EPO_EBOX_ROM_SHA1;
	case DRGQST_ROM_EPO_ES2J:
		return EPO_ES2J_ROM_SHA1;
	case DRGQST_ROM_EPO_HAMC:
		return EPO_HAMC_ROM_SHA1;
	case DRGQST_ROM_EPO_HAMD:
		return EPO_HAMD_ROM_SHA1;
	case DRGQST_ROM_TVPC_DOR:
		return TVPC_DOR_ROM_SHA1;
	case DRGQST_ROM_TVPC_HAM:
		return TVPC_HAM_ROM_SHA1;
	case DRGQST_ROM_TVPC_HK:
		return TVPC_HK_ROM_SHA1;
	case DRGQST_ROM_DRAGON_QUEST:
	case DRGQST_ROM_UNKNOWN:
	default:
		return DRGQST_ROM_SHA1;
	}
}

static enum drgqst_persistence_kind persistence_kind_for_rom(
	enum drgqst_persistence_kind kind, enum drgqst_rom_kind rom_kind)
{
	switch (rom_kind)
	{
	case DRGQST_ROM_BAN_ONEP:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_BAN_ONEP_EEPROM :
			DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE;
	case DRGQST_ROM_BAN_OMT:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_BAN_OMT_EEPROM :
			DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE;
	case DRGQST_ROM_TTV_LOTR:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TTV_LOTR_EEPROM :
			DRGQST_PERSISTENCE_TTV_LOTR_RUNTIME_STATE;
	case DRGQST_ROM_TTV_SW:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TTV_SW_EEPROM :
			DRGQST_PERSISTENCE_TTV_SW_RUNTIME_STATE;
	case DRGQST_ROM_TTV_SWJ:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TTV_SWJ_EEPROM :
			DRGQST_PERSISTENCE_TTV_SWJ_RUNTIME_STATE;
	case DRGQST_ROM_TTV_MX:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TTV_MX_EEPROM :
			DRGQST_PERSISTENCE_TTV_MX_RUNTIME_STATE;
	case DRGQST_ROM_TOM_JUMP:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TOM_JUMP_EEPROM :
			DRGQST_PERSISTENCE_TOM_JUMP_RUNTIME_STATE;
	case DRGQST_ROM_EPO_SDB:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_EPO_SDB_RUNTIME_STATE :
			DRGQST_PERSISTENCE_EPO_SDB_NVRAM;
	case DRGQST_ROM_EPO_EBOX:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_EPO_EBOX_RUNTIME_STATE :
			DRGQST_PERSISTENCE_EPO_EBOX_NVRAM;
	case DRGQST_ROM_EPO_ES2J:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_EPO_ES2J_RUNTIME_STATE : kind;
	case DRGQST_ROM_EPO_HAMC:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_EPO_HAMC_RUNTIME_STATE : kind;
	case DRGQST_ROM_EPO_BOWL:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_EPO_BOWL_EEPROM :
			DRGQST_PERSISTENCE_EPO_BOWL_RUNTIME_STATE;
	case DRGQST_ROM_TAK_CHQ:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TAK_CHQ_EEPROM :
			DRGQST_PERSISTENCE_TAK_CHQ_RUNTIME_STATE;
	case DRGQST_ROM_EPO_HAMD:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE : kind;
	case DRGQST_ROM_TVPC_DOR:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TVPC_DOR_EEPROM :
			DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE;
	case DRGQST_ROM_TVPC_HAM:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TVPC_HAM_EEPROM :
			DRGQST_PERSISTENCE_TVPC_HAM_RUNTIME_STATE;
	case DRGQST_ROM_TVPC_HK:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TVPC_HK_EEPROM :
			DRGQST_PERSISTENCE_TVPC_HK_RUNTIME_STATE;
	case DRGQST_ROM_DRAGON_QUEST:
	case DRGQST_ROM_UNKNOWN:
	default:
		return kind;
	}
}

static size_t eeprom_size_for_rom(enum drgqst_rom_kind kind)
{
	if (kind == DRGQST_ROM_EPO_HAMD || kind == DRGQST_ROM_EPO_ES2J ||
		kind == DRGQST_ROM_EPO_HAMC ||
		rom_uses_parallel_nvram(kind) ||
		kind == DRGQST_ROM_BAN_NARU)
		return 0;
	if (drgqst_rom_is_tvpc(kind))
		return DRGQST_PERSISTENCE_EEPROM24C16_SIZE;
	return DRGQST_PERSISTENCE_EEPROM_SIZE;
}

static void apply_system_ui_language(enum interface_language language)
{
	LANGID language_id = language == LANGUAGE_ENGLISH ?
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) :
		MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
	SetThreadUILanguage(language_id);
}

static void update_window_title(HWND window)
{
	const interface_strings *text = interface_text();
	const wchar_t *title = text->window_title_idle;

	switch (g_window_status)
	{
	case WINDOW_STATUS_RUNNING:
		title = text->window_title_running;
		break;
	case WINDOW_STATUS_STATE_SAVED:
		title = text->window_title_state_saved;
		break;
	case WINDOW_STATUS_STATE_LOADED:
		title = text->window_title_state_loaded;
		break;
	case WINDOW_STATUS_SCREENSHOT_SAVED:
		title = text->window_title_screenshot_saved;
		break;
	case WINDOW_STATUS_IDLE:
	default:
		break;
	}
	SetWindowTextW(window, title);
}

static void initialize_idle_framebuffer(void)
{
	int y;

	for (y = 0; y < FRAME_HEIGHT; ++y)
	{
		int x;
		for (x = 0; x < FRAME_WIDTH; ++x)
		{
			uint8_t shade = (uint8_t)(((x >> 4) ^ (y >> 4)) & 1 ? 0x28 : 0x18);
			g_idle_framebuffer[y * FRAME_WIDTH + x] =
				((uint32_t)shade << 16) | ((uint32_t)shade << 8) | shade;
		}
	}
}

static HMENU create_application_menu(void)
{
	const interface_strings *text = interface_text();
	HMENU menu = CreateMenu();
	HMENU file_menu = CreatePopupMenu();
	HMENU state_menu = CreatePopupMenu();
	HMENU view_menu = CreatePopupMenu();
	HMENU language_menu = CreatePopupMenu();
	HMENU help_menu = CreatePopupMenu();
	UINT state_flags = MF_STRING | (g_core ? MF_ENABLED : MF_GRAYED);
	UINT screenshot_flags = MF_STRING |
		(emulator_loaded() ? MF_ENABLED : MF_GRAYED);

	AppendMenuW(file_menu, MF_STRING, ID_FILE_OPEN, text->menu_open);
	AppendMenuW(file_menu, screenshot_flags, ID_FILE_SCREENSHOT,
		text->menu_screenshot);
	AppendMenuW(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(file_menu, MF_STRING, ID_FILE_EXIT, text->menu_exit);

	AppendMenuW(state_menu, state_flags, ID_STATE_SAVE,
		text->menu_save_state);
	AppendMenuW(state_menu, state_flags, ID_STATE_LOAD,
		text->menu_load_state);

	AppendMenuW(view_menu, MF_STRING, ID_VIEW_SCALE_1, text->menu_scale_1);
	AppendMenuW(view_menu, MF_STRING, ID_VIEW_SCALE_2, text->menu_scale_2);
	AppendMenuW(view_menu, MF_STRING, ID_VIEW_SCALE_3, text->menu_scale_3);
	AppendMenuW(view_menu, MF_STRING, ID_VIEW_SCALE_4, text->menu_scale_4);
	AppendMenuW(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(view_menu, MF_STRING, ID_VIEW_MAXIMIZE,
		text->menu_maximize);
	AppendMenuW(view_menu,
		MF_STRING | (g_stretch_4_3 ? MF_CHECKED : MF_UNCHECKED),
		ID_VIEW_STRETCH_4_3, text->menu_stretch_4_3);
	AppendMenuW(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(view_menu, MF_STRING, ID_VIEW_FULLSCREEN,
		text->menu_fullscreen);

	AppendMenuW(language_menu,
		MF_STRING | (g_language == LANGUAGE_ZH_TW ? MF_CHECKED : MF_UNCHECKED),
		ID_LANGUAGE_ZH_TW, text->menu_zh_tw);
	AppendMenuW(language_menu,
		MF_STRING | (g_language == LANGUAGE_ENGLISH ? MF_CHECKED : MF_UNCHECKED),
		ID_LANGUAGE_ENGLISH, text->menu_english);
	CheckMenuRadioItem(language_menu, ID_LANGUAGE_ZH_TW,
		ID_LANGUAGE_ENGLISH,
		g_language == LANGUAGE_ZH_TW ? ID_LANGUAGE_ZH_TW :
		ID_LANGUAGE_ENGLISH, MF_BYCOMMAND);

	AppendMenuW(help_menu, MF_STRING, ID_HELP_ABOUT, text->menu_about);

	AppendMenuW(menu, MF_POPUP, (UINT_PTR)file_menu, text->menu_file);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)state_menu, text->menu_state);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)view_menu, text->menu_view);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)language_menu,
		text->menu_language);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)help_menu, text->menu_help);
	return menu;
}

static void rebuild_application_menu(HWND window)
{
	HMENU old_menu = g_menu;
	HMENU new_menu = create_application_menu();

	if (!new_menu)
		return;
	g_menu = new_menu;
	if (!g_fullscreen)
	{
		SetMenu(window, g_menu);
		DrawMenuBar(window);
	}
	if (old_menu)
		DestroyMenu(old_menu);
}

static void set_interface_language(HWND window,
	enum interface_language language)
{
	if (g_language == language)
		return;
	g_language = language;
	apply_system_ui_language(language);
	rebuild_application_menu(window);
	update_window_title(window);
}

static void open_web_page(HWND window, const wchar_t *url)
{
	const interface_strings *text = interface_text();
	HINSTANCE result = ShellExecuteW(window, L"open", url, NULL, NULL,
		SW_SHOWNORMAL);

	if ((INT_PTR)result <= 32)
		MessageBoxW(window, text->link_open_error, text->link_open_title,
			MB_OK | MB_ICONERROR);
}

static INT_PTR CALLBACK about_dialog_procedure(HWND dialog, UINT message,
	WPARAM wparam, LPARAM lparam)
{
	(void)lparam;
	if (message == WM_INITDIALOG)
	{
		const interface_strings *text = interface_text();
		HINSTANCE instance = GetModuleHandleW(NULL);
		HICON icon = (HICON)LoadImageW(instance,
			MAKEINTRESOURCEW(IDI_XAVIXEMU), IMAGE_ICON, 16, 16,
			LR_DEFAULTCOLOR | LR_SHARED);

		SetWindowTextW(dialog, text->about_title);
		SetDlgItemTextW(dialog, IDC_ABOUT_TEXT, text->about_text);
		SetDlgItemTextW(dialog, IDC_ABOUT_FACEBOOK,
			text->about_facebook_button);
		SetDlgItemTextW(dialog, IDC_ABOUT_SUPPORT_TEXT,
			text->about_support_text);
		SetDlgItemTextW(dialog, IDC_ABOUT_SUBSCRIBE,
			text->about_subscribe_button);
		SetDlgItemTextW(dialog, IDOK, text->about_close_button);
		if (icon)
			SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)icon);
		return TRUE;
	}
	if (message == WM_COMMAND)
	{
		switch (LOWORD(wparam))
		{
		case IDC_ABOUT_FACEBOOK:
			open_web_page(dialog, FACEBOOK_URL);
			return TRUE;
		case IDC_ABOUT_SUBSCRIBE:
			open_web_page(dialog, SUBSCRIBE_URL);
			return TRUE;
		case IDOK:
		case IDCANCEL:
			EndDialog(dialog, LOWORD(wparam));
			return TRUE;
		default:
			break;
		}
	}
	return FALSE;
}

static void show_about(HWND window)
{
	const interface_strings *text = interface_text();
	if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_ABOUT),
		window, about_dialog_procedure, 0) == -1)
		MessageBoxW(window, text->about_text, text->about_title,
			MB_OK | MB_ICONINFORMATION);
}

static display_viewport calculate_viewport(HWND window)
{
	display_viewport viewport;
	RECT client;
	int client_width;
	int client_height;
	int scale_x;
	int scale_y;

	GetClientRect(window, &client);
	client_width = client.right - client.left;
	client_height = client.bottom - client.top;
	if (g_stretch_4_3)
	{
		if ((int64_t)client_width * 3 <= (int64_t)client_height * 4)
		{
			viewport.width = client_width;
			viewport.height = (client_width * 3 + 2) / 4;
		}
		else
		{
			viewport.height = client_height;
			viewport.width = (client_height * 4 + 1) / 3;
		}
		if (viewport.width < 1)
			viewport.width = 1;
		if (viewport.height < 1)
			viewport.height = 1;
		viewport.scale = viewport.height / (int)g_frame_height;
		if (viewport.scale < 1)
			viewport.scale = 1;
		viewport.x = (client_width - viewport.width) / 2;
		viewport.y = (client_height - viewport.height) / 2;
		return viewport;
	}

	scale_x = client_width / (int)g_frame_width;
	scale_y = client_height / (int)g_frame_height;
	viewport.scale = scale_x < scale_y ? scale_x : scale_y;
	if (viewport.scale < 1)
		viewport.scale = 1;
	viewport.width = (int)g_frame_width * viewport.scale;
	viewport.height = (int)g_frame_height * viewport.scale;
	viewport.x = (client_width - viewport.width) / 2;
	viewport.y = (client_height - viewport.height) / 2;
	return viewport;
}

static void resize_for_scale(HWND window, int scale)
{
	int client_width;
	int client_height = (int)g_frame_height * scale;
	RECT rectangle;
	LONG_PTR style;
	LONG_PTR extended_style;

	if (g_fullscreen)
		return;
	g_window_scale = scale;
	if (IsZoomed(window))
		ShowWindow(window, SW_RESTORE);
	client_width = g_stretch_4_3 ?
		(client_height * 4 + 1) / 3 : (int)g_frame_width * scale;
	SetRect(&rectangle, 0, 0, client_width, client_height);

	style = GetWindowLongPtrW(window, GWL_STYLE);
	extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
	AdjustWindowRectEx(&rectangle, (DWORD)style, TRUE, (DWORD)extended_style);
	SetWindowPos(window, NULL, 0, 0,
		rectangle.right - rectangle.left,
		rectangle.bottom - rectangle.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void toggle_stretch_4_3(HWND window)
{
	g_stretch_4_3 = !g_stretch_4_3;
	CheckMenuItem(g_menu, ID_VIEW_STRETCH_4_3,
		MF_BYCOMMAND | (g_stretch_4_3 ? MF_CHECKED : MF_UNCHECKED));
	if (!g_fullscreen && !IsZoomed(window))
		resize_for_scale(window, g_window_scale);
	InvalidateRect(window, NULL, FALSE);
}

static void toggle_fullscreen(HWND window)
{
	if (!g_fullscreen)
	{
		MONITORINFO monitor;
		memset(&monitor, 0, sizeof(monitor));
		monitor.cbSize = sizeof(monitor);
		g_windowed_style = GetWindowLongPtrW(window, GWL_STYLE);
		g_windowed_placement.length = sizeof(g_windowed_placement);
		GetWindowPlacement(window, &g_windowed_placement);
		GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
		SetMenu(window, NULL);
		SetWindowLongPtrW(window, GWL_STYLE, g_windowed_style & ~WS_OVERLAPPEDWINDOW);
		SetWindowPos(window, HWND_TOP,
			monitor.rcMonitor.left, monitor.rcMonitor.top,
			monitor.rcMonitor.right - monitor.rcMonitor.left,
			monitor.rcMonitor.bottom - monitor.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		g_fullscreen = 1;
	}
	else
	{
		SetWindowLongPtrW(window, GWL_STYLE, g_windowed_style);
		SetMenu(window, g_menu);
		SetWindowPlacement(window, &g_windowed_placement);
		SetWindowPos(window, NULL, 0, 0, 0, 0,
			SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
			SWP_NOZORDER | SWP_NOACTIVATE);
		g_fullscreen = 0;
	}
	InvalidateRect(window, NULL, FALSE);
}

static void start_frame_clock(HWND window)
{
	LARGE_INTEGER now;
	QueryPerformanceFrequency(&g_counter_frequency);
	QueryPerformanceCounter(&now);
	g_frame_counter_step = g_counter_frequency.QuadPart / XAVIX_FRAME_RATE;
	if (g_frame_counter_step < 1)
		g_frame_counter_step = 1;
	g_next_frame_counter = now.QuadPart;
	g_timing_window_counter = now.QuadPart;
	g_timing_core_counter = 0;
	g_timing_frames = 0;
	g_timing_dropped_frames = 0;
	g_timing_guest_cycles = g_xavix2 ?
		g_xavix2->cpu.total_cycles : 0;
	g_timing_interrupts = g_xavix2 ?
		g_xavix2->cpu.interrupt_count : 0;
	SetTimer(window, ID_EMULATION_TIMER, 1, NULL);
}

static void stop_frame_clock(HWND window)
{
	KillTimer(window, ID_EMULATION_TIMER);
}

static void update_core_mouse(void)
{
	if (!g_core)
		return;
	if (g_rom.kind == DRGQST_ROM_TVPC_DOR)
	{
		g_core->machine.state.anport_regs[2] = g_tvpc_mouse_counter_x;
		g_core->machine.state.anport_regs[3] = g_tvpc_mouse_counter_y;
		if (g_left_button)
			g_core->machine.state.input0 |= 0x80;
		else
			g_core->machine.state.input0 &= (uint8_t)~0x80;
		return;
	}
	if (rom_uses_digital_direction_input(g_rom.kind))
		return;
	if (g_rom.kind == DRGQST_ROM_EPO_ES2J ||
		g_rom.kind == DRGQST_ROM_EPO_HAMC)
		return;
	drgqst_core_set_mouse(g_core, g_mouse_x, g_mouse_y,
		g_left_button || (g_rom.kind == DRGQST_ROM_BAN_OMT &&
			g_omt_backside),
		g_right_button || (g_rom.kind == DRGQST_ROM_BAN_OMT &&
			g_omt_backside));
	if (g_rom.kind == DRGQST_ROM_TTV_SW && !g_left_button &&
		!g_right_button && !g_ttv_spin_held)
	{
		/* The US program treats the 3-by-3 Japanese narrow image as a held
		 * defensive pose.  A real moving edge is smaller and immediately
		 * leaves the camera field; keep only a one-frame point sample. */
		xavix_machine_set_sword_input(&g_core->machine, g_mouse_x, g_mouse_y,
			g_ttv_sw_motion_frames ? XAVIX_SENSOR_POINT : XAVIX_SENSOR_NONE);
	}
	if ((g_rom.kind == DRGQST_ROM_TTV_SW ||
		g_rom.kind == DRGQST_ROM_TTV_SWJ) && g_ttv_spin_held)
	{
		static const enum xavix_sensor_mode rotation[4] =
		{
			XAVIX_SENSOR_VERTICAL,
			XAVIX_SENSOR_DIAGONAL_DOWN,
			XAVIX_SENSOR_HORIZONTAL,
			XAVIX_SENSOR_DIAGONAL_UP
		};
		/* Pointing the physical saber at the camera and rolling it produces
		 * a rotating elongated reflection, rather than a moving point. */
		xavix_machine_set_sword_input(&g_core->machine, 0x80, 0x80,
			rotation[(g_ttv_spin_phase / 2) & 3]);
	}
}

static void synchronize_mouse_buttons(HWND window)
{
	g_left_button = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	g_right_button = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	if (g_left_button || g_right_button)
		SetCapture(window);
	else if (GetCapture() == window)
		ReleaseCapture();
}

static void set_tvpc_cursor_key(WPARAM key, int pressed)
{
	uint8_t mask;

	if (!g_core || g_rom.kind != DRGQST_ROM_TVPC_DOR)
		return;
	switch (key)
	{
	case VK_UP:
	case 'W':
		mask = 0x01;
		break;
	case VK_DOWN:
	case 'S':
		mask = 0x02;
		break;
	case VK_LEFT:
	case 'A':
		mask = 0x04;
		break;
	case VK_RIGHT:
	case 'D':
		mask = 0x08;
		break;
	default:
		return;
	}
	if (pressed)
		g_tvpc_keyboard_rows[1] |= mask;
	else
		g_tvpc_keyboard_rows[1] &= (uint8_t)~mask;
}

static void update_tvpc_keyboard(void)
{
	uint8_t rows[8];
	unsigned row;

	if (!g_core || g_rom.kind != DRGQST_ROM_TVPC_DOR)
		return;
	memcpy(rows, g_tvpc_keyboard_rows, sizeof(rows));
	if (g_tvpc_mouse_key_active)
		g_tvpc_mouse_key_active = 0;
	else if (g_tvpc_mouse_key_pending)
	{
		g_tvpc_mouse_key_active = g_tvpc_mouse_key_pending;
		g_tvpc_mouse_key_pending = 0;
		rows[1] |= g_tvpc_mouse_key_active;
	}
	for (row = 0; row < sizeof(rows); ++row)
		drgqst_core_set_tvpc_keyboard_row(g_core, row, rows[row]);
}

static void update_hamd_input(void)
{
	if (!g_core || g_rom.kind != DRGQST_ROM_EPO_HAMD)
		return;
	if (g_hamd_left_pulse_frames)
	{
		drgqst_core_trigger_hamd_packet(g_core, 0x15);
		--g_hamd_left_pulse_frames;
	}
	if (g_hamd_right_pulse_frames)
	{
		drgqst_core_trigger_hamd_packet(g_core, 0x13);
		--g_hamd_right_pulse_frames;
	}
	if (g_hamd_confirm_frames)
	{
		g_core->machine.state.input0 |= 0x01;
		--g_hamd_confirm_frames;
	}
	else
		g_core->machine.state.input0 &= (uint8_t)~0x01;
}

static void update_digital_direction_input(void)
{
	uint8_t input = 0;
	int up;
	int down;
	int left;
	int right;

	if (!g_core || !rom_uses_digital_direction_input(g_rom.kind))
		return;

	/* These boards expose active-high digital directions.  Keyboard directions
	 * take priority; with no direction key held, the mouse position supplies a
	 * virtual tilt or boxing-pad direction with a generous centre dead zone. */
	up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0 ||
		(GetAsyncKeyState('W') & 0x8000) != 0;
	down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
		(GetAsyncKeyState('S') & 0x8000) != 0;
	left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 ||
		(GetAsyncKeyState('A') & 0x8000) != 0;
	right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 ||
		(GetAsyncKeyState('D') & 0x8000) != 0;
	if (!up && !down && !left && !right)
	{
		up = g_mouse_y < 0x60;
		down = g_mouse_y > 0x9f;
		left = g_mouse_x < 0x60;
		right = g_mouse_x > 0x9f;
	}
	if (up && !down)
		input |= 0x10;
	else if (down && !up)
		input |= 0x20;
	if (left && !right)
		input |= 0x40;
	else if (right && !left)
		input |= 0x80;
	if (g_left_button || (GetAsyncKeyState(VK_SPACE) & 0x8000))
		input |= 0x01;
	if (g_right_button || (GetAsyncKeyState(VK_CONTROL) & 0x8000))
		input |= 0x02;
	if (g_rom.kind != DRGQST_ROM_EPO_EBOX &&
		((GetAsyncKeyState(VK_MBUTTON) & 0x8000) ||
		(GetAsyncKeyState('P') & 0x8000)))
		input |= 0x04;
	g_core->machine.state.input0 = input;
}

static void release_held_host_inputs(HWND window)
{
	g_left_button = 0;
	g_right_button = 0;
	g_omt_backside = 0;
	g_ttv_spin_held = 0;
	g_ttv_spin_phase = 0;
	g_ttv_sw_motion_frames = 0;
	g_hamd_left_pulse_frames = 0;
	g_hamd_right_pulse_frames = 0;
	g_hamd_confirm_frames = 0;
	memset(g_tvpc_keyboard_rows, 0, sizeof(g_tvpc_keyboard_rows));
	g_tvpc_mouse_key_pending = 0;
	g_tvpc_mouse_key_active = 0;
	g_naruto_joined_hands = 0;
	g_naruto_execute_delay = 0;
	g_naruto_execute_frames = 0;
	if (GetCapture() == window)
		ReleaseCapture();
	update_core_mouse();
}

static void advance_ttv_special_gesture(void)
{
	if (g_ttv_spin_held)
		g_ttv_spin_phase = (g_ttv_spin_phase + 1) & 7;
	if (g_ttv_sw_motion_frames)
		--g_ttv_sw_motion_frames;
}

static void pulse_ban_onep_menu_input(uint8_t input)
{
	if (!g_core || !rom_uses_camera(g_rom.kind))
		return;
	g_core->machine.state.input0 &= (uint8_t)~g_ban_onep_menu_input;
	g_ban_onep_menu_input = input;
	g_ban_onep_menu_input_frames = 4;
	g_core->machine.state.input0 |= g_ban_onep_menu_input;
}

static void advance_ban_onep_menu_input(void)
{
	if (!g_core || !g_ban_onep_menu_input_frames)
		return;
	if (!--g_ban_onep_menu_input_frames)
	{
		g_core->machine.state.input0 &=
			(uint8_t)~g_ban_onep_menu_input;
		g_ban_onep_menu_input = 0;
	}
}

static void update_cursor_presentation(void)
{
	int x;
	int y;

	if (!g_core || rom_uses_camera(g_rom.kind))
		return;
	if (drgqst_core_sword_cursor_position(g_core, &x, &y))
		drgqst_cursor_presentation_update(&g_cursor_presentation, x, y);
}

static int initialize_executable_directory(void)
{
	DWORD length = GetModuleFileNameW(NULL, g_executable_directory,
		sizeof(g_executable_directory) / sizeof(g_executable_directory[0]));
	wchar_t *separator;
	wchar_t *alternative;

	if (!length || length >=
		sizeof(g_executable_directory) / sizeof(g_executable_directory[0]))
		return 0;
	separator = wcsrchr(g_executable_directory, L'\\');
	alternative = wcsrchr(g_executable_directory, L'/');
	if (alternative && (!separator || alternative > separator))
		separator = alternative;
	if (!separator)
		return 0;
	if (separator == g_executable_directory + 2 &&
		g_executable_directory[1] == L':')
		separator[1] = L'\0';
	else
		*separator = L'\0';
	return g_executable_directory[0] != L'\0';
}

static int persistence_file_is_missing(enum drgqst_persistence_kind kind)
{
	wchar_t path[MAX_PATH];
	wchar_t error[256];
	DWORD windows_error;

	if (!drgqst_persistence_get_path(g_executable_directory, kind, path,
		sizeof(path) / sizeof(path[0]), error,
		sizeof(error) / sizeof(error[0])))
		return 0;
	if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
		return 0;
	windows_error = GetLastError();
	return windows_error == ERROR_FILE_NOT_FOUND ||
		windows_error == ERROR_PATH_NOT_FOUND;
}

static int load_persistence_data(enum drgqst_persistence_kind kind,
	enum drgqst_rom_kind rom_kind,
	void *payload, size_t payload_capacity, size_t *payload_size,
	wchar_t *error, size_t error_length, int *loaded_from_legacy)
{
	const enum drgqst_persistence_kind stored_kind =
		persistence_kind_for_rom(kind, rom_kind);
	const uint8_t *rom_sha1 = rom_sha1_for_kind(rom_kind);
	if (loaded_from_legacy)
		*loaded_from_legacy = 0;

	if (drgqst_persistence_load(g_executable_directory, stored_kind, rom_sha1,
		payload, payload_capacity, payload_size, error, error_length))
		return 1;
	if (!persistence_file_is_missing(stored_kind) ||
		!drgqst_persistence_load(NULL, stored_kind, rom_sha1, payload,
			payload_capacity, payload_size, error, error_length))
		return 0;
	if (loaded_from_legacy)
		*loaded_from_legacy = 1;
	return 1;
}

static void load_persistent_eeprom(HWND window, drgqst_core *core,
	enum drgqst_rom_kind rom_kind)
{
	const interface_strings *text = interface_text();
	uint8_t image[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
	wchar_t error[384];
	size_t expected_size = rom_uses_parallel_nvram(rom_kind) ?
		DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE :
		eeprom_size_for_rom(rom_kind);
	size_t size = 0;
	int loaded_from_legacy = 0;

	if (!expected_size)
		return;
	if (load_persistence_data(DRGQST_PERSISTENCE_EEPROM, rom_kind,
		image, expected_size, &size, error,
		sizeof(error) / sizeof(error[0]), &loaded_from_legacy) &&
		size == expected_size)
	{
		if (rom_uses_parallel_nvram(rom_kind))
			memcpy(core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
				image, size);
		else
			xavix_eeprom_load_image(
				&core->machine.state.peripherals.eeprom, image, size);
		if (loaded_from_legacy &&
			!drgqst_persistence_save(g_executable_directory,
				persistence_kind_for_rom(DRGQST_PERSISTENCE_EEPROM, rom_kind),
				rom_sha1_for_kind(rom_kind), image, size,
				error, sizeof(error) / sizeof(error[0])))
			MessageBoxW(window, text->eeprom_save_error,
				text->eeprom_save_title, MB_OK | MB_ICONERROR);
	}
}

static int save_persistent_eeprom(HWND window, int show_error)
{
	const interface_strings *text = interface_text();
	xavix_eeprom24c08 *eeprom;
	uint8_t image[DRGQST_PERSISTENCE_EEPROM24C16_SIZE];
	wchar_t error[384];
	size_t size;

	if (!g_core)
		return 1;
	if (rom_uses_parallel_nvram(g_rom.kind))
	{
		if (!drgqst_persistence_save(g_executable_directory,
			persistence_kind_for_rom(DRGQST_PERSISTENCE_EEPROM, g_rom.kind),
			rom_sha1_for_kind(g_rom.kind),
			g_core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
			DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE, error,
			sizeof(error) / sizeof(error[0])))
		{
			if (show_error || !g_eeprom_error_shown)
				MessageBoxW(window, text->eeprom_save_error,
					text->eeprom_save_title, MB_OK | MB_ICONWARNING);
			g_eeprom_error_shown = 1;
			return 0;
		}
		g_parallel_nvram_generation =
			g_core->machine.nvram_write_generation;
		g_parallel_nvram_settle_frames = 0;
		g_parallel_nvram_save_pending = 0;
		g_eeprom_error_shown = 0;
		return 1;
	}
	size = eeprom_size_for_rom(g_rom.kind);
	if (!size)
		return 1;
	eeprom = &g_core->machine.state.peripherals.eeprom;
	if (!xavix_eeprom24c08_is_dirty(eeprom))
		return 1;
	xavix_eeprom_copy_image(eeprom, image, size);
	if (!drgqst_persistence_save(g_executable_directory,
		persistence_kind_for_rom(DRGQST_PERSISTENCE_EEPROM, g_rom.kind),
		rom_sha1_for_kind(g_rom.kind), image, size, error,
		sizeof(error) / sizeof(error[0])))
	{
		if (show_error || !g_eeprom_error_shown)
			MessageBoxW(window, text->eeprom_save_error,
				text->eeprom_save_title,
				MB_OK | MB_ICONWARNING);
		g_eeprom_error_shown = 1;
		return 0;
	}
	xavix_eeprom24c08_clear_dirty(eeprom);
	g_eeprom_error_shown = 0;
	return 1;
}

static void poll_persistent_eeprom(HWND window)
{
	xavix_eeprom24c08 *eeprom;
	uint32_t generation;

	if (!g_core)
		return;
	if (rom_uses_parallel_nvram(g_rom.kind))
	{
		generation = g_core->machine.nvram_write_generation;
		if (generation != g_parallel_nvram_generation)
		{
			g_parallel_nvram_generation = generation;
			g_parallel_nvram_settle_frames = 30;
			g_parallel_nvram_save_pending = 1;
		}
		else if (g_parallel_nvram_settle_frames)
			--g_parallel_nvram_settle_frames;
		if (g_parallel_nvram_save_pending &&
			!g_parallel_nvram_settle_frames)
			save_persistent_eeprom(window, 0);
		return;
	}
	if (!eeprom_size_for_rom(g_rom.kind))
		return;
	eeprom = &g_core->machine.state.peripherals.eeprom;
	generation = eeprom->write_generation;
	if (generation != g_eeprom_generation)
	{
		g_eeprom_generation = generation;
		g_eeprom_settle_frames = 30;
	}
	else if (g_eeprom_settle_frames)
		--g_eeprom_settle_frames;
	if (xavix_eeprom24c08_is_dirty(eeprom) && !g_eeprom_settle_frames)
		save_persistent_eeprom(window, 0);
}

static void set_mouse_position(HWND window, int client_x, int client_y)
{
	display_viewport viewport = calculate_viewport(window);
	int x = client_x - viewport.x;
	int y = client_y - viewport.y;
	uint8_t new_mouse_x;
	uint8_t new_mouse_y;

	if (x < 0)
		x = 0;
	else if (x >= viewport.width)
		x = viewport.width - 1;
	if (y < 0)
		y = 0;
	else if (y >= viewport.height)
		y = viewport.height - 1;
	new_mouse_x = viewport.width > 1 ?
		(uint8_t)((x * 255 + (viewport.width - 1) / 2) /
			(viewport.width - 1)) : 0;
	new_mouse_y = viewport.height > 1 ?
		(uint8_t)((y * 255 + (viewport.height - 1) / 2) /
			(viewport.height - 1)) : 0;
	if (g_core && g_rom.kind == DRGQST_ROM_TTV_SW &&
		(new_mouse_x != g_mouse_x || new_mouse_y != g_mouse_y))
		g_ttv_sw_motion_frames = 1;
	if (g_core && g_rom.kind == DRGQST_ROM_TVPC_DOR)
	{
		if (g_tvpc_mouse_position_valid)
		{
			int delta_x = (int)new_mouse_x - (int)g_mouse_x;
			int delta_y = (int)new_mouse_y - (int)g_mouse_y;
			if (delta_x < -32)
				delta_x = -32;
			if (delta_x > 32)
				delta_x = 32;
			if (delta_y < -32)
				delta_y = -32;
			if (delta_y > 32)
				delta_y = 32;
			g_tvpc_mouse_counter_x =
				(uint8_t)(g_tvpc_mouse_counter_x + delta_x);
			g_tvpc_mouse_counter_y =
				(uint8_t)(g_tvpc_mouse_counter_y - delta_y);
			if (delta_y < 0)
				g_tvpc_mouse_key_pending = 0x01;
			else if (delta_y > 0)
				g_tvpc_mouse_key_pending = 0x02;
		}
		g_tvpc_mouse_position_valid = 1;
	}
	g_mouse_x = new_mouse_x;
	g_mouse_y = new_mouse_y;
	update_core_mouse();
}

static void run_xavix2_frame(void)
{
	uint8_t packet[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	uint32_t pio_input = 0;
	unsigned width;
	unsigned height;
	unsigned stride;
	uint8_t sample_x = (uint8_t)(1 +
		((unsigned)g_mouse_x * 54 + 127) / 255);
	uint8_t sample_y = (uint8_t)(55 -
		((unsigned)g_mouse_y * 54 + 127) / 255);

	if (g_naruto_execute_delay)
		--g_naruto_execute_delay;
	else if (g_left_button || g_naruto_execute_frames)
	{
		pio_input = UINT32_C(1) << 16;
		if (g_naruto_execute_frames)
			--g_naruto_execute_frames;
	}

	packet[0] = sample_x;
	packet[1] = sample_y;
	packet[2] = 0x20;
	if (g_right_button || g_naruto_joined_hands)
	{
		packet[3] = sample_x;
		packet[4] = sample_y;
		packet[5] = 0x20;
	}
	(void)xavix2_machine_run_video_frame(g_xavix2, packet, pio_input);
	win_audio_submit(&g_audio_output,
		xavix2_machine_frame_audio(g_xavix2),
		XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME);
	g_framebuffer = xavix2_machine_visible_frame(g_xavix2,
		&width, &height, &stride);
	g_frame_width = width;
	g_frame_height = height;
	g_frame_stride = stride;
}

static void update_timing_diagnostics(HWND window, LONGLONG now)
{
	wchar_t title[256];
	win_audio_stats audio_stats;
	double elapsed;
	double fps;
	double core_ms;
	double guest_rate;
	double irq_rate;
	uint64_t guest_cycles;
	uint64_t interrupts;

	if (!g_timing_diagnostics || !emulator_loaded() ||
		!g_counter_frequency.QuadPart ||
		now - g_timing_window_counter < g_counter_frequency.QuadPart)
		return;
	elapsed = (double)(now - g_timing_window_counter) /
		(double)g_counter_frequency.QuadPart;
	fps = elapsed > 0.0 ? (double)g_timing_frames / elapsed : 0.0;
	core_ms = g_timing_frames ?
		(double)g_timing_core_counter * 1000.0 /
		((double)g_counter_frequency.QuadPart * (double)g_timing_frames) : 0.0;
	guest_cycles = g_xavix2 ? g_xavix2->cpu.total_cycles : 0;
	interrupts = g_xavix2 ? g_xavix2->cpu.interrupt_count : 0;
	guest_rate = g_xavix2 && elapsed > 0.0 ?
		(double)(guest_cycles - g_timing_guest_cycles) / elapsed / 1000000.0 : 0.0;
	irq_rate = g_xavix2 && elapsed > 0.0 ?
		(double)(interrupts - g_timing_interrupts) / elapsed : 0.0;
	win_audio_get_stats(&g_audio_output, &audio_stats);
	swprintf(title, sizeof(title) / sizeof(title[0]),
		L"XaviXEmu | %.1f FPS | %.2f ms/frame | dropped %llu | guest %.1f M/s | IRQ %.1f/s | audio drop %llu / under %llu",
		fps, core_ms, (unsigned long long)g_timing_dropped_frames,
		guest_rate, irq_rate,
		(unsigned long long)audio_stats.dropped_frames,
		(unsigned long long)audio_stats.underruns);
	SetWindowTextW(window, title);
	g_timing_window_counter = now;
	g_timing_core_counter = 0;
	g_timing_frames = 0;
	g_timing_dropped_frames = 0;
	g_timing_guest_cycles = guest_cycles;
	g_timing_interrupts = interrupts;
}

static void run_due_frames(HWND window)
{
	LARGE_INTEGER now;
	LARGE_INTEGER frame_start;
	LARGE_INTEGER frame_end;
	unsigned frames = 0;

	if (!emulator_loaded())
		return;
	QueryPerformanceCounter(&now);
	while (now.QuadPart >= g_next_frame_counter && frames < 3)
	{
		QueryPerformanceCounter(&frame_start);
		if (g_xavix2)
			run_xavix2_frame();
		else
		{
			update_hamd_input();
			update_tvpc_keyboard();
			update_digital_direction_input();
			update_core_mouse();
			g_framebuffer = drgqst_core_run_frame(g_core);
			advance_ttv_special_gesture();
			advance_ban_onep_menu_input();
			update_cursor_presentation();
			win_audio_submit(&g_audio_output,
				drgqst_core_frame_audio(g_core),
				DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME);
			poll_persistent_eeprom(window);
		}
		QueryPerformanceCounter(&frame_end);
		g_timing_core_counter += frame_end.QuadPart - frame_start.QuadPart;
		g_timing_frames++;
		g_next_frame_counter += g_frame_counter_step;
		++frames;
	}
	QueryPerformanceCounter(&now);
	if (frames == 3 && now.QuadPart >= g_next_frame_counter)
	{
		g_timing_dropped_frames += 1 +
			(uint64_t)((now.QuadPart - g_next_frame_counter) /
			g_frame_counter_step);
		g_next_frame_counter = now.QuadPart + g_frame_counter_step;
	}
	update_timing_diagnostics(window, now.QuadPart);
	if (frames)
		InvalidateRect(window, NULL, FALSE);
}

static int save_runtime_state(HWND window)
{
	const interface_strings *text = interface_text();
	uint8_t *state;
	size_t state_size;
	size_t written = 0;
	wchar_t error[384];
	int encoded;
	int success;

	if (!g_core)
		return 0;
	state_size = drgqst_state_serialized_size();
	state = (uint8_t *)malloc(state_size);
	if (!state)
	{
		MessageBoxW(window, text->state_save_memory_error,
			text->state_save_title, MB_OK | MB_ICONERROR);
		return 0;
	}
	error[0] = L'\0';
	encoded = drgqst_state_save(g_core, state, state_size, &written) &&
		written == state_size;
	success = encoded && drgqst_persistence_save(
			g_executable_directory,
			persistence_kind_for_rom(DRGQST_PERSISTENCE_RUNTIME_STATE, g_rom.kind),
			rom_sha1_for_kind(g_rom.kind), state, written, error,
			sizeof(error) / sizeof(error[0]));
	free(state);
	if (!success)
	{
		MessageBoxW(window, text->state_save_error,
			text->state_save_title, MB_OK | MB_ICONERROR);
		return 0;
	}
	g_window_status = WINDOW_STATUS_STATE_SAVED;
	update_window_title(window);
	return 1;
}

static int load_runtime_state(HWND window)
{
	const interface_strings *text = interface_text();
	uint8_t *state;
	uint8_t eeprom_image[DRGQST_PERSISTENCE_EEPROM24C16_SIZE];
	uint8_t parallel_nvram_image[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
	size_t state_size;
	size_t loaded = 0;
	size_t eeprom_size = eeprom_size_for_rom(g_rom.kind);
	wchar_t error[384];
	xavix_eeprom24c08 *eeprom;
	uint32_t eeprom_generation;
	int success;
	int loaded_from_legacy = 0;

	if (!g_core)
		return 0;
	state_size = drgqst_state_serialized_size();
	state = (uint8_t *)malloc(state_size);
	if (!state)
	{
		MessageBoxW(window, text->state_load_memory_error,
			text->state_load_title, MB_OK | MB_ICONERROR);
		return 0;
	}
	if (!load_persistence_data(DRGQST_PERSISTENCE_RUNTIME_STATE, g_rom.kind,
		state, state_size, &loaded, error,
		sizeof(error) / sizeof(error[0]), &loaded_from_legacy))
	{
		free(state);
		MessageBoxW(window, text->state_load_error,
			text->state_load_title, MB_OK | MB_ICONERROR);
		return 0;
	}

	/* Runtime states never rewind or overwrite the game's durable EEPROM. */
	save_persistent_eeprom(window, 1);
	eeprom = &g_core->machine.state.peripherals.eeprom;
	if (eeprom_size)
		xavix_eeprom_copy_image(eeprom, eeprom_image, eeprom_size);
	if (rom_uses_parallel_nvram(g_rom.kind))
		memcpy(parallel_nvram_image,
			g_core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
			sizeof(parallel_nvram_image));
	eeprom_generation = eeprom->write_generation;
	success = drgqst_state_load(g_core, state, loaded);
	if (!success)
	{
		free(state);
		MessageBoxW(window, text->state_incompatible_error,
			text->state_load_title, MB_OK | MB_ICONERROR);
		return 0;
	}
	if (loaded_from_legacy &&
		!drgqst_persistence_save(g_executable_directory,
			persistence_kind_for_rom(DRGQST_PERSISTENCE_RUNTIME_STATE, g_rom.kind),
			rom_sha1_for_kind(g_rom.kind), state, loaded,
			error, sizeof(error) / sizeof(error[0])))
		MessageBoxW(window, text->state_save_error,
			text->state_save_title, MB_OK | MB_ICONERROR);
	free(state);
	eeprom = &g_core->machine.state.peripherals.eeprom;
	if (eeprom_size)
		memcpy(eeprom->data, eeprom_image, eeprom_size);
	if (rom_uses_parallel_nvram(g_rom.kind))
		memcpy(g_core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
			parallel_nvram_image, sizeof(parallel_nvram_image));
	eeprom->dirty = 0;
	eeprom->write_generation = eeprom_generation;
	g_eeprom_generation = eeprom_generation;
	g_eeprom_settle_frames = 0;
	g_parallel_nvram_generation = g_core->machine.nvram_write_generation;
	g_parallel_nvram_settle_frames = 0;
	g_parallel_nvram_save_pending = 0;
	g_ban_onep_menu_input = 0;
	g_ban_onep_menu_input_frames = 0;
	g_omt_backside = 0;
	g_ttv_spin_held = 0;
	g_ttv_spin_phase = 0;
	g_ttv_sw_motion_frames = 0;
	g_hamd_left_pulse_frames = 0;
	g_hamd_right_pulse_frames = 0;
	g_hamd_confirm_frames = 0;
	memset(g_tvpc_keyboard_rows, 0, sizeof(g_tvpc_keyboard_rows));
	g_tvpc_mouse_key_pending = 0;
	g_tvpc_mouse_key_active = 0;
	g_naruto_joined_hands = 0;
	g_naruto_execute_delay = 0;
	g_naruto_execute_frames = 0;
	if (g_rom.kind == DRGQST_ROM_TVPC_DOR)
	{
		g_tvpc_mouse_counter_x = g_core->machine.state.anport_regs[2];
		g_tvpc_mouse_counter_y = g_core->machine.state.anport_regs[3];
		g_tvpc_mouse_position_valid = 0;
	}
	g_core->machine.state.input0 &= (uint8_t)~0x0c;
	/* Runtime states contain guest hardware only.  Re-sample the physical
	 * buttons so a missed Windows button-up message cannot turn a restored
	 * broad reflection into a permanently held defensive posture. */
	synchronize_mouse_buttons(window);
	update_core_mouse();
	/* Submitted PCM is copied into win_audio's own buffers, so restoring the
	 * core does not invalidate it.  Keeping the existing device alive avoids
	 * racing rapid F7 loads against waveOut's asynchronous buffer retirement. */
	g_framebuffer = drgqst_core_framebuffer(g_core);
	drgqst_cursor_presentation_reset(&g_cursor_presentation);
	update_cursor_presentation();
	start_frame_clock(window);
	g_window_status = WINDOW_STATUS_STATE_LOADED;
	update_window_title(window);
	InvalidateRect(window, NULL, FALSE);
	return 1;
}

static int activate_xavix2_rom(HWND window, drgqst_rom_image *image,
	int show_error)
{
	const interface_strings *text = interface_text();
	xavix2_machine_t *machine =
		(xavix2_machine_t *)calloc(1, sizeof(*machine));

	if (!machine || !xavix2_machine_init(machine, image->data, image->size))
	{
		free(machine);
		if (show_error)
			MessageBoxW(window, text->core_initialize_error,
				text->rom_open_title, MB_OK | MB_ICONERROR);
		return 0;
	}

	stop_frame_clock(window);
	save_persistent_eeprom(window, 1);
	free(g_core);
	free(g_xavix2);
	drgqst_rom_release(&g_rom);
	g_core = NULL;
	g_xavix2 = machine;
	g_rom = *image;
	memset(image, 0, sizeof(*image));
	g_ban_onep_menu_input = 0;
	g_ban_onep_menu_input_frames = 0;
	g_omt_backside = 0;
	g_ttv_spin_held = 0;
	g_ttv_spin_phase = 0;
	g_ttv_sw_motion_frames = 0;
	g_hamd_left_pulse_frames = 0;
	g_hamd_right_pulse_frames = 0;
	g_hamd_confirm_frames = 0;
	memset(g_tvpc_keyboard_rows, 0, sizeof(g_tvpc_keyboard_rows));
	g_tvpc_mouse_key_pending = 0;
	g_tvpc_mouse_key_active = 0;
	g_naruto_joined_hands = 0;
	g_naruto_execute_delay = 0;
	g_naruto_execute_frames = 0;
	g_tvpc_mouse_counter_x = 0;
	g_tvpc_mouse_counter_y = 0;
	g_tvpc_mouse_position_valid = 0;
	drgqst_cursor_presentation_reset(&g_cursor_presentation);
	win_audio_shutdown(&g_audio_output);
	win_audio_init(&g_audio_output);
	win_audio_open(&g_audio_output);
	run_xavix2_frame();
	g_window_status = WINDOW_STATUS_RUNNING;
	update_window_title(window);
	EnableMenuItem(g_menu, ID_STATE_SAVE, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(g_menu, ID_STATE_LOAD, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(g_menu, ID_FILE_SCREENSHOT,
		MF_BYCOMMAND | MF_ENABLED);
	DrawMenuBar(window);
	start_frame_clock(window);
	InvalidateRect(window, NULL, FALSE);
	return 1;
}

static int load_rom(HWND window, const wchar_t *path, int show_error)
{
	const interface_strings *text = interface_text();
	drgqst_rom_image image;
	drgqst_core *core;
	wchar_t error[384];

	memset(&image, 0, sizeof(image));
	if (!drgqst_rom_load_zip(path, &image, error,
		sizeof(error) / sizeof(error[0])))
	{
		if (show_error)
			MessageBoxW(window, text->rom_open_error,
				text->rom_open_title,
				MB_OK | MB_ICONERROR);
		return 0;
	}
	if (drgqst_rom_is_xavix2(image.kind))
	{
		int result = activate_xavix2_rom(window, &image, show_error);
		drgqst_rom_release(&image);
		return result;
	}

	core = (drgqst_core *)calloc(1, sizeof(*core));
	if (!core || !drgqst_core_init_profile(core, image.data, image.size,
		core_profile_for_rom(image.kind)))
	{
		free(core);
		drgqst_rom_release(&image);
		if (show_error)
			MessageBoxW(window, text->core_initialize_error,
				text->rom_open_title, MB_OK | MB_ICONERROR);
		return 0;
	}
	load_persistent_eeprom(window, core, image.kind);

	stop_frame_clock(window);
	save_persistent_eeprom(window, 1);
	free(g_core);
	free(g_xavix2);
	drgqst_rom_release(&g_rom);
	g_core = core;
	g_xavix2 = NULL;
	g_rom = image;
	g_ban_onep_menu_input = 0;
	g_ban_onep_menu_input_frames = 0;
	g_omt_backside = 0;
	g_ttv_spin_held = 0;
	g_ttv_spin_phase = 0;
	g_ttv_sw_motion_frames = 0;
	g_hamd_left_pulse_frames = 0;
	g_hamd_right_pulse_frames = 0;
	g_hamd_confirm_frames = 0;
	memset(g_tvpc_keyboard_rows, 0, sizeof(g_tvpc_keyboard_rows));
	g_tvpc_mouse_key_pending = 0;
	g_tvpc_mouse_key_active = 0;
	g_naruto_joined_hands = 0;
	g_naruto_execute_delay = 0;
	g_naruto_execute_frames = 0;
	g_tvpc_mouse_counter_x = 0;
	g_tvpc_mouse_counter_y = 0;
	g_tvpc_mouse_position_valid = 0;
	drgqst_cursor_presentation_reset(&g_cursor_presentation);
	g_eeprom_generation =
		g_core->machine.state.peripherals.eeprom.write_generation;
	g_eeprom_settle_frames = 0;
	g_parallel_nvram_generation = g_core->machine.nvram_write_generation;
	g_parallel_nvram_settle_frames = 0;
	g_parallel_nvram_save_pending = 0;
	win_audio_shutdown(&g_audio_output);
	win_audio_init(&g_audio_output);
	win_audio_open(&g_audio_output);
	update_core_mouse();
	g_framebuffer = drgqst_core_run_frame(g_core);
	g_frame_width = FRAME_WIDTH;
	g_frame_height = FRAME_HEIGHT;
	g_frame_stride = FRAME_WIDTH;
	update_cursor_presentation();
	win_audio_submit(&g_audio_output, drgqst_core_frame_audio(g_core),
		DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME);
	g_window_status = WINDOW_STATUS_RUNNING;
	update_window_title(window);
	EnableMenuItem(g_menu, ID_STATE_SAVE, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_STATE_LOAD, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_FILE_SCREENSHOT,
		MF_BYCOMMAND | MF_ENABLED);
	DrawMenuBar(window);
	start_frame_clock(window);
	InvalidateRect(window, NULL, FALSE);
	return 1;
}

static void show_open_dialog(HWND window)
{
	const interface_strings *text = interface_text();
	wchar_t path[32768] = L"";
	wchar_t games_directory[MAX_PATH];
	OPENFILENAMEW dialog;
	size_t base_length = wcslen(g_executable_directory);
	static const wchar_t games_suffix[] = L"\\Games";

	memset(&dialog, 0, sizeof(dialog));
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = window;
	dialog.lpstrFilter = text->open_filter;
	dialog.lpstrFile = path;
	dialog.nMaxFile = sizeof(path) / sizeof(path[0]);
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
	dialog.lpstrDefExt = L"zip";
	if (base_length + sizeof(games_suffix) / sizeof(games_suffix[0]) <=
		sizeof(games_directory) / sizeof(games_directory[0]))
	{
		memcpy(games_directory, g_executable_directory,
			base_length * sizeof(*games_directory));
		memcpy(games_directory + base_length, games_suffix,
			sizeof(games_suffix));
		if (GetFileAttributesW(games_directory) != INVALID_FILE_ATTRIBUTES)
			dialog.lpstrInitialDir = games_directory;
	}

	if (GetOpenFileNameW(&dialog))
		load_rom(window, path, 1);
}

static void draw_mouse_target(HDC device, const display_viewport *viewport)
{
	HPEN pen;
	HGDIOBJ old_pen;
	HGDIOBJ old_brush;
	int center_x;
	int center_y;
	int radius;

	if (!g_core || rom_has_internal_cursor(g_rom.kind) ||
		g_rom.kind == DRGQST_ROM_EPO_HAMD ||
		g_rom.kind == DRGQST_ROM_TVPC_DOR ||
		g_rom.kind == DRGQST_ROM_TAK_CHQ ||
		g_rom.kind == DRGQST_ROM_EPO_ES2J ||
		g_rom.kind == DRGQST_ROM_EPO_HAMC ||
		rom_uses_digital_direction_input(g_rom.kind) ||
		(!rom_uses_camera(g_rom.kind) &&
		drgqst_core_feather_visible(g_core)))
		return;
	if (rom_uses_camera(g_rom.kind))
	{
		center_x = viewport->x +
			(g_mouse_x * (viewport->width - 1) + 127) / 255;
		center_y = viewport->y +
			(g_mouse_y * (viewport->height - 1) + 127) / 255;
	}
	else
		drgqst_cursor_presentation_map_viewport(&g_cursor_presentation,
			viewport->x, viewport->y, viewport->width, viewport->height,
			&center_x, &center_y);
	radius = 6 * viewport->scale;
	if (radius < 6)
		radius = 6;
	pen = CreatePen(PS_SOLID, viewport->scale > 1 ? viewport->scale : 1,
		RGB(53, 61, 210));
	old_pen = SelectObject(device, pen);
	old_brush = SelectObject(device, GetStockObject(NULL_BRUSH));
	Ellipse(device, center_x - radius, center_y - radius,
		center_x + radius, center_y + radius);
	MoveToEx(device, center_x - radius - 2, center_y - radius - 2, NULL);
	LineTo(device, center_x + radius + 3, center_y + radius + 3);
	MoveToEx(device, center_x + radius + 2, center_y - radius - 2, NULL);
	LineTo(device, center_x - radius - 3, center_y + radius + 3);
	SelectObject(device, old_brush);
	SelectObject(device, old_pen);
	DeleteObject(pen);
}

static void fill_letterbox(HDC device, const RECT *client,
	const display_viewport *viewport)
{
	RECT bar;
	HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);

	if (viewport->y > client->top)
	{
		SetRect(&bar, client->left, client->top,
			client->right, viewport->y);
		FillRect(device, &bar, black);
	}
	if (viewport->y + viewport->height < client->bottom)
	{
		SetRect(&bar, client->left, viewport->y + viewport->height,
			client->right, client->bottom);
		FillRect(device, &bar, black);
	}
	if (viewport->x > client->left)
	{
		SetRect(&bar, client->left, viewport->y,
			viewport->x, viewport->y + viewport->height);
		FillRect(device, &bar, black);
	}
	if (viewport->x + viewport->width < client->right)
	{
		SetRect(&bar, viewport->x + viewport->width, viewport->y,
			client->right, viewport->y + viewport->height);
		FillRect(device, &bar, black);
	}
}

static int render_display(HDC device, const RECT *client,
	const display_viewport *viewport)
{
	BITMAPINFO bitmap;
	int result;

	fill_letterbox(device, client, viewport);
	memset(&bitmap, 0, sizeof(bitmap));
	bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
	bitmap.bmiHeader.biWidth = (LONG)g_frame_stride;
	bitmap.bmiHeader.biHeight = -(LONG)g_frame_height;
	bitmap.bmiHeader.biPlanes = 1;
	bitmap.bmiHeader.biBitCount = 32;
	bitmap.bmiHeader.biCompression = BI_RGB;

	SetStretchBltMode(device, COLORONCOLOR);
	result = StretchDIBits(device,
		viewport->x, viewport->y, viewport->width, viewport->height,
		0, 0, (int)g_frame_width, (int)g_frame_height,
		g_framebuffer, &bitmap, DIB_RGB_COLORS, SRCCOPY);
	draw_mouse_target(device, viewport);
	return result != 0 && (DWORD)result != GDI_ERROR;
}

static void paint_window(HWND window)
{
	PAINTSTRUCT paint;
	HDC device = BeginPaint(window, &paint);
	RECT client;
	display_viewport viewport = calculate_viewport(window);

	GetClientRect(window, &client);
	(void)render_display(device, &client, &viewport);
	EndPaint(window, &paint);
}

static void release_capture_surface(void)
{
	if (g_capture_dc && g_capture_old_bitmap)
		SelectObject(g_capture_dc, g_capture_old_bitmap);
	if (g_capture_bitmap)
		DeleteObject(g_capture_bitmap);
	if (g_capture_dc)
		DeleteDC(g_capture_dc);
	g_capture_dc = NULL;
	g_capture_bitmap = NULL;
	g_capture_old_bitmap = NULL;
	g_capture_width = 0;
	g_capture_height = 0;
}

static int ensure_capture_surface(HDC device, int width, int height)
{
	HBITMAP bitmap;
	HDC memory;
	HGDIOBJ old_bitmap;

	if (width <= 0 || height <= 0)
		return 0;
	if (g_capture_dc && g_capture_width == width &&
		g_capture_height == height)
		return 1;
	release_capture_surface();
	memory = CreateCompatibleDC(device);
	if (!memory)
		return 0;
	bitmap = CreateCompatibleBitmap(device, width, height);
	if (!bitmap)
	{
		DeleteDC(memory);
		return 0;
	}
	old_bitmap = SelectObject(memory, bitmap);
	if (!old_bitmap || old_bitmap == HGDI_ERROR)
	{
		DeleteObject(bitmap);
		DeleteDC(memory);
		return 0;
	}
	g_capture_dc = memory;
	g_capture_bitmap = bitmap;
	g_capture_old_bitmap = old_bitmap;
	g_capture_width = width;
	g_capture_height = height;
	return 1;
}

static void capture_screenshot(HWND window)
{
	const interface_strings *text = interface_text();
	display_viewport viewport;
	RECT source_rectangle;
	RECT client;
	HDC device = NULL;
	wchar_t error[384];
	wchar_t message[768];

	if (!emulator_loaded())
		return;
	GetClientRect(window, &client);
	device = GetDC(window);
	if (!device || !ensure_capture_surface(device,
		client.right - client.left, client.bottom - client.top))
	{
		if (device)
			ReleaseDC(window, device);
		lstrcpyW(error, L"The screenshot surface is not available.");
		goto failure;
	}
	ReleaseDC(window, device);
	device = NULL;
	viewport = calculate_viewport(window);
	if (!render_display(g_capture_dc, &client, &viewport))
	{
		lstrcpyW(error, L"The current frame could not be rendered for capture.");
		goto failure;
	}
	GdiFlush();
	SetRect(&source_rectangle, viewport.x, viewport.y,
		viewport.x + viewport.width, viewport.y + viewport.height);
	if (source_rectangle.left < 0 || source_rectangle.top < 0 ||
		source_rectangle.right > g_capture_width ||
		source_rectangle.bottom > g_capture_height)
	{
		lstrcpyW(error, L"The display viewport is outside the screenshot surface.");
		goto failure;
	}
	if (!xavix_screenshot_save_png(g_capture_dc, &source_rectangle,
		g_executable_directory, NULL, 0, error,
		sizeof(error) / sizeof(error[0])))
		goto failure;
	g_window_status = WINDOW_STATUS_SCREENSHOT_SAVED;
	update_window_title(window);
	return;

failure:
	_snwprintf(message, sizeof(message) / sizeof(message[0]),
		L"%ls\r\n\r\n%ls", text->screenshot_save_error, error);
	message[sizeof(message) / sizeof(message[0]) - 1] = L'\0';
	MessageBoxW(window, message, text->screenshot_save_title,
		MB_OK | MB_ICONERROR);
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message,
	WPARAM wparam, LPARAM lparam)
{
	switch (message)
	{
	case WM_COMMAND:
		switch (LOWORD(wparam))
		{
		case ID_FILE_OPEN:
			show_open_dialog(window);
			return 0;
		case ID_FILE_SCREENSHOT:
			capture_screenshot(window);
			return 0;
		case ID_FILE_EXIT:
			DestroyWindow(window);
			return 0;
		case ID_STATE_SAVE:
			save_runtime_state(window);
			return 0;
		case ID_STATE_LOAD:
			load_runtime_state(window);
			return 0;
		case ID_VIEW_SCALE_1:
		case ID_VIEW_SCALE_2:
		case ID_VIEW_SCALE_3:
		case ID_VIEW_SCALE_4:
			resize_for_scale(window, LOWORD(wparam) - ID_VIEW_SCALE_1 + 1);
			return 0;
		case ID_VIEW_MAXIMIZE:
			if (!g_fullscreen)
				ShowWindow(window, SW_MAXIMIZE);
			return 0;
		case ID_VIEW_STRETCH_4_3:
			toggle_stretch_4_3(window);
			return 0;
		case ID_VIEW_FULLSCREEN:
			toggle_fullscreen(window);
			return 0;
		case ID_LANGUAGE_ZH_TW:
			set_interface_language(window, LANGUAGE_ZH_TW);
			return 0;
		case ID_LANGUAGE_ENGLISH:
			set_interface_language(window, LANGUAGE_ENGLISH);
			return 0;
		case ID_HELP_ABOUT:
			show_about(window);
			return 0;
		default:
			break;
		}
		break;

	case WM_TIMER:
		if (wparam == ID_EMULATION_TIMER)
		{
			run_due_frames(window);
			return 0;
		}
		break;

	case WM_ACTIVATEAPP:
		if (!wparam)
			release_held_host_inputs(window);
		return 0;

	case WM_MOUSEMOVE:
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_MOUSEWHEEL:
		if (GET_WHEEL_DELTA_WPARAM(wparam))
			pulse_ban_onep_menu_input(
				GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 0x08 : 0x04);
		return 0;

	case WM_LBUTTONDOWN:
		SetFocus(window);
		SetCapture(window);
		g_left_button = 1;
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_left_pulse_frames = 4;
		if (g_xavix2)
		{
			g_naruto_execute_delay = 2;
			g_naruto_execute_frames = 4;
		}
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_LBUTTONUP:
		g_left_button = 0;
		if (!g_right_button)
			ReleaseCapture();
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_RBUTTONDOWN:
		SetFocus(window);
		SetCapture(window);
		g_right_button = 1;
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_right_pulse_frames = 4;
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_RBUTTONUP:
		g_right_button = 0;
		if (!g_left_button)
			ReleaseCapture();
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_MBUTTONDOWN:
		SetFocus(window);
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_confirm_frames = 4;
		return 0;

	case WM_CAPTURECHANGED:
		if ((HWND)lparam != window)
		{
			g_left_button = 0;
			g_right_button = 0;
			update_core_mouse();
		}
		return 0;

	case WM_SETCURSOR:
		if (LOWORD(lparam) == HTCLIENT && emulator_loaded())
		{
			SetCursor(NULL);
			return TRUE;
		}
		break;

	case WM_SYSKEYDOWN:
		if (wparam == VK_RETURN && (lparam & ((LPARAM)1 << 29)) &&
			!(lparam & ((LPARAM)1 << 30)))
		{
			toggle_fullscreen(window);
			return 0;
		}
		break;

	case WM_KEYDOWN:
		if (wparam == VK_F10 && !(lparam & ((LPARAM)1 << 30)))
		{
			LARGE_INTEGER now;
			g_timing_diagnostics = !g_timing_diagnostics;
			QueryPerformanceCounter(&now);
			g_timing_window_counter = now.QuadPart;
			g_timing_core_counter = 0;
			g_timing_frames = 0;
			g_timing_dropped_frames = 0;
			g_timing_guest_cycles = g_xavix2 ?
				g_xavix2->cpu.total_cycles : 0;
			g_timing_interrupts = g_xavix2 ?
				g_xavix2->cpu.interrupt_count : 0;
			if (!g_timing_diagnostics)
				update_window_title(window);
			return 0;
		}
		if (wparam == VK_SPACE && g_xavix2)
		{
			g_naruto_joined_hands = 1;
			return 0;
		}
		if (wparam == VK_SPACE && g_core &&
			g_rom.kind == DRGQST_ROM_EPO_HAMD)
		{
			if (!(lparam & ((LPARAM)1 << 30)))
			{
				g_hamd_left_pulse_frames = 4;
				g_hamd_right_pulse_frames = 4;
			}
			return 0;
		}
		if (wparam == VK_RETURN && g_core &&
			g_rom.kind == DRGQST_ROM_EPO_HAMD)
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				g_hamd_confirm_frames = 4;
			return 0;
		}
		if (g_core && g_rom.kind == DRGQST_ROM_TVPC_DOR &&
			(wparam == VK_UP || wparam == VK_DOWN ||
			 wparam == VK_LEFT || wparam == VK_RIGHT ||
			 wparam == 'W' || wparam == 'A' ||
			 wparam == 'S' || wparam == 'D'))
		{
			set_tvpc_cursor_key(wparam, 1);
			return 0;
		}
		if (wparam == VK_ESCAPE && g_core && !g_fullscreen &&
			g_rom.kind == DRGQST_ROM_TVPC_DOR)
		{
			g_tvpc_keyboard_rows[0] |= 0x40;
			return 0;
		}
		if (wparam == VK_SPACE && g_core &&
			(g_rom.kind == DRGQST_ROM_TTV_SW ||
			 g_rom.kind == DRGQST_ROM_TTV_SWJ))
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				g_ttv_spin_phase = 0;
			g_ttv_spin_held = 1;
			update_core_mouse();
			return 0;
		}
		if (wparam == VK_SPACE && g_core &&
			g_rom.kind == DRGQST_ROM_BAN_OMT)
		{
			g_omt_backside = 1;
			update_core_mouse();
			return 0;
		}
		if (wparam == VK_SPACE && g_core &&
			g_rom.kind == DRGQST_ROM_BAN_ONEP)
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				drgqst_core_trigger_bazooka(g_core);
			return 0;
		}
		if ((wparam == VK_UP || wparam == VK_DOWN) && g_core &&
			rom_uses_camera(g_rom.kind))
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				pulse_ban_onep_menu_input(wparam == VK_UP ? 0x08 : 0x04);
			return 0;
		}
		if (wparam == VK_F5 && g_core)
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				save_runtime_state(window);
			return 0;
		}
		if (wparam == VK_F7 && g_core)
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				load_runtime_state(window);
			return 0;
		}
		if (wparam == VK_F8 && emulator_loaded())
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				capture_screenshot(window);
			return 0;
		}
		if (wparam == VK_ESCAPE && g_fullscreen)
		{
			toggle_fullscreen(window);
			return 0;
		}
		break;

	case WM_KEYUP:
		if (wparam == VK_SPACE && g_xavix2)
		{
			g_naruto_joined_hands = 0;
			return 0;
		}
		if (g_rom.kind == DRGQST_ROM_TVPC_DOR &&
			(wparam == VK_UP || wparam == VK_DOWN ||
			 wparam == VK_LEFT || wparam == VK_RIGHT ||
			 wparam == 'W' || wparam == 'A' ||
			 wparam == 'S' || wparam == 'D'))
		{
			set_tvpc_cursor_key(wparam, 0);
			return 0;
		}
		if (wparam == VK_ESCAPE && g_rom.kind == DRGQST_ROM_TVPC_DOR)
		{
			g_tvpc_keyboard_rows[0] &= (uint8_t)~0x40;
			return 0;
		}
		if (wparam == VK_SPACE && g_rom.kind == DRGQST_ROM_BAN_OMT)
		{
			g_omt_backside = 0;
			update_core_mouse();
			return 0;
		}
		if (wparam == VK_SPACE &&
			(g_rom.kind == DRGQST_ROM_TTV_SW ||
			 g_rom.kind == DRGQST_ROM_TTV_SWJ))
		{
			g_ttv_spin_held = 0;
			update_core_mouse();
			return 0;
		}
		break;

	case WM_DROPFILES:
		{
			HDROP drop = (HDROP)wparam;
			wchar_t path[32768];
			if (DragQueryFileW(drop, 0, path, sizeof(path) / sizeof(path[0])))
				load_rom(window, path, 1);
			DragFinish(drop);
			return 0;
		}

	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *limits = (MINMAXINFO *)lparam;
			int minimum_width = g_stretch_4_3 ?
				((int)g_frame_height * 4 + 1) / 3 :
				(int)g_frame_width;
			RECT rectangle = { 0, 0, minimum_width,
				(int)g_frame_height };
			AdjustWindowRectEx(&rectangle,
				(DWORD)GetWindowLongPtrW(window, GWL_STYLE), TRUE,
				(DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE));
			limits->ptMinTrackSize.x = rectangle.right - rectangle.left;
			limits->ptMinTrackSize.y = rectangle.bottom - rectangle.top;
			return 0;
		}

	case WM_PAINT:
		paint_window(window);
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_DESTROY:
		stop_frame_clock(window);
		save_persistent_eeprom(window, 1);
		win_audio_shutdown(&g_audio_output);
		release_capture_surface();
		timeEndPeriod(1);
		free(g_core);
		g_core = NULL;
		free(g_xavix2);
		g_xavix2 = NULL;
		drgqst_rom_release(&g_rom);
		PostQuitMessage(0);
		return 0;

	default:
		break;
	}

	return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
	LPSTR command_line, int show_command)
{
	WNDCLASSEXW window_class;
	HWND window;
	MSG message;
	LPWSTR *arguments;
	int argument_count = 0;
	int verify_only = 0;
	const wchar_t *initial_rom = NULL;

	(void)previous_instance;
	(void)command_line;
	{
		wchar_t timing_value[8];
		DWORD timing_length = GetEnvironmentVariableW(L"XAVIXEMU_TIMING",
			timing_value, sizeof(timing_value) / sizeof(timing_value[0]));
		g_timing_diagnostics = timing_length &&
			!(timing_length == 1 && timing_value[0] == L'0');
	}
	win_audio_init(&g_audio_output);
	arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
	if (arguments && argument_count >= 3 &&
		lstrcmpiW(arguments[1], L"--verify-rom") == 0)
	{
		initial_rom = arguments[2];
		verify_only = 1;
	}
	else if (arguments && argument_count >= 2)
		initial_rom = arguments[1];

	if (verify_only)
	{
		drgqst_rom_image image;
		wchar_t error[384];
		int result;
		memset(&image, 0, sizeof(image));
		result = drgqst_rom_load_zip(initial_rom, &image, error,
			sizeof(error) / sizeof(error[0]));
		drgqst_rom_release(&image);
		LocalFree(arguments);
		return result ? 0 : 2;
	}
	if (!initialize_executable_directory())
	{
		MessageBoxW(NULL, interface_text()->storage_directory_error,
			interface_text()->window_title_idle, MB_OK | MB_ICONERROR);
		if (arguments)
			LocalFree(arguments);
		return 1;
	}
	SetProcessDPIAware();
	apply_system_ui_language(g_language);
	timeBeginPeriod(1);

	memset(&window_class, 0, sizeof(window_class));
	window_class.cbSize = sizeof(window_class);
	window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	window_class.lpfnWndProc = window_procedure;
	window_class.hInstance = instance;
	window_class.hIcon = (HICON)LoadImageW(instance,
		MAKEINTRESOURCEW(IDI_XAVIXEMU), IMAGE_ICON, 32, 32,
		LR_DEFAULTCOLOR | LR_SHARED);
	window_class.hIconSm = (HICON)LoadImageW(instance,
		MAKEINTRESOURCEW(IDI_XAVIXEMU), IMAGE_ICON, 16, 16,
		LR_DEFAULTCOLOR | LR_SHARED);
	window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
	window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	window_class.lpszClassName = WINDOW_CLASS_NAME;
	if (!RegisterClassExW(&window_class))
	{
		if (arguments)
			LocalFree(arguments);
		timeEndPeriod(1);
		return 1;
	}

	g_menu = create_application_menu();
	initialize_idle_framebuffer();
	window = CreateWindowExW(0, WINDOW_CLASS_NAME,
		interface_text()->window_title_idle,
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		FRAME_WIDTH * DEFAULT_SCALE, FRAME_HEIGHT * DEFAULT_SCALE,
		NULL, g_menu, instance, NULL);
	if (!window)
	{
		if (arguments)
			LocalFree(arguments);
		timeEndPeriod(1);
		return 1;
	}

	DragAcceptFiles(window, TRUE);
	resize_for_scale(window, DEFAULT_SCALE);
	ShowWindow(window, show_command);
	UpdateWindow(window);
	if (initial_rom)
		load_rom(window, initial_rom, 1);
	if (arguments)
		LocalFree(arguments);

	while (GetMessageW(&message, NULL, 0, 0) > 0)
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return (int)message.wParam;
}
