// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "core/drgqst_core.h"
#include "core/drgqst_state.h"
#include "controller_input.h"
#include "cursor_presentation.h"
#include "game_controls.h"
#include "game_library.h"
#include "persistence.h"
#include "resource.h"
#include "rom_loader.h"
#include "screenshot.h"
#include "video_recorder.h"
#include "win_audio.h"
#include "xavix2/xavix2_machine.h"

#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

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
	ID_FILE_GAME_LIBRARY,
	ID_FILE_SET_ROM_DIRECTORY,
	ID_FILE_REFRESH_LIBRARY,
	ID_FILE_SCREENSHOT,
	ID_FILE_EXIT,
	ID_LIBRARY_CHOOSE_DIRECTORY = 180,
	ID_LIBRARY_REFRESH,
	ID_LIBRARY_OPEN_GAME,
	ID_LIBRARY_CONTROLLER_SETTINGS,
	ID_STATE_SAVE,
	ID_STATE_LOAD,
	ID_VIEW_SCALE_1,
	ID_VIEW_SCALE_2,
	ID_VIEW_SCALE_3,
	ID_VIEW_SCALE_4,
	ID_VIEW_MAXIMIZE,
	ID_VIEW_STRETCH_4_3,
	ID_VIEW_HIGH_RESOLUTION_3D,
	ID_VIEW_RECORD_WINDOW_SIZE,
	ID_VIEW_RECORD_FORMAT_AVI,
	ID_VIEW_RECORD_FORMAT_MP4,
	ID_VIEW_FULLSCREEN,
	ID_LANGUAGE_AUTO,
	ID_LANGUAGE_ZH_TW,
	ID_LANGUAGE_JAPANESE,
	ID_LANGUAGE_FRENCH,
	ID_LANGUAGE_ENGLISH,
	ID_CONTROLLER_SETTINGS,
	ID_HELP_ABOUT,
	ID_XAVIX2_AUDIO_ENABLE_ALL = 900,
	ID_XAVIX2_AUDIO_MUTE_ALL = 901,
	ID_XAVIX2_AUDIO_CHANNEL_FIRST = 902,
	ID_XAVIX2_AUDIO_CHANNEL_LAST =
		ID_XAVIX2_AUDIO_CHANNEL_FIRST + XAVIX2_AUDIO_VOICES - 1
};

enum interface_language
{
	LANGUAGE_ZH_TW,
	LANGUAGE_ENGLISH,
	LANGUAGE_JAPANESE,
	LANGUAGE_FRENCH
};

enum language_preference
{
	LANGUAGE_PREFERENCE_AUTO,
	LANGUAGE_PREFERENCE_ZH_TW,
	LANGUAGE_PREFERENCE_JAPANESE,
	LANGUAGE_PREFERENCE_FRENCH,
	LANGUAGE_PREFERENCE_ENGLISH
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
	const wchar_t *menu_high_resolution_3d;
	const wchar_t *menu_record_window_size;
	const wchar_t *menu_record_format;
	const wchar_t *menu_record_avi;
	const wchar_t *menu_record_mp4;
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

static int render_display(HDC device, const RECT *client,
	const display_viewport *viewport);
static void set_game_library_visible(HWND window, int visible);
static void update_game_library_labels(void);

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
		L"3D 畫質強化（快速模式）(&H)",
		L"依目前顯示大小錄影(&R)",
		L"錄影格式(&O)",
		L"AVI（MJPEG）(&A)",
		L"MP4（H.264 / AAC）(&M)",
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
		L"3D quality enhancement (fast) (&H)",
		L"Record at current display &size",
		L"Recording f&ormat",
		L"&AVI (MJPEG)",
		L"&MP4 (H.264 / AAC)",
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
	},
	{
		.window_title_idle = L"XaviXEmu",
		.window_title_running = L"XaviXEmu - ゲーム実行中",
		.window_title_state_saved = L"XaviXEmu - ステートを保存しました",
		.window_title_state_loaded = L"XaviXEmu - ステートを読み込みました",
		.window_title_screenshot_saved = L"XaviXEmu - スクリーンショットを保存しました",
		.menu_file = L"ファイル(&F)",
		.menu_open = L"ROM ZIPを開く(&O)...",
		.menu_screenshot = L"スクリーンショットを保存(&P)\tF8",
		.menu_exit = L"終了(&X)",
		.menu_state = L"ステート(&S)",
		.menu_save_state = L"ステートを保存(&S)\tF5",
		.menu_load_state = L"ステートを読み込む(&L)\tF7",
		.menu_view = L"表示(&V)",
		.menu_scale_1 = L"1倍ウィンドウ(&1)",
		.menu_scale_2 = L"2倍ウィンドウ(&2)",
		.menu_scale_3 = L"3倍ウィンドウ(&3)",
		.menu_scale_4 = L"4倍ウィンドウ(&4)",
		.menu_maximize = L"ウィンドウを最大化(&M)",
		.menu_stretch_4_3 = L"4:3表示(&A)",
		.menu_high_resolution_3d = L"3D画質強化（高速）(&H)",
		.menu_record_window_size = L"現在の表示サイズで録画(&R)",
		.menu_record_format = L"録画形式(&O)",
		.menu_record_avi = L"AVI（MJPEG）(&A)",
		.menu_record_mp4 = L"MP4（H.264 / AAC）(&M)",
		.menu_fullscreen = L"フルスクリーン(&F)\tAlt+Enter",
		.menu_language = L"言語(&L)",
		.menu_zh_tw = L"繁體中文 (&T)",
		.menu_english = L"English (&E)",
		.menu_help = L"ヘルプ(&H)",
		.menu_about = L"XaviXEmuについて(&A)",
		.about_title = L"XaviXEmuについて",
		.about_text = L"XaviXEmuはXaviXエミュレーターです。\r\n"
			L"現在、XaviXおよびXaviX 2タイトルを実験的にサポートしています。\r\n\r\n"
			L"マウス、キーボード、ゲーム固有の体感操作については、\r\n"
			L"同梱のcontrols.mdをご覧ください。\r\n\r\nBilly Jr",
		.about_facebook_button = L"Billy Jr.のエミュレーター世界",
		.about_support_text = L"開発を応援していただける場合は\r\nページの購読も歓迎します。",
		.about_subscribe_button = L"ページを購読（いつでも解除できます）",
		.about_close_button = L"OK",
		.open_filter = L"対応ROM ZIP (*.zip)\0*.zip\0すべてのファイル (*.*)\0*.*\0\0",
		.eeprom_save_title = L"ゲームデータを保存できませんでした",
		.eeprom_save_error = L"ゲーム内セーブを書き込めません。空き容量とフォルダーの権限を確認してください。",
		.state_save_title = L"ステート保存に失敗しました",
		.state_save_memory_error = L"メモリ不足のためステートを作成できません。",
		.state_save_error = L"ステートを書き込めません。空き容量とフォルダーの権限を確認してください。",
		.state_load_title = L"ステート読み込みに失敗しました",
		.state_load_memory_error = L"メモリ不足のためステートを読み込めません。",
		.state_load_error = L"ステートが見つからないか、読み込めません。",
		.state_incompatible_error = L"ステートに互換性がないか、破損しています。",
		.rom_open_title = L"ROMを開けませんでした",
		.rom_open_error = L"このROM ZIPを開けません。XaviXEmuが対応しているROMを選択してください。",
		.core_initialize_error = L"エミュレーターコアを初期化できませんでした。",
		.screenshot_save_title = L"スクリーンショットの保存に失敗しました",
		.screenshot_save_error = L"PNG画像を実行ファイル横のsnapフォルダーに保存できません。",
		.storage_directory_error = L"XaviXEmu.exeのあるフォルダーを取得または初期化できません。",
		.link_open_title = L"Webページを開けませんでした",
		.link_open_error = L"既定のブラウザーでリンクを開けませんでした。"
	},
	{
		.window_title_idle = L"XaviXEmu",
		.window_title_running = L"XaviXEmu - jeu en cours",
		.window_title_state_saved = L"XaviXEmu - état sauvegardé",
		.window_title_state_loaded = L"XaviXEmu - état chargé",
		.window_title_screenshot_saved = L"XaviXEmu - capture enregistrée",
		.menu_file = L"&Fichier",
		.menu_open = L"&Ouvrir une archive ROM ZIP...",
		.menu_screenshot = L"Enregistrer une &capture\tF8",
		.menu_exit = L"&Quitter",
		.menu_state = L"É&tat",
		.menu_save_state = L"&Sauvegarder l'état\tF5",
		.menu_load_state = L"&Charger l'état\tF7",
		.menu_view = L"&Affichage",
		.menu_scale_1 = L"Fenêtre &1x",
		.menu_scale_2 = L"Fenêtre &2x",
		.menu_scale_3 = L"Fenêtre &3x",
		.menu_scale_4 = L"Fenêtre &4x",
		.menu_maximize = L"&Agrandir la fenêtre",
		.menu_stretch_4_3 = L"Format d'affichage &4:3",
		.menu_high_resolution_3d = L"Amélioration 3D (rapide) (&H)",
		.menu_record_window_size = L"Enregistrer à la taille d'affichage actuelle",
		.menu_record_format = L"Format d'enregistrement (&O)",
		.menu_record_avi = L"&AVI (MJPEG)",
		.menu_record_mp4 = L"&MP4 (H.264 / AAC)",
		.menu_fullscreen = L"&Plein écran\tAlt+Entrée",
		.menu_language = L"&Langue",
		.menu_zh_tw = L"繁體中文 (&T)",
		.menu_english = L"&English",
		.menu_help = L"&Aide",
		.menu_about = L"À &propos de XaviXEmu",
		.about_title = L"À propos de XaviXEmu",
		.about_text = L"XaviXEmu est un émulateur XaviX.\r\n"
			L"Il prend en charge à titre expérimental des jeux XaviX, "
			L"XaviX 2000 et XaviX 2.\r\n\r\n"
			L"Consultez controls.md pour les commandes de mouvement propres "
			L"à chaque jeu.\r\n\r\nBilly Jr",
		.about_facebook_button = L"Le monde de l'émulation de Billy Jr.",
		.about_support_text = L"Pour signaler un problème ou soutenir le projet,\r\n"
			L"vous pouvez visiter notre page Facebook.",
		.about_subscribe_button = L"S'abonner à la page (annulable à tout moment)",
		.about_close_button = L"OK",
		.open_filter = L"Archives ROM ZIP prises en charge (*.zip)\0*.zip\0Tous les fichiers (*.*)\0*.*\0\0",
		.eeprom_save_title = L"Impossible d'enregistrer la progression",
		.eeprom_save_error = L"Impossible d'écrire la sauvegarde du jeu. Vérifiez l'espace disponible et les autorisations du dossier.",
		.state_save_title = L"Échec de la sauvegarde d'état",
		.state_save_memory_error = L"Mémoire insuffisante pour créer l'état.",
		.state_save_error = L"Impossible d'écrire l'état. Vérifiez l'espace disponible et les autorisations du dossier.",
		.state_load_title = L"Échec du chargement de l'état",
		.state_load_memory_error = L"Mémoire insuffisante pour charger l'état.",
		.state_load_error = L"L'état est introuvable ou illisible.",
		.state_incompatible_error = L"L'état est incompatible ou endommagé.",
		.rom_open_title = L"Impossible d'ouvrir la ROM",
		.rom_open_error = L"Impossible d'ouvrir cette archive ROM ZIP. Sélectionnez une image prise en charge par XaviXEmu.",
		.core_initialize_error = L"Impossible d'initialiser le cœur de l'émulateur.",
		.screenshot_save_title = L"Échec de la capture d'écran",
		.screenshot_save_error = L"Impossible d'enregistrer l'image PNG dans le dossier snap près de l'exécutable.",
		.storage_directory_error = L"Impossible d'initialiser le dossier contenant XaviXEmu.exe.",
		.link_open_title = L"Impossible d'ouvrir la page Web",
		.link_open_error = L"Impossible d'ouvrir ce lien avec le navigateur par défaut."
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
static const uint8_t BAN_NARU_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x13, 0xe3, 0xd2, 0xde, 0x5d, 0x5a, 0x08, 0x46, 0x35, 0xca,
	0xb1, 0x58, 0xf3, 0x63, 0x9a, 0x1e, 0xa7, 0x32, 0x65, 0xdc
};
static const uint8_t BAN_BLDJ_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x2f, 0x5f, 0x48, 0x09, 0xa0, 0x7a, 0x2f, 0x56, 0x71, 0xf8,
	0x1a, 0xa2, 0x2e, 0x37, 0x9c, 0x11, 0xc4, 0x39, 0x43, 0xa0
};
static const uint8_t BAN_DB2J_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xf1, 0x88, 0x04, 0x70, 0xf0, 0xdb, 0x56, 0x13, 0x5d, 0x9b,
	0xc8, 0x8d, 0x71, 0x93, 0xd0, 0x37, 0xac, 0x49, 0xb9, 0x96
};
static const uint8_t BAN_DBZ_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x6c, 0x74, 0x6a, 0xf7, 0x63, 0x27, 0x3b, 0xd9, 0xe4, 0x79,
	0x29, 0xc3, 0xba, 0x85, 0x7c, 0x7a, 0xf5, 0x63, 0xbf, 0x79
};
static const uint8_t EPO_DAB2J_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xa2, 0xf9, 0x30, 0xf4, 0xff, 0xe7, 0x78, 0xe0, 0x25, 0x56,
	0xb5, 0xe1, 0xa1, 0x83, 0x6f, 0x88, 0x88, 0x8e, 0x7c, 0x82
};
static const uint8_t EPO_DTCJ_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x14, 0xf0, 0x2e, 0xb0, 0x1f, 0x1c, 0x6e, 0x76, 0x20, 0x2f,
	0x7a, 0x70, 0x81, 0x8c, 0x30, 0x0b, 0xa2, 0x3f, 0xd8, 0x79
};
static const uint8_t EPO_PABJ_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x06, 0xc2, 0xb4, 0x93, 0x82, 0x40, 0x85, 0x50, 0x2e, 0x96,
	0xa7, 0xc1, 0xe4, 0x6e, 0x9e, 0x89, 0x43, 0x3e, 0x73, 0x01
};
static const uint8_t EPO_SSK2_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x01, 0x0b, 0xc2, 0x41, 0x78, 0x14, 0xde, 0xd2, 0x4a, 0x47,
	0x4d, 0x91, 0x65, 0xf6, 0xb9, 0x52, 0x3a, 0xf7, 0xd1, 0xef
};
static const uint8_t EPO_SSKJ_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xcd, 0xa2, 0x7b, 0xd1, 0xc7, 0xd6, 0xcc, 0xdb, 0x6d, 0xa0,
	0x6c, 0xd8, 0x37, 0xaa, 0x9c, 0xde, 0x5a, 0x58, 0xe5, 0xe4
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
static const uint8_t TOM_DPGM_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0xfa, 0x30, 0x06, 0x9d, 0x17, 0x70, 0x5f, 0x27, 0xe4, 0xff,
	0x45, 0xe7, 0xf6, 0xcc, 0xf0, 0x69, 0x86, 0xe1, 0x38, 0xf3
};
static const uint8_t EPO_MINI_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{
	0x98, 0x72, 0x18, 0xb6, 0x79, 0x91, 0x95, 0xba, 0x15, 0xad,
	0xf3, 0x98, 0x85, 0xc1, 0xd1, 0x77, 0xc3, 0x81, 0xec, 0x26
};
static const uint8_t RAD_MTRK_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0x79, 0x53, 0xcf, 0x29, 0x64, 0x36, 0x72, 0xf8, 0x36, 0x76,
  0x39, 0x55, 0x5b, 0x79, 0x7c, 0x20, 0xbb, 0x53, 0x3e, 0xab };
static const uint8_t RAD_SNOW_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0x03, 0x48, 0x3a, 0xc3, 0x9e, 0xdd, 0xd7, 0x74, 0x64, 0x70,
  0xfb, 0x60, 0x01, 0x8e, 0x70, 0x43, 0x82, 0xb0, 0xda, 0x59 };
static const uint8_t RAD_SSX_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0x3d, 0xfb, 0x18, 0xef, 0xb6, 0x33, 0x1b, 0x96, 0xa5, 0x31,
  0x38, 0xa5, 0xba, 0x29, 0xda, 0xe9, 0xcd, 0x96, 0x6e, 0x90 };
static const uint8_t RAD_SBW_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0xd3, 0x7d, 0x14, 0x84, 0xa5, 0xb1, 0x47, 0x35, 0xb3, 0x5a,
  0xfb, 0xca, 0x30, 0x5d, 0xad, 0x7d, 0x17, 0x8b, 0x08, 0xa2 };
static const uint8_t TAK_GIN_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0xab, 0x08, 0x79, 0x0e, 0x95, 0xcd, 0xcc, 0xf3, 0x54, 0x1e,
  0xcb, 0xdd, 0xb8, 0x7e, 0xbf, 0x0d, 0xed, 0xb3, 0x71, 0x8b };
static const uint8_t TCARNAVI_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0xbe, 0x37, 0xb3, 0x5f, 0x1e, 0x1e, 0x66, 0x1e, 0x10, 0x18,
  0x72, 0x53, 0xc2, 0xc3, 0xaa, 0x98, 0x58, 0xa9, 0x08, 0x12 };
static const uint8_t TOMTHR_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0x67, 0x6b, 0x2a, 0x90, 0x5b, 0x75, 0x73, 0x56, 0xc6, 0xc1,
  0xdf, 0xe3, 0xf1, 0x01, 0x48, 0x48, 0x4c, 0xaa, 0x44, 0xc5 };
static const uint8_t EPO_CROK_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0xe6, 0xe4, 0x23, 0x5d, 0xc7, 0xc7, 0xdb, 0x30, 0x73, 0x73,
  0x7b, 0x10, 0xba, 0x4b, 0xc5, 0xb0, 0x0d, 0xec, 0xa2, 0xc3 };
static const uint8_t TAK_ZUBA_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0xba, 0x68, 0x7f, 0xc9, 0x55, 0x03, 0x22, 0x3d, 0xd4, 0x84,
  0xed, 0x95, 0x33, 0xdc, 0xb0, 0x97, 0xec, 0xfe, 0xa0, 0x0d };
static const uint8_t DUELMAST_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0xd8, 0x84, 0x9c, 0x74, 0x83, 0x3e, 0x77, 0xb8, 0xb3, 0x09,
  0xe8, 0x45, 0x52, 0x3f, 0x2c, 0xdc, 0x7a, 0xc6, 0x80, 0x54 };
static const uint8_t EPO_GOLF_ROM_SHA1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
{ 0x94, 0x21, 0x83, 0x6a, 0x6b, 0xc4, 0xaf, 0x9e, 0xe1, 0xfc,
  0x7a, 0x40, 0x2d, 0x62, 0xb2, 0xfb, 0x4d, 0xbc, 0xde, 0xfc };

static drgqst_rom_image g_rom;
static drgqst_core *g_core;
static xavix2_machine_t *g_xavix2;
static win_audio g_audio_output;
static xavix_video_recorder g_video_recorder;
static xavix_controller_input g_controller_input;
static xavix_controller_reading g_controller_reading;
static uint32_t g_idle_framebuffer[FRAME_WIDTH * FRAME_HEIGHT];
static const uint32_t *g_framebuffer = g_idle_framebuffer;
static unsigned g_frame_width = FRAME_WIDTH;
static unsigned g_frame_height = FRAME_HEIGHT;
static unsigned g_frame_stride = FRAME_WIDTH;
static unsigned g_frame_pixel_scale = 1;
static HMENU g_menu;
static HMENU g_recording_format_menu;
static HMENU g_xavix2_channel_menu;
static uint64_t g_xavix2_audio_mute_mask;
static enum interface_language g_language = LANGUAGE_ENGLISH;
static enum language_preference g_language_preference =
	LANGUAGE_PREFERENCE_AUTO;
static enum window_status g_window_status = WINDOW_STATUS_IDLE;
static int g_fullscreen;
static int g_stretch_4_3 = 1;
static int g_high_resolution_3d;
static int g_record_window_size = 1;
static xavix_video_format g_recording_format = XAVIX_VIDEO_FORMAT_AVI;
static int g_window_scale = DEFAULT_SCALE;
static WINDOWPLACEMENT g_windowed_placement;
static LONG_PTR g_windowed_style;
static HDC g_capture_dc;
static HBITMAP g_capture_bitmap;
static HGDIOBJ g_capture_old_bitmap;
static int g_capture_width;
static int g_capture_height;
static HDC g_recording_dc;
static HBITMAP g_recording_bitmap;
static HGDIOBJ g_recording_old_bitmap;
static uint32_t *g_recording_pixels;
static int g_recording_width;
static int g_recording_height;
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
static uint8_t g_tak_chq_wheel = 0x80;
static int g_tak_chq_mouse_delta;
static int g_tak_chq_mouse_recentering;
static unsigned g_motion_racing_phase;
static unsigned g_early_motion_phase;
static uint8_t g_rad_mtrk_wheel_direction = 0x80;
static int g_takecopter_mouse_drag_valid;
static int g_takecopter_mouse_drag_x;
static int g_takecopter_mouse_drag_y;
static unsigned g_takecopter_direction_pulse;
static unsigned g_takecopter_direction_frames;
static unsigned g_takecopter_boost_phase;
static uint8_t g_tvpc_mouse_counter_x;
static uint8_t g_tvpc_mouse_counter_y;
static int g_tvpc_mouse_position_valid;
static drgqst_cursor_presentation g_cursor_presentation;
static int g_left_button;
static int g_right_button;
static int g_naruto_joined_hands;
static unsigned g_naruto_execute_delay;
static unsigned g_naruto_execute_frames;
static unsigned g_xavix2_area_gesture_frame;
static unsigned g_xavix2_area_gesture_frames;
static unsigned g_xavix2_gesture_kind;
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
static wchar_t g_save_directory[MAX_PATH];
static wchar_t g_snap_directory[MAX_PATH];
static wchar_t g_ini_path[MAX_PATH];
static wchar_t g_rom_directory[MAX_PATH];
static xavix_game_library g_game_library;
static enum xavix_game_library_sort g_game_library_sort = XAVIX_GAME_SORT_TITLE;
static HWND g_game_library_view;
static HWND g_game_library_choose_button;
static HWND g_game_library_refresh_button;
static HIMAGELIST g_game_library_images;
static int g_game_library_visible = 1;
static int g_exit_confirmation;
static int g_resume_enter_blocked;

enum xavix2_host_gesture
{
	XAVIX2_GESTURE_NONE,
	XAVIX2_GESTURE_DB2J_EDGE,
	XAVIX2_GESTURE_DBZ_SINGLE_CLOSE,
	XAVIX2_GESTURE_DBZ_BOTH_CLOSE,
	XAVIX2_GESTURE_DBZ_DEFLECT,
	XAVIX2_GESTURE_BLDJ_FIRST_ATTACK,
	XAVIX2_GESTURE_BLDJ_SECOND_ATTACK,
	XAVIX2_GESTURE_BLDJ_BOTH_ATTACK
};

static int emulator_loaded(void)
{
	return g_core != NULL || g_xavix2 != NULL;
}

static const interface_strings *interface_text(void)
{
	return &INTERFACE_TEXT[g_language];
}

static const wchar_t *localized_text(const wchar_t *zh_tw,
	const wchar_t *japanese, const wchar_t *french, const wchar_t *english)
{
	switch (g_language)
	{
	case LANGUAGE_ZH_TW: return zh_tw;
	case LANGUAGE_JAPANESE: return japanese;
	case LANGUAGE_FRENCH: return french;
	case LANGUAGE_ENGLISH:
	default: return english;
	}
}

static int rom_uses_camera(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_BAN_ONEP || kind == DRGQST_ROM_BAN_OMT ||
		kind == DRGQST_ROM_TTV_LOTR || kind == DRGQST_ROM_TTV_SW ||
		kind == DRGQST_ROM_TTV_SWJ || kind == DRGQST_ROM_EPO_BOWL ||
		kind == DRGQST_ROM_EPO_HAMC || kind == DRGQST_ROM_EPO_MINI ||
		kind == DRGQST_ROM_EPO_GOLF || kind == DRGQST_ROM_EPO_CROK ||
		kind == DRGQST_ROM_TAK_ZUBA;
}

static int rom_uses_tvpc_host_input(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_TVPC_DOR ||
		kind == DRGQST_ROM_TVPC_HAM ||
		kind == DRGQST_ROM_TVPC_HK;
}

static int rom_uses_digital_direction_input(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_TTV_MX || kind == DRGQST_ROM_TOM_JUMP ||
		kind == DRGQST_ROM_EPO_EBOX || kind == DRGQST_ROM_EPO_ES2J ||
		kind == DRGQST_ROM_RAD_MTRK ||
		kind == DRGQST_ROM_RAD_SNOW || kind == DRGQST_ROM_RAD_SSX ||
		kind == DRGQST_ROM_RAD_SBW || kind == DRGQST_ROM_TAK_GIN ||
		kind == DRGQST_ROM_TCARNAVI || kind == DRGQST_ROM_TOMTHR ||
		kind == DRGQST_ROM_DUELMAST || kind == DRGQST_ROM_EPO_GOLF;
}

static int rom_uses_motion_racing_input(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_TTV_MX || kind == DRGQST_ROM_TOM_JUMP ||
		kind == DRGQST_ROM_RAD_MTRK || kind == DRGQST_ROM_RAD_SNOW ||
		kind == DRGQST_ROM_RAD_SSX || kind == DRGQST_ROM_RAD_SBW ||
		kind == DRGQST_ROM_TAK_GIN || kind == DRGQST_ROM_TCARNAVI ||
		kind == DRGQST_ROM_TOMTHR;
}

static int rom_is_new_early_racer(enum drgqst_rom_kind kind)
{
	return kind >= DRGQST_ROM_RAD_MTRK && kind <= DRGQST_ROM_TOMTHR;
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

static uint16_t xavix2_motion_packet_address(enum drgqst_rom_kind kind)
{
	switch (kind)
	{
	case DRGQST_ROM_BAN_DB2J:
		return 0x014d;
	case DRGQST_ROM_BAN_DBZ:
		return 0x0149;
	case DRGQST_ROM_BAN_NARU:
	case DRGQST_ROM_BAN_BLDJ:
	default:
		return XAVIX2_MOTION_PACKET_FIRST;
	}
}

static int xavix2_uses_motion_packet(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_BAN_NARU ||
		kind == DRGQST_ROM_BAN_BLDJ ||
		kind == DRGQST_ROM_BAN_DB2J ||
		kind == DRGQST_ROM_BAN_DBZ;
}

static uint32_t xavix2_fixed_pio_input(enum drgqst_rom_kind kind)
{
	/* Dragon Ball Z samples this receiver-present input once during boot.
	 * Keep it separate from the neighbouring firmware-controlled outputs. */
	return kind == DRGQST_ROM_BAN_DBZ ? UINT32_C(1) << 23 : 0;
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
	case DRGQST_ROM_TOM_DPGM:
	case DRGQST_ROM_EPO_MINI:
		return DRGQST_CORE_TOM_DPGM_SENSOR_24C08;
	case DRGQST_ROM_EPO_HAMD:
		return DRGQST_CORE_XAVIX_BASE;
	case DRGQST_ROM_RAD_MTRK:
	case DRGQST_ROM_RAD_SNOW:
	case DRGQST_ROM_RAD_SSX:
	case DRGQST_ROM_RAD_SBW:
	case DRGQST_ROM_TAK_GIN:
	case DRGQST_ROM_TCARNAVI:
	case DRGQST_ROM_TOMTHR:
		return DRGQST_CORE_XAVIX_PLAIN;
	case DRGQST_ROM_EPO_CROK:
		return DRGQST_CORE_XAVIX_I2C_24C04;
	case DRGQST_ROM_TAK_ZUBA:
		return DRGQST_CORE_XAVIX_I2C_24C02;
	case DRGQST_ROM_DUELMAST:
		return DRGQST_CORE_DUELMAST_24C04;
	case DRGQST_ROM_EPO_GOLF:
		return DRGQST_CORE_XAVIX2000_SENSOR_24C04;
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
	case DRGQST_ROM_BAN_NARU:
		return BAN_NARU_ROM_SHA1;
	case DRGQST_ROM_BAN_BLDJ:
		return BAN_BLDJ_ROM_SHA1;
	case DRGQST_ROM_BAN_DB2J:
		return BAN_DB2J_ROM_SHA1;
	case DRGQST_ROM_BAN_DBZ:
		return BAN_DBZ_ROM_SHA1;
	case DRGQST_ROM_EPO_DAB2J:
		return EPO_DAB2J_ROM_SHA1;
	case DRGQST_ROM_EPO_DTCJ:
		return EPO_DTCJ_ROM_SHA1;
	case DRGQST_ROM_EPO_PABJ:
		return EPO_PABJ_ROM_SHA1;
	case DRGQST_ROM_EPO_SSK2:
		return EPO_SSK2_ROM_SHA1;
	case DRGQST_ROM_EPO_SSKJ:
		return EPO_SSKJ_ROM_SHA1;
	case DRGQST_ROM_EPO_HAMD:
		return EPO_HAMD_ROM_SHA1;
	case DRGQST_ROM_TVPC_DOR:
		return TVPC_DOR_ROM_SHA1;
	case DRGQST_ROM_TVPC_HAM:
		return TVPC_HAM_ROM_SHA1;
	case DRGQST_ROM_TVPC_HK:
		return TVPC_HK_ROM_SHA1;
	case DRGQST_ROM_TOM_DPGM:
		return TOM_DPGM_ROM_SHA1;
	case DRGQST_ROM_EPO_MINI:
		return EPO_MINI_ROM_SHA1;
	case DRGQST_ROM_RAD_MTRK:
		return RAD_MTRK_ROM_SHA1;
	case DRGQST_ROM_RAD_SNOW:
		return RAD_SNOW_ROM_SHA1;
	case DRGQST_ROM_RAD_SSX:
		return RAD_SSX_ROM_SHA1;
	case DRGQST_ROM_RAD_SBW:
		return RAD_SBW_ROM_SHA1;
	case DRGQST_ROM_TAK_GIN:
		return TAK_GIN_ROM_SHA1;
	case DRGQST_ROM_TCARNAVI:
		return TCARNAVI_ROM_SHA1;
	case DRGQST_ROM_TOMTHR:
		return TOMTHR_ROM_SHA1;
	case DRGQST_ROM_EPO_CROK:
		return EPO_CROK_ROM_SHA1;
	case DRGQST_ROM_TAK_ZUBA:
		return TAK_ZUBA_ROM_SHA1;
	case DRGQST_ROM_DUELMAST:
		return DUELMAST_ROM_SHA1;
	case DRGQST_ROM_EPO_GOLF:
		return EPO_GOLF_ROM_SHA1;
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
	case DRGQST_ROM_BAN_NARU:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_BAN_NARU_RUNTIME_STATE : kind;
	case DRGQST_ROM_BAN_BLDJ:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_BAN_BLDJ_RUNTIME_STATE : kind;
	case DRGQST_ROM_BAN_DB2J:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_BAN_DB2J_RUNTIME_STATE : kind;
	case DRGQST_ROM_BAN_DBZ:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_BAN_DBZ_RUNTIME_STATE : kind;
	case DRGQST_ROM_EPO_DAB2J:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_EPO_DAB2J_RUNTIME_STATE : kind;
	case DRGQST_ROM_EPO_DTCJ:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_EPO_DTCJ_EEPROM :
			DRGQST_PERSISTENCE_EPO_DTCJ_RUNTIME_STATE;
	case DRGQST_ROM_EPO_PABJ:
		return kind == DRGQST_PERSISTENCE_RUNTIME_STATE ?
			DRGQST_PERSISTENCE_EPO_PABJ_RUNTIME_STATE : kind;
	case DRGQST_ROM_EPO_SSK2:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_EPO_SSK2_EEPROM :
			DRGQST_PERSISTENCE_EPO_SSK2_RUNTIME_STATE;
	case DRGQST_ROM_EPO_SSKJ:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_EPO_SSKJ_EEPROM :
			DRGQST_PERSISTENCE_EPO_SSKJ_RUNTIME_STATE;
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
	case DRGQST_ROM_TOM_DPGM:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TOM_DPGM_EEPROM :
			DRGQST_PERSISTENCE_TOM_DPGM_RUNTIME_STATE;
	case DRGQST_ROM_EPO_MINI:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_EPO_MINI_EEPROM :
			DRGQST_PERSISTENCE_EPO_MINI_RUNTIME_STATE;
	case DRGQST_ROM_RAD_MTRK:
		return DRGQST_PERSISTENCE_RAD_MTRK_RUNTIME_STATE;
	case DRGQST_ROM_RAD_SNOW:
		return DRGQST_PERSISTENCE_RAD_SNOW_RUNTIME_STATE;
	case DRGQST_ROM_RAD_SSX:
		return DRGQST_PERSISTENCE_RAD_SSX_RUNTIME_STATE;
	case DRGQST_ROM_RAD_SBW:
		return DRGQST_PERSISTENCE_RAD_SBW_RUNTIME_STATE;
	case DRGQST_ROM_TAK_GIN:
		return DRGQST_PERSISTENCE_TAK_GIN_RUNTIME_STATE;
	case DRGQST_ROM_TCARNAVI:
		return DRGQST_PERSISTENCE_TCARNAVI_RUNTIME_STATE;
	case DRGQST_ROM_TOMTHR:
		return DRGQST_PERSISTENCE_TOMTHR_RUNTIME_STATE;
	case DRGQST_ROM_EPO_CROK:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_EPO_CROK_EEPROM :
			DRGQST_PERSISTENCE_EPO_CROK_RUNTIME_STATE;
	case DRGQST_ROM_TAK_ZUBA:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_TAK_ZUBA_EEPROM :
			DRGQST_PERSISTENCE_TAK_ZUBA_RUNTIME_STATE;
	case DRGQST_ROM_DUELMAST:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_DUELMAST_EEPROM :
			DRGQST_PERSISTENCE_DUELMAST_RUNTIME_STATE;
	case DRGQST_ROM_EPO_GOLF:
		return kind == DRGQST_PERSISTENCE_EEPROM ?
			DRGQST_PERSISTENCE_EPO_GOLF_EEPROM :
			DRGQST_PERSISTENCE_EPO_GOLF_RUNTIME_STATE;
	case DRGQST_ROM_DRAGON_QUEST:
	case DRGQST_ROM_UNKNOWN:
	default:
		return kind;
	}
}

static size_t eeprom_size_for_rom(enum drgqst_rom_kind kind)
{
	if (kind == DRGQST_ROM_EPO_CROK || kind == DRGQST_ROM_DUELMAST ||
		kind == DRGQST_ROM_EPO_GOLF)
		return DRGQST_PERSISTENCE_EEPROM24C04_SIZE;
	if (kind == DRGQST_ROM_TAK_ZUBA)
		return DRGQST_PERSISTENCE_EEPROM24C02_SIZE;
	if (kind == DRGQST_ROM_EPO_DTCJ || kind == DRGQST_ROM_EPO_SSK2 ||
		kind == DRGQST_ROM_EPO_SSKJ)
		return DRGQST_PERSISTENCE_EEPROM24C04_SIZE;
	if (kind == DRGQST_ROM_EPO_HAMD || kind == DRGQST_ROM_EPO_ES2J ||
		kind == DRGQST_ROM_EPO_HAMC || rom_is_new_early_racer(kind) ||
		rom_uses_parallel_nvram(kind) ||
		drgqst_rom_is_xavix2(kind))
		return 0;
	if (drgqst_rom_is_tvpc(kind))
		return DRGQST_PERSISTENCE_EEPROM24C16_SIZE;
	return DRGQST_PERSISTENCE_EEPROM_SIZE;
}

static enum interface_language detect_system_language(void)
{
	wchar_t locale_name[LOCALE_NAME_MAX_LENGTH];

	if (!GetUserDefaultLocaleName(locale_name,
		sizeof(locale_name) / sizeof(locale_name[0])))
		return LANGUAGE_ENGLISH;
	if (_wcsnicmp(locale_name, L"ja", 2) == 0)
		return LANGUAGE_JAPANESE;
	if (_wcsnicmp(locale_name, L"fr", 2) == 0)
		return LANGUAGE_FRENCH;
	if (_wcsnicmp(locale_name, L"zh-TW", 5) == 0 ||
		_wcsnicmp(locale_name, L"zh-HK", 5) == 0 ||
		_wcsnicmp(locale_name, L"zh-MO", 5) == 0 ||
		wcsstr(locale_name, L"Hant") != NULL)
		return LANGUAGE_ZH_TW;
	return LANGUAGE_ENGLISH;
}

static enum interface_language resolved_language(
	enum language_preference preference)
{
	switch (preference)
	{
	case LANGUAGE_PREFERENCE_ZH_TW:
		return LANGUAGE_ZH_TW;
	case LANGUAGE_PREFERENCE_JAPANESE:
		return LANGUAGE_JAPANESE;
	case LANGUAGE_PREFERENCE_FRENCH:
		return LANGUAGE_FRENCH;
	case LANGUAGE_PREFERENCE_ENGLISH:
		return LANGUAGE_ENGLISH;
	case LANGUAGE_PREFERENCE_AUTO:
	default:
		return detect_system_language();
	}
}

static void write_ini_string(const wchar_t *section, const wchar_t *key,
	const wchar_t *value)
{
	if (g_ini_path[0])
		(void)WritePrivateProfileStringW(section, key, value, g_ini_path);
}

static void write_ini_integer(const wchar_t *section, const wchar_t *key,
	int value)
{
	wchar_t text[24];
	_snwprintf(text, sizeof(text) / sizeof(text[0]), L"%d", value);
	text[sizeof(text) / sizeof(text[0]) - 1] = L'\0';
	write_ini_string(section, key, text);
}

static void apply_system_ui_language(enum interface_language language)
{
	LANGID language_id;

	if (language == LANGUAGE_JAPANESE)
		language_id = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
	else if (language == LANGUAGE_FRENCH)
		language_id = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);
	else if (language == LANGUAGE_ZH_TW)
		language_id = MAKELANGID(LANG_CHINESE,
			SUBLANG_CHINESE_TRADITIONAL);
	else
		language_id = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	SetThreadUILanguage(language_id);
}

static void update_window_title(HWND window)
{
	const interface_strings *text = interface_text();
	const wchar_t *title = text->window_title_idle;
	if (xavix_video_recorder_active(&g_video_recorder))
	{
		SetWindowTextW(window, L"XaviXEmu - REC (F10 to stop)");
		return;
	}

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

static void update_xavix2_audio_channel_menu(void)
{
	unsigned channel;
	uint32_t engine_rate = g_xavix2 ?
		xavix2_audio_engine_rate(g_xavix2->mmio[0xa00],
			g_xavix2->mmio[0xa05]) : 0;

	if (!g_xavix2_channel_menu)
		return;
	EnableMenuItem(g_xavix2_channel_menu, ID_XAVIX2_AUDIO_ENABLE_ALL,
		MF_BYCOMMAND | (g_xavix2 ? MF_ENABLED : MF_GRAYED));
	EnableMenuItem(g_xavix2_channel_menu, ID_XAVIX2_AUDIO_MUTE_ALL,
		MF_BYCOMMAND | (g_xavix2 ? MF_ENABLED : MF_GRAYED));
	for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
	{
		const UINT id = ID_XAVIX2_AUDIO_CHANNEL_FIRST + channel;
		wchar_t label[64];
		UINT flags = MF_BYCOMMAND | MF_STRING;
		if (channel && channel % 16 == 0)
			flags |= MF_MENUBARBREAK;
		if (g_xavix2 && g_xavix2->audio.voice[channel].active)
		{
			const xavix2_audio_voice *voice =
				&g_xavix2->audio.voice[channel];
			uint32_t rate = (uint32_t)(((uint64_t)voice->pitch *
				engine_rate + 32768U) >> 16);
			_snwprintf(label, sizeof(label) / sizeof(label[0]),
				L"%02u  %5lu Hz  L%u R%u%s", channel,
				(unsigned long)rate, voice->volume_left, voice->volume_right,
				voice->release_phase ? L"  release" :
				voice->loop ? L"  loop" : L"");
		}
		else
			_snwprintf(label, sizeof(label) / sizeof(label[0]),
				L"%02u  idle", channel);
		label[sizeof(label) / sizeof(label[0]) - 1] = L'\0';
		ModifyMenuW(g_xavix2_channel_menu, id, flags, id, label);
		EnableMenuItem(g_xavix2_channel_menu, id,
			MF_BYCOMMAND | (g_xavix2 ? MF_ENABLED : MF_GRAYED));
		CheckMenuItem(g_xavix2_channel_menu, id, MF_BYCOMMAND |
			(!(g_xavix2_audio_mute_mask & (UINT64_C(1) << channel)) ?
				MF_CHECKED : MF_UNCHECKED));
	}
}

static HMENU create_application_menu(void)
{
	const interface_strings *text = interface_text();
	HMENU menu = CreateMenu();
	HMENU file_menu = CreatePopupMenu();
	HMENU state_menu = CreatePopupMenu();
	HMENU view_menu = CreatePopupMenu();
	HMENU recording_format_menu = CreatePopupMenu();
	HMENU language_menu = CreatePopupMenu();
	HMENU controller_menu = CreatePopupMenu();
	HMENU audio_menu = CreatePopupMenu();
	HMENU help_menu = CreatePopupMenu();
	UINT state_flags = MF_STRING |
		(emulator_loaded() ? MF_ENABLED : MF_GRAYED);
	UINT screenshot_flags = MF_STRING |
		(emulator_loaded() ? MF_ENABLED : MF_GRAYED);

	AppendMenuW(file_menu, MF_STRING, ID_FILE_GAME_LIBRARY,
		localized_text(L"遊戲清單(&L)", L"ゲーム一覧(&L)",
			L"&Bibliothèque de jeux", L"Game &library"));
	AppendMenuW(file_menu, MF_STRING, ID_FILE_SET_ROM_DIRECTORY,
		localized_text(L"設定 ROM 目錄(&D)...", L"ROMフォルダーを設定(&D)...",
			L"Choisir le dossier ROM(&D)...", L"Set ROM &directory..."));
	AppendMenuW(file_menu, MF_STRING, ID_FILE_REFRESH_LIBRARY,
		localized_text(L"重新掃描遊戲(&R)", L"ゲームを再スキャン(&R)",
			L"Réanalyser les jeux(&R)", L"&Refresh game list"));
	AppendMenuW(file_menu, MF_SEPARATOR, 0, NULL);
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
	AppendMenuW(view_menu, MF_STRING |
		(g_high_resolution_3d ? MF_CHECKED : MF_UNCHECKED) |
		(g_xavix2 ? MF_ENABLED : MF_GRAYED),
		ID_VIEW_HIGH_RESOLUTION_3D, text->menu_high_resolution_3d);
	AppendMenuW(view_menu,
		MF_STRING | (g_record_window_size ? MF_CHECKED : MF_UNCHECKED),
		ID_VIEW_RECORD_WINDOW_SIZE, text->menu_record_window_size);
	AppendMenuW(recording_format_menu, MF_STRING, ID_VIEW_RECORD_FORMAT_AVI,
		text->menu_record_avi);
	AppendMenuW(recording_format_menu, MF_STRING, ID_VIEW_RECORD_FORMAT_MP4,
		text->menu_record_mp4);
	CheckMenuRadioItem(recording_format_menu, ID_VIEW_RECORD_FORMAT_AVI,
		ID_VIEW_RECORD_FORMAT_MP4,
		g_recording_format == XAVIX_VIDEO_FORMAT_MP4 ?
			ID_VIEW_RECORD_FORMAT_MP4 : ID_VIEW_RECORD_FORMAT_AVI,
		MF_BYCOMMAND);
	AppendMenuW(view_menu, MF_POPUP, (UINT_PTR)recording_format_menu,
		text->menu_record_format);
	AppendMenuW(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(view_menu, MF_STRING, ID_VIEW_FULLSCREEN,
		text->menu_fullscreen);

	AppendMenuW(language_menu, MF_STRING, ID_LANGUAGE_AUTO,
		localized_text(L"自動", L"自動", L"Automatique", L"Auto"));
	AppendMenuW(language_menu, MF_STRING, ID_LANGUAGE_ZH_TW,
		text->menu_zh_tw);
	AppendMenuW(language_menu, MF_STRING, ID_LANGUAGE_JAPANESE,
		L"日本語 (&J)");
	AppendMenuW(language_menu, MF_STRING, ID_LANGUAGE_FRENCH,
		L"Français (&R)");
	AppendMenuW(language_menu, MF_STRING, ID_LANGUAGE_ENGLISH,
		text->menu_english);
	CheckMenuRadioItem(language_menu, ID_LANGUAGE_AUTO,
		ID_LANGUAGE_ENGLISH,
		ID_LANGUAGE_AUTO + (UINT)g_language_preference, MF_BYCOMMAND);

	AppendMenuW(help_menu, MF_STRING, ID_HELP_ABOUT, text->menu_about);
	AppendMenuW(controller_menu, MF_STRING, ID_CONTROLLER_SETTINGS,
		localized_text(L"控制器設定...", L"コントローラー設定...",
			L"Paramètres de la manette...", L"Controller settings..."));
	g_recording_format_menu = recording_format_menu;
	g_xavix2_channel_menu = audio_menu;
	AppendMenuW(audio_menu, MF_STRING, ID_XAVIX2_AUDIO_ENABLE_ALL,
		L"全部開啟 / Enable all");
	AppendMenuW(audio_menu, MF_STRING, ID_XAVIX2_AUDIO_MUTE_ALL,
		L"全部靜音 / Mute all");
	AppendMenuW(audio_menu, MF_SEPARATOR, 0, NULL);
	{
		unsigned channel;
		for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
		{
			wchar_t label[16];
			UINT flags = MF_STRING;
			if (channel && channel % 16 == 0)
				flags |= MF_MENUBARBREAK;
			_snwprintf(label, sizeof(label) / sizeof(label[0]),
				L"%02u", channel);
			AppendMenuW(audio_menu, flags,
				ID_XAVIX2_AUDIO_CHANNEL_FIRST + channel, label);
		}
	}
	update_xavix2_audio_channel_menu();

	AppendMenuW(menu, MF_POPUP, (UINT_PTR)file_menu, text->menu_file);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)state_menu, text->menu_state);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)audio_menu,
		L"XaviX2 聲道 / Channels");
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)view_menu, text->menu_view);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)language_menu,
		text->menu_language);
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)controller_menu,
		localized_text(L"控制器(&C)", L"コントローラー(&C)",
			L"&Manette", L"&Controller"));
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

static void set_language_preference(HWND window,
	enum language_preference preference)
{
	enum interface_language language = resolved_language(preference);
	static const wchar_t *const setting_values[] =
	{
		L"auto", L"zh-TW", L"ja", L"fr", L"en"
	};

	if (preference < LANGUAGE_PREFERENCE_AUTO ||
		preference > LANGUAGE_PREFERENCE_ENGLISH)
		return;
	g_language_preference = preference;
	write_ini_string(L"Interface", L"Language", setting_values[preference]);
	if (g_language == language)
	{
		rebuild_application_menu(window);
		update_game_library_labels();
		return;
	}
	g_language = language;
	apply_system_ui_language(language);
	rebuild_application_menu(window);
	update_game_library_labels();
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

typedef struct controller_dialog_state
{
	unsigned bindings[XAVIX_CONTROLLER_ACTION_COUNT];
	enum xavix_controller_action waiting_action;
	enum drgqst_rom_kind kind;
	const wchar_t *title;
	int waiting_for_release;
} controller_dialog_state;

static const wchar_t *controller_action_name(unsigned action,
	enum drgqst_rom_kind kind)
{
	const xavix_game_control_profile *profile =
		xavix_game_control_profile_for_kind(kind);
	static const wchar_t *const zh[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"主要動作", L"次要動作", L"防禦", L"必殺技", L"確認",
		L"雙手動作", L"彈開／反擊"
	};
	static const wchar_t *const ja[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"メイン動作", L"サブ動作", L"防御", L"必殺技", L"決定",
		L"両手動作", L"はじき／反撃"
	};
	static const wchar_t *const en[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"Primary action", L"Secondary action", L"Defense", L"Special",
		L"Confirm", L"Two-hand action", L"Deflect / counter"
	};
	static const wchar_t *const fr[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"Action principale", L"Action secondaire", L"Défense",
		L"Technique spéciale", L"Confirmer", L"Action à deux mains",
		L"Dévier / contrer"
	};
	static const wchar_t *const bldj_zh[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"攻擊（反光點 1）", L"攻擊（反光點 2）", L"防禦",
		L"雙手攻擊", L"確認", L"雙手攻擊", L"彈開／反擊"
	};
	static const wchar_t *const bldj_ja[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"攻撃（反射点 1）", L"攻撃（反射点 2）", L"防御",
		L"両手攻撃", L"決定", L"両手攻撃", L"はじき／反撃"
	};
	static const wchar_t *const bldj_en[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"Attack (reflector 1)", L"Attack (reflector 2)", L"Defense",
		L"Two-hand attack", L"Confirm", L"Two-hand attack",
		L"Deflect / counter"
	};
	static const wchar_t *const bldj_fr[XAVIX_CONTROLLER_ACTION_COUNT] =
	{
		L"Attaque (réflecteur 1)", L"Attaque (réflecteur 2)", L"Défense",
		L"Attaque à deux mains", L"Confirmer", L"Attaque à deux mains",
		L"Dévier / contrer"
	};
	if (action >= XAVIX_CONTROLLER_ACTION_COUNT)
		return L"";
	if (profile)
	{
		if (!xavix_game_control_action_visible(profile,
			(enum xavix_controller_action)action))
			return L"";
		return xavix_game_control_label_text(profile->actions[action],
			(unsigned)g_language);
	}
	if (kind == DRGQST_ROM_BAN_BLDJ)
		return g_language == LANGUAGE_ZH_TW ? bldj_zh[action] :
			g_language == LANGUAGE_JAPANESE ? bldj_ja[action] :
			g_language == LANGUAGE_FRENCH ? bldj_fr[action] :
			bldj_en[action];
	return g_language == LANGUAGE_ZH_TW ? zh[action] :
		g_language == LANGUAGE_JAPANESE ? ja[action] :
		g_language == LANGUAGE_FRENCH ? fr[action] : en[action];
}

static const wchar_t *controller_requirements(enum drgqst_rom_kind kind)
{
	switch (kind)
	{
	case DRGQST_ROM_TTV_MX:
	case DRGQST_ROM_TOM_JUMP:
	case DRGQST_ROM_TAK_CHQ:
	case DRGQST_ROM_RAD_MTRK:
	case DRGQST_ROM_RAD_SNOW:
	case DRGQST_ROM_RAD_SSX:
	case DRGQST_ROM_RAD_SBW:
	case DRGQST_ROM_TAK_GIN:
	case DRGQST_ROM_TCARNAVI:
	case DRGQST_ROM_TOMTHR:
		return localized_text(
			L"操作：方向鍵或滑鼠左右轉向；PS 搖桿左類比可精細轉向。上／下為加速與煞車，動作鍵依遊戲使用。",
			L"操作：方向キーまたはマウス左右で旋回。PSコントローラーの左スティックで細かく操作。上／下は加速・ブレーキです。",
			L"Commandes : flèches ou souris pour tourner ; stick gauche PS pour une direction progressive. Haut/bas : accélérer/freiner.",
			L"Controls: arrow keys or mouse steer; the PS left stick provides analog steering. Up/down accelerate and brake.");
	case DRGQST_ROM_EPO_SDB:
		return localized_text(
			L"操作：滑鼠或左類比桿控制球／感應器方向；主要、次要動作對應兩位玩家的按鍵。",
			L"操作：マウスまたは左スティックでボール／センサーを操作。メイン／サブ動作は各プレイヤーのボタンです。",
			L"Commandes : souris ou stick gauche pour le capteur ; actions principale/secondaire pour les deux joueurs.",
			L"Controls: mouse or left stick moves the ball/sensor; primary and secondary actions are the two player buttons.");
	case DRGQST_ROM_EPO_BOWL:
	case DRGQST_ROM_EPO_MINI:
	case DRGQST_ROM_EPO_HAMC:
	case DRGQST_ROM_EPO_HAMD:
		return localized_text(
			L"操作：滑鼠移動模擬光學感應器；方向鍵選擇，滑鼠左／右鍵或自訂動作鍵確認與操作。",
			L"操作：マウス移動で光学センサーを再現。方向キーで選択し、左右クリックまたは割り当てた動作ボタンで操作します。",
			L"Commandes : la souris simule le capteur optique ; flèches pour choisir, clics ou actions assignées pour valider.",
			L"Controls: mouse motion simulates the optical sensor; use arrows to select and mouse buttons or assigned actions to operate.");
	case DRGQST_ROM_TVPC_DOR:
	case DRGQST_ROM_TVPC_HAM:
	case DRGQST_ROM_TVPC_HK:
	case DRGQST_ROM_TOM_DPGM:
		return localized_text(
			L"操作：滑鼠移動游標、左鍵確認；方向鍵與 Enter 也可操作選單，鍵盤會傳送到 TV-PC 鍵盤矩陣。",
			L"操作：マウスでカーソル移動、左クリックで決定。方向キーと Enter でもメニューを操作できます。",
			L"Commandes : souris pour le curseur, clic gauche pour valider ; flèches et Entrée fonctionnent aussi dans les menus.",
			L"Controls: move the cursor with the mouse and left-click to confirm; arrow keys and Enter also operate menus.");
	case DRGQST_ROM_DRAGON_QUEST:
		return localized_text(
			L"操作：滑鼠或 PS 類比桿模擬揮劍；下方可分別設定攻擊、防禦、魔法與必殺技。",
			L"操作：マウスまたはPSスティックで剣を振り、攻撃・防御・魔法・必殺技を個別に割り当てます。",
			L"Commandes : souris ou stick PS pour l'épée ; assignez séparément attaque, défense, magie et technique spéciale.",
			L"Controls: swing the sword with the mouse or PS stick; attack, defense, magic, and special move are mapped separately.");
	case DRGQST_ROM_BAN_ONEP:
		return localized_text(
			L"操作：滑鼠或雙類比控制感應點；可分別設定攻擊、第二感應點與火箭炮。",
			L"操作：マウスまたは2本のスティックでセンサーを操作。攻撃、第2センサー、バズーカを個別設定できます。",
			L"Commandes : souris ou deux sticks pour les capteurs ; assignez attaque, second capteur et bazooka.",
			L"Controls: mouse or dual sticks control the sensors; attack, second sensor, and bazooka are separate actions.");
	case DRGQST_ROM_BAN_OMT:
		return localized_text(
			L"操作：滑鼠或類比桿模擬體感；攻擊、第二攻擊、翻面與旋轉感應器可分別設定。",
			L"操作：マウスまたはスティックで体感操作。攻撃、第2攻撃、反転、回転を個別設定できます。",
			L"Commandes : gestes souris/stick ; attaque, seconde attaque, inversion et rotation sont séparées.",
			L"Controls: mouse/stick motion; attack, second attack, sensor reverse, and sensor spin are mapped separately.");
	case DRGQST_ROM_TTV_LOTR:
		return localized_text(
			L"操作：滑鼠或類比桿揮劍；劍擊、第二攻擊、防禦與必殺技皆可單獨設定。",
			L"操作：マウスまたはスティックで剣を振り、剣攻撃、第2攻撃、防御、必殺技を個別設定します。",
			L"Commandes : maniez l'épée à la souris/stick ; attaque, seconde attaque, défense et spécial sont séparés.",
			L"Controls: swing with the mouse or stick; sword attack, second attack, defense, and special move are separate.");
	case DRGQST_ROM_TTV_SW:
	case DRGQST_ROM_TTV_SWJ:
		return localized_text(
			L"操作：滑鼠或類比桿揮動光劍；劍擊、第二感應動作、防禦與光劍旋轉可分別設定。",
			L"操作：マウスまたはスティックでライトセーバーを振り、攻撃、第2動作、防御、回転を個別設定します。",
			L"Commandes : sabre laser à la souris/stick ; attaque, seconde action, défense et rotation sont séparées.",
			L"Controls: swing the lightsaber with the mouse or stick; attack, second action, defense, and saber spin are separate.");
	case DRGQST_ROM_EPO_DTCJ:
		return localized_text(
			L"操作：按住滑鼠右鍵拖曳，或使用方向鍵／PS 左類比模擬頭部傾斜；此遊戲沒有額外的獨立按鍵。",
			L"操作：右クリックドラッグ、方向キー、またはPS左スティックで頭の傾きを再現します。追加の独立ボタンはありません。",
			L"Commandes : glisser avec le bouton droit, flèches ou stick gauche PS pour incliner la tête ; aucun bouton séparé supplémentaire.",
			L"Controls: right-drag the mouse, use arrows, or the PS left stick to emulate head tilt; there are no separate extra buttons.");
	case DRGQST_ROM_BAN_NARU:
	case DRGQST_ROM_BAN_BLDJ:
	case DRGQST_ROM_BAN_DB2J:
	case DRGQST_ROM_BAN_DBZ:
		return localized_text(
			L"操作：滑鼠、雙類比桿或雙 Wii Remote 控制一至兩個反光點；下方可設定攻擊、合掌／雙手與必殺技。",
			L"操作：マウス、2本のアナログスティック、または2台のWii Remoteで反射点を操作。攻撃・両手・必殺技は下で設定できます。",
			L"Commandes : souris, deux sticks ou deux Wii Remote pour les réflecteurs ; configurez attaque, deux mains et spécial ci-dessous.",
			L"Controls: mouse, dual analog sticks, or two Wii Remotes control the reflectors; map attack, two-hand, and special actions below.");
	case DRGQST_ROM_DUELMAST:
		return localized_text(
			L"操作：方向鍵或左類比桿移動；主要／確認、次要與特殊動作對應三個本體按鍵。遊戲卡與掃卡器仍需另行驗證。",
			L"操作：方向キーまたは左スティックで移動。メイン／決定、サブ、特殊が本体の3ボタンです。カートリッジとカードリーダーは検証中です。",
			L"Commandes : flèches ou stick gauche ; action principale/validation, secondaire et spéciale pour les trois boutons. Cartouche et lecteur à vérifier.",
			L"Controls: arrows or left stick move; primary/confirm, secondary, and special map the three console buttons. Cartridge and card reader still need verification.");
	case DRGQST_ROM_EPO_CROK:
		return localized_text(
			L"操作：滑鼠或類比桿模擬拳套／槌子的揮動；主要、次要動作分別揮拳與槌擊，防禦對應紅色按鈕。",
			L"操作：マウスまたはスティックでリストバンド／ハンマーを振ります。メインとサブがパンチ／ハンマー、防御が赤ボタンです。",
			L"Commandes : gestes souris/stick pour le bracelet et le marteau ; actions principale/secondaire pour frapper, défense pour le bouton rouge.",
			L"Controls: mouse/stick gestures emulate the wrist and hammer; primary/secondary swing them, and defense maps the red guard button.");
	case DRGQST_ROM_TAK_ZUBA:
		return localized_text(
			L"操作：滑鼠或雙類比桿模擬兩把刀的揮動；主要、次要動作分別對應藍刀與紅刀，防禦／特殊切換刀的扳機。",
			L"操作：マウスまたは2本のスティックで2本の刀を振ります。メイン／サブは青刀／赤刀、防御／特殊はトリガーです。",
			L"Commandes : gestes souris/deux sticks pour les deux sabres ; principale/secondaire pour bleu/rouge, défense/spéciale pour les gâchettes.",
			L"Controls: mouse/dual-stick gestures emulate both swords; primary/secondary map blue/red sword swings, defense/special map their triggers.");
	case DRGQST_ROM_EPO_GOLF:
		return localized_text(
			L"操作：滑鼠揮動或 PS 類比桿模擬球桿；左右偏移會影響揮桿方向，主要動作確認選單。",
			L"操作：マウスの振りまたはPSスティックでクラブを再現。左右の軌道で打球方向が変わり、メイン動作で決定します。",
			L"Commandes : geste souris ou stick PS pour le club ; l'écart gauche/droite influe sur le tir, action principale pour valider.",
			L"Controls: swing with the mouse or PS stick; left/right path affects the shot, and primary confirms menus.");
	case DRGQST_ROM_EPO_ES2J:
		return localized_text(
			L"操作：方向鍵與 Enter／動作鍵可操作選單；實體卡片掃描器仍在研究中，因此目前為初步支援。",
			L"操作：方向キーと Enter／動作ボタンでメニュー操作。カードスキャナーは調査中のため暫定対応です。",
			L"Commandes : flèches et Entrée/actions pour les menus ; le lecteur de cartes reste en cours d'étude.",
			L"Controls: arrows and Enter/actions operate menus; the physical card scanner is still under study.");
	default:
		return localized_text(
			L"操作：方向鍵、滑鼠與下方可自訂動作鍵。此遊戲目前為初步支援，操作會持續依實測補齊。",
			L"操作：方向キー、マウス、下の割り当て可能な動作ボタン。現在は暫定対応です。",
			L"Commandes : flèches, souris et actions configurables ci-dessous. Prise en charge initiale.",
			L"Controls: arrow keys, mouse, and configurable actions below. Support is currently preliminary.");
	}
}

static void update_controller_binding_buttons(HWND dialog,
	const controller_dialog_state *state)
{
	const xavix_game_control_profile *profile =
		xavix_game_control_profile_for_kind(state->kind);
	unsigned action;
	for (action = 0; action < XAVIX_CONTROLLER_ACTION_COUNT; ++action)
	{
		HWND button = GetDlgItem(dialog, IDC_CONTROLLER_ACTION_0 + action);
		wchar_t label[96];
		int visible = !profile || xavix_game_control_action_visible(profile,
			(enum xavix_controller_action)action);
		ShowWindow(button, visible ? SW_SHOW : SW_HIDE);
		EnableWindow(button, visible);
		if (!visible)
			continue;
		if (state->waiting_action == action)
			_snwprintf(label, sizeof(label) / sizeof(label[0]), L"%ls: %ls",
				controller_action_name(action, state->kind),
				localized_text(L"請按搖桿按鍵", L"ボタンを押してください",
					L"appuyez sur un bouton", L"press a controller button"));
		else if (state->bindings[action])
			_snwprintf(label, sizeof(label) / sizeof(label[0]), L"%ls: B%u",
				controller_action_name(action, state->kind), state->bindings[action]);
		else
			_snwprintf(label, sizeof(label) / sizeof(label[0]), L"%ls: -",
				controller_action_name(action, state->kind));
		label[sizeof(label) / sizeof(label[0]) - 1] = L'\0';
		SetDlgItemTextW(dialog, IDC_CONTROLLER_ACTION_0 + action, label);
	}
}

static void update_controller_status(HWND dialog)
{
	wchar_t status[224];
	int gamepad = xavix_controller_input_gamepad_connected(&g_controller_input);
	int wii1 = xavix_controller_input_wii_connected(&g_controller_input, 0);
	int wii2 = xavix_controller_input_wii_connected(&g_controller_input, 1);
	_snwprintf(status, sizeof(status) / sizeof(status[0]),
		localized_text(L"PS／搖桿：%ls　Wii 1：%ls　Wii 2：%ls",
			L"ゲームパッド: %ls    Wii 1: %ls    Wii 2: %ls",
			L"Manette : %ls    Wii 1 : %ls    Wii 2 : %ls",
			L"Gamepad: %ls    Wii 1: %ls    Wii 2: %ls"),
		gamepad ? localized_text(L"OK", L"OK", L"connectée", L"connected") :
			localized_text(L"未連接", L"未接続", L"non connectée", L"not connected"),
		wii1 ? localized_text(L"OK", L"OK", L"connectée", L"connected") :
			localized_text(L"未連接", L"未接続", L"non connectée", L"not connected"),
		wii2 ? localized_text(L"OK", L"OK", L"connectée", L"connected") :
			localized_text(L"未連接", L"未接続", L"non connectée", L"not connected"));
	status[sizeof(status) / sizeof(status[0]) - 1] = L'\0';
	SetDlgItemTextW(dialog, IDC_CONTROLLER_STATUS, status);
}

static INT_PTR CALLBACK controller_dialog_procedure(HWND dialog,
	UINT message, WPARAM wparam, LPARAM lparam)
{
	controller_dialog_state *state = (controller_dialog_state *)
		GetWindowLongPtrW(dialog, DWLP_USER);
	if (message == WM_INITDIALOG)
	{
		unsigned action;
		wchar_t profile[128];
		state = (controller_dialog_state *)lparam;
		SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)state);
		state->waiting_action = XAVIX_CONTROLLER_ACTION_COUNT;
		for (action = 0; action < XAVIX_CONTROLLER_ACTION_COUNT; ++action)
			state->bindings[action] = xavix_controller_input_binding(
				&g_controller_input, (enum xavix_controller_action)action);
		SetWindowTextW(dialog, localized_text(L"控制器設定",
			L"コントローラー設定", L"Paramètres de la manette",
			L"Controller settings"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_SOURCE_LABEL,
			localized_text(L"輸入來源", L"入力方式", L"Source d'entrée",
				L"Input source"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_SINGLE_LABEL,
			localized_text(L"單一反光點", L"単一反射板", L"Réflecteur unique",
				L"Single reflector"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_DEADZONE_LABEL,
			localized_text(L"死區 (%)", L"デッドゾーン (%)", L"Zone morte (%)",
				L"Dead zone (%)"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_ACTIONS_GROUP,
			localized_text(L"本遊戲實際動作", L"このゲームの実際の操作",
				L"Actions du jeu", L"Game actions"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_WII_GROUP,
			localized_text(L"Wii Remote 紅外線校正", L"Wii Remote IR補正",
				L"Étalonnage IR des Wii Remote", L"Wii Remote IR calibration"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_RESCAN,
			localized_text(L"重新掃描", L"再検索", L"Rechercher", L"Rescan"));
		SetDlgItemTextW(dialog, IDOK, L"OK");
		SetDlgItemTextW(dialog, IDCANCEL,
			localized_text(L"取消", L"キャンセル", L"Annuler", L"Cancel"));
		_snwprintf(profile, sizeof(profile) / sizeof(profile[0]),
			localized_text(L"目前遊戲設定：%ls", L"ゲーム設定: %ls",
				L"Profil du jeu : %ls", L"Current game profile: %ls"),
			state->title ? state->title : g_controller_input.profile);
		SetDlgItemTextW(dialog, IDC_CONTROLLER_PROFILE, profile);
		SetDlgItemTextW(dialog, IDC_CONTROLLER_REQUIREMENTS,
			controller_requirements(state->kind));
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SOURCE, CB_ADDSTRING, 0,
			(LPARAM)localized_text(L"自動", L"自動", L"Automatique", L"Auto"));
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SOURCE, CB_ADDSTRING, 0,
			(LPARAM)localized_text(L"滑鼠", L"マウス", L"Souris", L"Mouse"));
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SOURCE, CB_ADDSTRING, 0,
			(LPARAM)localized_text(L"PS／搖桿類比桿",
				L"PS／ゲームパッドのアナログスティック",
				L"Sticks analogiques PS / manette",
				L"PS / gamepad analog sticks"));
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SOURCE, CB_ADDSTRING, 0,
			(LPARAM)localized_text(L"雙 Wii Remote（紅外線）",
				L"2台のWii Remote (IR)", L"Deux Wii Remote (IR)",
				L"Two Wii Remotes (IR)"));
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SOURCE, CB_SETCURSEL,
			xavix_controller_input_source(&g_controller_input), 0);
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SINGLE, CB_ADDSTRING, 0,
			(LPARAM)localized_text(L"左類比桿", L"左スティック",
				L"Stick gauche", L"Left stick"));
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SINGLE, CB_ADDSTRING, 0,
			(LPARAM)localized_text(L"右類比桿", L"右スティック",
				L"Stick droit", L"Right stick"));
		SendDlgItemMessageW(dialog, IDC_CONTROLLER_SINGLE, CB_SETCURSEL,
			xavix_controller_input_single_reflector(&g_controller_input), 0);
		SetDlgItemInt(dialog, IDC_CONTROLLER_DEADZONE,
			xavix_controller_input_dead_zone(&g_controller_input), FALSE);
		SetDlgItemTextW(dialog, IDC_CONTROLLER_WII1_TL,
			localized_text(L"Wii 1 左上", L"Wii 1 左上", L"Wii 1 haut-gauche",
				L"Wii 1 upper-left"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_WII1_BR,
			localized_text(L"Wii 1 右下", L"Wii 1 右下", L"Wii 1 bas-droite",
				L"Wii 1 lower-right"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_WII2_TL,
			localized_text(L"Wii 2 左上", L"Wii 2 左上", L"Wii 2 haut-gauche",
				L"Wii 2 upper-left"));
		SetDlgItemTextW(dialog, IDC_CONTROLLER_WII2_BR,
			localized_text(L"Wii 2 右下", L"Wii 2 右下", L"Wii 2 bas-droite",
				L"Wii 2 lower-right"));
		update_controller_binding_buttons(dialog, state);
		update_controller_status(dialog);
		SetTimer(dialog, 1, 30, NULL);
		return TRUE;
	}
	if (!state)
		return FALSE;
	if (message == WM_TIMER && wparam == 1 &&
		state->waiting_action < XAVIX_CONTROLLER_ACTION_COUNT)
	{
		unsigned button = xavix_controller_input_first_pressed_button(
			&g_controller_input);
		if (state->waiting_for_release)
		{
			if (!button)
				state->waiting_for_release = 0;
		}
		else if (button)
		{
			state->bindings[state->waiting_action] = button;
			state->waiting_action = XAVIX_CONTROLLER_ACTION_COUNT;
			update_controller_binding_buttons(dialog, state);
		}
		return TRUE;
	}
	if (message == WM_COMMAND)
	{
		unsigned id = LOWORD(wparam);
		if (id >= IDC_CONTROLLER_ACTION_0 &&
			id < IDC_CONTROLLER_ACTION_0 + XAVIX_CONTROLLER_ACTION_COUNT)
		{
			state->waiting_action = (enum xavix_controller_action)
				(id - IDC_CONTROLLER_ACTION_0);
			state->waiting_for_release = 1;
			update_controller_binding_buttons(dialog, state);
			return TRUE;
		}
		if (id == IDC_CONTROLLER_RESCAN)
		{
			xavix_controller_input_rescan(&g_controller_input);
			update_controller_status(dialog);
			return TRUE;
		}
		if (id >= IDC_CONTROLLER_WII1_TL &&
			id <= IDC_CONTROLLER_WII2_BR)
		{
			unsigned offset = id - IDC_CONTROLLER_WII1_TL;
			unsigned index = offset / 2;
			int upper_left = (offset & 1) == 0;
			if (!xavix_controller_input_capture_wii_calibration(
				&g_controller_input, index, upper_left))
				MessageBoxW(dialog,
					localized_text(
						L"請先連接 Wii Remote，並讓它對準螢幕感應條。",
						L"Wii Remoteを接続し、センサーバーを画面に向けてください。",
						L"Connectez la Wii Remote et pointez-la vers la barre de capteurs.",
						L"Connect the Wii Remote and point it at the screen sensor bar."),
					L"XaviXEmu", MB_OK | MB_ICONWARNING);
			return TRUE;
		}
		if (id == IDOK)
		{
			unsigned action;
			BOOL translated;
			UINT dead_zone = GetDlgItemInt(dialog, IDC_CONTROLLER_DEADZONE,
				&translated, FALSE);
			LRESULT source = SendDlgItemMessageW(dialog,
				IDC_CONTROLLER_SOURCE, CB_GETCURSEL, 0, 0);
			LRESULT single = SendDlgItemMessageW(dialog,
				IDC_CONTROLLER_SINGLE, CB_GETCURSEL, 0, 0);
			if (source >= XAVIX_CONTROLLER_SOURCE_AUTO &&
				source <= XAVIX_CONTROLLER_SOURCE_WII_REMOTE)
				xavix_controller_input_set_source(&g_controller_input,
					(enum xavix_controller_source)source);
			if (single == 0 || single == 1)
				xavix_controller_input_set_single_reflector(
					&g_controller_input, (int)single);
			if (translated)
				xavix_controller_input_set_dead_zone(&g_controller_input,
					(int)dead_zone);
			for (action = 0; action < XAVIX_CONTROLLER_ACTION_COUNT; ++action)
				xavix_controller_input_set_binding(&g_controller_input,
					(enum xavix_controller_action)action,
					state->bindings[action]);
			KillTimer(dialog, 1);
			EndDialog(dialog, IDOK);
			return TRUE;
		}
		if (id == IDCANCEL)
		{
			KillTimer(dialog, 1);
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
	}
	return FALSE;
}

static void show_controller_settings_for_game(HWND window,
	enum drgqst_rom_kind kind, const wchar_t *title)
{
	controller_dialog_state state;
	if (kind != DRGQST_ROM_UNKNOWN &&
		!xavix_game_control_profile_for_kind(kind))
		return;
	const char *restore_profile = emulator_loaded() ?
		drgqst_rom_short_name(g_rom.kind) : "default";
	memset(&state, 0, sizeof(state));
	state.kind = kind;
	state.title = title;
	if (kind != DRGQST_ROM_UNKNOWN)
		xavix_controller_input_set_profile(&g_controller_input,
			drgqst_rom_short_name(kind));
	(void)DialogBoxParamW(GetModuleHandleW(NULL),
		MAKEINTRESOURCEW(IDD_CONTROLLER_SETTINGS), window,
		controller_dialog_procedure, (LPARAM)&state);
	xavix_controller_input_set_profile(&g_controller_input, restore_profile);
}

static void show_controller_settings(HWND window)
{
	const xavix_game_metadata *metadata = emulator_loaded() ?
		xavix_game_metadata_for_kind(g_rom.kind) : NULL;
	show_controller_settings_for_game(window,
		emulator_loaded() ? g_rom.kind : DRGQST_ROM_UNKNOWN,
		metadata ? metadata->title : NULL);
}

static unsigned logical_frame_width(void)
{
	return g_frame_width / (g_frame_pixel_scale ? g_frame_pixel_scale : 1);
}

static unsigned logical_frame_height(void)
{
	return g_frame_height / (g_frame_pixel_scale ? g_frame_pixel_scale : 1);
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
		viewport.scale = viewport.height / (int)logical_frame_height();
		if (viewport.scale < 1)
			viewport.scale = 1;
		viewport.x = (client_width - viewport.width) / 2;
		viewport.y = (client_height - viewport.height) / 2;
		return viewport;
	}

	scale_x = client_width / (int)logical_frame_width();
	scale_y = client_height / (int)logical_frame_height();
	viewport.scale = scale_x < scale_y ? scale_x : scale_y;
	if (viewport.scale < 1)
		viewport.scale = 1;
	viewport.width = (int)logical_frame_width() * viewport.scale;
	viewport.height = (int)logical_frame_height() * viewport.scale;
	viewport.x = (client_width - viewport.width) / 2;
	viewport.y = (client_height - viewport.height) / 2;
	return viewport;
}

static void resize_for_scale(HWND window, int scale)
{
	int client_width;
	int client_height = (int)logical_frame_height() * scale;
	RECT rectangle;
	LONG_PTR style;
	LONG_PTR extended_style;

	if (g_fullscreen)
		return;
	g_window_scale = scale;
	write_ini_integer(L"Video", L"WindowScale", scale);
	if (IsZoomed(window))
		ShowWindow(window, SW_RESTORE);
	client_width = g_stretch_4_3 ?
		(client_height * 4 + 1) / 3 : (int)logical_frame_width() * scale;
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
	write_ini_integer(L"Video", L"Stretch4x3", g_stretch_4_3);
	CheckMenuItem(g_menu, ID_VIEW_STRETCH_4_3,
		MF_BYCOMMAND | (g_stretch_4_3 ? MF_CHECKED : MF_UNCHECKED));
	if (!g_fullscreen && !IsZoomed(window))
		resize_for_scale(window, g_window_scale);
	InvalidateRect(window, NULL, FALSE);
}

static void toggle_high_resolution_3d(HWND window)
{
	unsigned width;
	unsigned height;
	unsigned stride;

	if (!g_xavix2)
		return;
	g_high_resolution_3d = !g_high_resolution_3d;
	write_ini_integer(L"Video", L"HighResolution3D",
		g_high_resolution_3d);
	xavix2_machine_set_high_resolution_3d(g_xavix2,
		g_high_resolution_3d);
	g_framebuffer = xavix2_machine_visible_frame(g_xavix2,
		&width, &height, &stride);
	g_frame_width = width;
	g_frame_height = height;
	g_frame_stride = stride;
	g_frame_pixel_scale = xavix2_machine_frame_scale(g_xavix2);
	CheckMenuItem(g_menu, ID_VIEW_HIGH_RESOLUTION_3D,
		MF_BYCOMMAND | (g_high_resolution_3d ? MF_CHECKED : MF_UNCHECKED));
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

static int controller_action_held(enum xavix_controller_action action)
{
	return (g_controller_reading.actions & (UINT32_C(1) << action)) != 0;
}

static int controller_action_pressed(enum xavix_controller_action action)
{
	return (g_controller_reading.pressed & (UINT32_C(1) << action)) != 0;
}

enum takecopter_tilt_direction
{
	TAKECOPTER_TILT_NEUTRAL,
	TAKECOPTER_TILT_FORWARD,
	TAKECOPTER_TILT_BACKWARD,
	TAKECOPTER_TILT_LEFT,
	TAKECOPTER_TILT_RIGHT
};

static void pulse_takecopter_tilt(unsigned direction)
{
	g_takecopter_direction_pulse = direction;
	/* The game accepts a direction only after its serial receiver and movement
	 * filters both settle.  Twelve reports are reliable at the native 60 Hz. */
	g_takecopter_direction_frames = 12;
}

static void start_takecopter_boost(void)
{
	/* The real toy boosts when the head is snapped backward and then forward. */
	g_takecopter_boost_phase = 1;
	g_takecopter_direction_frames = 0;
}

static int host_left_reflection(void)
{
	if (g_left_button || controller_action_held(XAVIX_CONTROLLER_PRIMARY) ||
		controller_action_held(XAVIX_CONTROLLER_CONFIRM))
		return 1;
	if (g_rom.kind == DRGQST_ROM_DRAGON_QUEST &&
		controller_action_held(XAVIX_CONTROLLER_DEFENSE))
		return 1;
	if ((g_rom.kind == DRGQST_ROM_TTV_SW ||
		g_rom.kind == DRGQST_ROM_TTV_SWJ) &&
		controller_action_held(XAVIX_CONTROLLER_DEFENSE))
		return 1;
	return 0;
}

static int host_right_reflection(void)
{
	if (g_right_button || controller_action_held(XAVIX_CONTROLLER_SECONDARY))
		return 1;
	if ((g_rom.kind == DRGQST_ROM_DRAGON_QUEST ||
		g_rom.kind == DRGQST_ROM_TTV_LOTR) &&
		(controller_action_held(XAVIX_CONTROLLER_SPECIAL) ||
		 controller_action_held(XAVIX_CONTROLLER_DEFENSE)))
		return 1;
	return 0;
}

static void update_core_mouse(void)
{
	int left = host_left_reflection();
	int right = host_right_reflection();
	int reverse = g_omt_backside ||
		controller_action_held(XAVIX_CONTROLLER_DEFENSE) ||
		controller_action_held(XAVIX_CONTROLLER_TWO_HAND);
	int spin = g_ttv_spin_held ||
		controller_action_held(XAVIX_CONTROLLER_SPECIAL);

	if (!g_core)
		return;
	if (rom_uses_tvpc_host_input(g_rom.kind))
	{
		g_core->machine.state.anport_regs[2] = g_tvpc_mouse_counter_x;
		g_core->machine.state.anport_regs[3] = g_tvpc_mouse_counter_y;
		if (left)
			g_core->machine.state.input0 |= 0x80;
		else
			g_core->machine.state.input0 &= (uint8_t)~0x80;
		return;
	}
	/* Duel Masters has ordinary cabinet buttons and a 32x32 optical card
	 * scanner. Keep feeding the scanner even though its buttons use the
	 * digital-input path below. */
	if (rom_uses_digital_direction_input(g_rom.kind) &&
		g_rom.kind != DRGQST_ROM_DUELMAST &&
		g_rom.kind != DRGQST_ROM_EPO_GOLF)
		return;
	if (g_rom.kind == DRGQST_ROM_TAK_CHQ)
		return;
	if (g_rom.kind == DRGQST_ROM_EPO_ES2J ||
		g_rom.kind == DRGQST_ROM_EPO_SDB)
		return;
	drgqst_core_set_mouse(g_core, g_mouse_x, g_mouse_y,
		left || (g_rom.kind == DRGQST_ROM_BAN_OMT && reverse),
		right || (g_rom.kind == DRGQST_ROM_BAN_OMT && reverse));
	if (g_rom.kind == DRGQST_ROM_BAN_ONEP &&
		g_controller_reading.connected &&
		(g_controller_reading.reflector[0].visible ||
		 g_controller_reading.reflector[1].visible))
	{
		const xavix_virtual_reflector *first =
			&g_controller_reading.reflector[0];
		const xavix_virtual_reflector *second =
			&g_controller_reading.reflector[1];
		drgqst_core_set_reflectors(g_core,
			first->x, first->y, first->area, first->visible,
			second->x, second->y, second->area, second->visible);
	}
	if (g_rom.kind == DRGQST_ROM_TTV_SW && !left && !right && !spin)
	{
		/* The US program treats the 3-by-3 Japanese narrow image as a held
		 * defensive pose.  A real moving edge is smaller and immediately
		 * leaves the camera field; keep only a one-frame point sample. */
		xavix_machine_set_sword_input(&g_core->machine, g_mouse_x, g_mouse_y,
			g_ttv_sw_motion_frames ? XAVIX_SENSOR_POINT : XAVIX_SENSOR_NONE);
	}
	if ((g_rom.kind == DRGQST_ROM_TTV_SW ||
		g_rom.kind == DRGQST_ROM_TTV_SWJ) && spin)
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

/* Raw indices are the original TV-PC's 8x8 external keyboard matrix. The
 * layout was recovered from the 37200 manual and verified in Hello Kitty's
 * mailbox name editor without patching guest RAM. */
static int tvpc_hk_key_index(WPARAM key, LPARAM lparam, unsigned *index)
{
	const unsigned scan_code = (unsigned)((lparam >> 16) & 0xff);

	if (!index)
		return 0;
	switch (key)
	{
	case '1': *index = 5; break;
	case '2': *index = 4; break;
	case '3': *index = 3; break;
	case '4': *index = 2; break;
	case '5': *index = 1; break;
	case '6': *index = 0; break;
	case '7': *index = 7; break;
	case '8': *index = 23; break;
	case '9': *index = 22; break;
	case '0': *index = 21; break;
	case VK_OEM_MINUS: *index = 20; break;
	case VK_OEM_PLUS: *index = 19; break;
	case VK_OEM_3: *index = 18; break;
	case 'Q': *index = 31; break;
	case 'W': *index = 30; break;
	case 'E': *index = 29; break;
	case 'R': *index = 28; break;
	case 'T': *index = 27; break;
	case 'Y': *index = 26; break;
	case 'U': *index = 25; break;
	case 'I': *index = 24; break;
	case 'O': *index = 35; break;
	case 'P': *index = 34; break;
	case VK_OEM_4: *index = 48; break;
	case VK_OEM_6: *index = 62; break;
	case 'A': *index = 39; break;
	case 'S': *index = 38; break;
	case 'D': *index = 37; break;
	case 'F': *index = 36; break;
	case 'G': *index = 47; break;
	case 'H': *index = 46; break;
	case 'J': *index = 45; break;
	case 'K': *index = 44; break;
	case 'L': *index = 43; break;
	case VK_OEM_1: *index = 42; break;
	case VK_OEM_7: *index = 41; break;
	case VK_OEM_8: *index = 40; break;
	case VK_CONTROL:
		if (!(lparam & ((LPARAM)1 << 24)))
			return 0;
		*index = 40;
		break;
	case 'Z': *index = 55; break;
	case 'X': *index = 54; break;
	case 'C': *index = 53; break;
	case 'V': *index = 52; break;
	case 'B': *index = 51; break;
	case 'N': *index = 50; break;
	case 'M': *index = 49; break;
	case VK_OEM_COMMA: *index = 61; break;
	case VK_OEM_PERIOD: *index = 60; break;
	case VK_OEM_2: *index = 59; break;
	case VK_OEM_5: *index = 58; break;
	case VK_ESCAPE: *index = 6; break;
	case VK_F2: *index = 16; break; /* Family Mail shortcut. */
	case VK_BACK: *index = 17; break;
	case VK_RETURN: *index = 56; break;
	case VK_SPACE: *index = 57; break;
	case VK_TAB: *index = 63; break; /* Input-mode key. */
	case VK_LSHIFT: *index = 32; break;
	case VK_RSHIFT: *index = 33; break;
	case VK_SHIFT:
		*index = scan_code == 0x36 ? 33 : 32;
		break;
	case VK_UP: *index = 8; break;
	case VK_DOWN: *index = 9; break;
	case VK_LEFT: *index = 10; break;
	case VK_RIGHT: *index = 11; break;
	case VK_NUMPAD8: *index = 12; break;
	case VK_NUMPAD2: *index = 13; break;
	case VK_NUMPAD4: *index = 14; break;
	case VK_NUMPAD6: *index = 15; break;
	default:
		return 0;
	}
	return 1;
}

static int set_tvpc_keyboard_key(WPARAM key, LPARAM lparam, int pressed)
{
	unsigned index;

	if (!g_core || !rom_uses_tvpc_host_input(g_rom.kind))
		return 0;
	if (g_rom.kind == DRGQST_ROM_TVPC_DOR)
	{
		switch (key)
		{
		case VK_UP:
		case 'W': index = 8; break;
		case VK_DOWN:
		case 'S': index = 9; break;
		case VK_LEFT:
		case 'A': index = 10; break;
		case VK_RIGHT:
		case 'D': index = 11; break;
		case VK_ESCAPE: index = 6; break;
		default: return 0;
		}
	}
	else if (!tvpc_hk_key_index(key, lparam, &index))
		return 0;
	if (pressed)
		g_tvpc_keyboard_rows[index / 8] |= (uint8_t)(1U << (index & 7));
	else
		g_tvpc_keyboard_rows[index / 8] &=
			(uint8_t)~(uint8_t)(1U << (index & 7));
	return 1;
}

static void update_tvpc_keyboard(void)
{
	uint8_t rows[8];
	unsigned row;

	if (!g_core || !rom_uses_tvpc_host_input(g_rom.kind))
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

static void resolve_digital_directions(int pulse_analogue,
	int *up, int *down, int *left, int *right)
{
	uint8_t axis_x = g_mouse_x;
	uint8_t axis_y = g_mouse_y;

	*up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0 ||
		(GetAsyncKeyState('W') & 0x8000) != 0;
	*down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
		(GetAsyncKeyState('S') & 0x8000) != 0;
	*left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 ||
		(GetAsyncKeyState('A') & 0x8000) != 0;
	*right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 ||
		(GetAsyncKeyState('D') & 0x8000) != 0;
	if (*up || *down || *left || *right)
		return;
	if (g_controller_reading.connected &&
		g_controller_reading.active_source ==
			XAVIX_CONTROLLER_SOURCE_GAMEPAD &&
		g_controller_reading.gamepad_axes_valid)
	{
		axis_x = g_controller_reading.gamepad_axis_x[0];
		axis_y = g_controller_reading.gamepad_axis_y[0];
	}
	if (pulse_analogue)
	{
		int horizontal = xavix_controller_pulse_digital_axis(axis_x,
			g_motion_racing_phase);
		int vertical = xavix_controller_pulse_digital_axis(axis_y,
			g_motion_racing_phase);
		*left = horizontal < 0;
		*right = horizontal > 0;
		*up = vertical < 0;
		*down = vertical > 0;
	}
	else
	{
		*up = axis_y < 0x60;
		*down = axis_y > 0x9f;
		*left = axis_x < 0x60;
		*right = axis_x > 0x9f;
	}
}

static void update_digital_direction_input(void)
{
	uint8_t input = 0;
	uint8_t input1 = 0;
	int up;
	int down;
	int left;
	int right;
	const int primary = host_left_reflection() ||
		(GetAsyncKeyState(VK_SPACE) & 0x8000) ||
		controller_action_held(XAVIX_CONTROLLER_PRIMARY);
	const int secondary = host_right_reflection() ||
		(GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
		controller_action_held(XAVIX_CONTROLLER_SECONDARY);
	const int confirm = (GetAsyncKeyState(VK_RETURN) & 0x8000) ||
		controller_action_held(XAVIX_CONTROLLER_CONFIRM);
	const int special = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ||
		controller_action_held(XAVIX_CONTROLLER_SPECIAL);
	const int defense = controller_action_held(XAVIX_CONTROLLER_DEFENSE);
	const int two_hand = controller_action_held(XAVIX_CONTROLLER_TWO_HAND);
	const int deflect = controller_action_held(XAVIX_CONTROLLER_DEFLECT);

	if (!g_core || !rom_uses_digital_direction_input(g_rom.kind))
		return;
	resolve_digital_directions(rom_uses_motion_racing_input(g_rom.kind),
		&up, &down, &left, &right);

	switch (g_rom.kind)
	{
	case DRGQST_ROM_RAD_MTRK:
		/* The wheel is a 20 Hz pulse source. Bit 7 carries direction and
		 * IO-event 3 is one physical encoder step. Pulse-density conversion
		 * preserves small analogue corrections instead of latching full lock. */
		input1 = 0x40; /* power switch released (active low) */
		if (special || (GetAsyncKeyState('N') & 0x8000)) input |= 0x01;
		if (primary || up) input |= 0x04;
		if (secondary || down) input |= 0x08;
		if (defense || (GetAsyncKeyState('R') & 0x8000)) input |= 0x10;
		if (confirm || (GetAsyncKeyState('H') & 0x8000)) input1 |= 0x02;
		if (left != right)
		{
			g_rad_mtrk_wheel_direction = right ? 0x80 : 0x00;
			if ((g_motion_racing_phase % 3U) == 0)
				xavix_machine_trigger_ioevent(&g_core->machine, 0x08);
		}
		input |= g_rad_mtrk_wheel_direction;
		break;

	case DRGQST_ROM_RAD_SNOW:
	case DRGQST_ROM_RAD_SSX:
	case DRGQST_ROM_RAD_SBW:
		input1 = 0x40; /* power switch released (active low) */
		if (primary || up) input |= 0x01;
		if (secondary || down || confirm) input |= 0x08;
		if (left && !right) input |= 0x10;
		else if (right && !left) input |= 0x20;
		break;

	case DRGQST_ROM_TAK_GIN:
		if (up && !down) input |= 0x01;
		else if (down && !up) input |= 0x02;
		if (left && !right) input |= 0x04;
		else if (right && !left) input |= 0x08;
		if (primary) input |= 0x01;
		break;

	case DRGQST_ROM_TCARNAVI:
		input = 0x20; /* dashboard power toggle released (active low) */
		input1 = 0x40; /* main power released (active low) */
		if (primary || up) input |= 0x01;
		if (secondary || down) input |= 0x02;
		if (special || (GetAsyncKeyState('C') & 0x8000)) input |= 0x04;
		if (defense || (GetAsyncKeyState('R') & 0x8000)) input |= 0x08;
		if (confirm || (GetAsyncKeyState('K') & 0x8000)) input |= 0x10;
		if (left && !right) input |= 0x40;
		else if (right && !left) input |= 0x80;
		if (deflect || (GetAsyncKeyState('L') & 0x8000)) input1 |= 0x01;
		if (two_hand || (GetAsyncKeyState('H') & 0x8000)) input1 |= 0x02;
		if (GetAsyncKeyState('V') & 0x8000) input1 |= 0x04;
		if (GetAsyncKeyState('M') & 0x8000) input1 |= 0x08;
		break;

	case DRGQST_ROM_TOMTHR:
		if (up && !down) input |= 0x01;
		else if (down && !up) input |= 0x02;
		if (left && !right) input |= 0x04;
		else if (right && !left) input |= 0x08;
		if (primary || confirm) input |= 0x10; /* horn/select */
		if (secondary || (GetAsyncKeyState('I') & 0x8000)) input |= 0x20;
		if (special || (GetAsyncKeyState('M') & 0x8000)) input |= 0x40;
		if (defense || (GetAsyncKeyState('V') & 0x8000)) input |= 0x80;
		if (deflect || (GetAsyncKeyState('L') & 0x8000)) input1 |= 0x01;
		g_core->machine.state.anport_regs[0] =
			(two_hand || (GetAsyncKeyState('Q') & 0x8000)) ? 0x80 : 0x00;
		break;

	case DRGQST_ROM_DUELMAST:
		if (primary || confirm) input |= 0x01;
		if (secondary) input |= 0x02;
		if (up && !down) input |= 0x04;
		else if (down && !up) input |= 0x08;
		if (right && !left) input |= 0x10;
		else if (left && !right) input |= 0x20;
		if (special || defense) input |= 0x40;
		break;
	case DRGQST_ROM_EPO_GOLF:
		if (primary || confirm) input |= 0x01; /* circle: caddy/confirm */
		if (secondary) input |= 0x02; /* cross: model/back */
		break;
	case DRGQST_ROM_TTV_MX:
	case DRGQST_ROM_TOM_JUMP:
	case DRGQST_ROM_EPO_EBOX:
	default:
		if (up && !down) input |= 0x10;
		else if (down && !up) input |= 0x20;
		if (left && !right) input |= 0x40;
		else if (right && !left) input |= 0x80;
		if (primary) input |= 0x01;
		if (secondary) input |= 0x02;
		if (g_rom.kind != DRGQST_ROM_EPO_EBOX &&
			(special || (GetAsyncKeyState('P') & 0x8000)))
			input |= 0x04;
		break;
	}
	g_core->machine.state.input0 = input;
	g_core->machine.state.input1 = input1;
	if (rom_uses_motion_racing_input(g_rom.kind))
		g_motion_racing_phase = (g_motion_racing_phase + 1) % 24U;
}

static uint8_t early_motion_swing_sample(unsigned phase)
{
	phase &= 63U;
	if (phase < 8U || phase >= 56U)
		return 0x80;
	if (phase < 16U)
		return (uint8_t)(0x80U - (phase - 7U) * 0x0eU);
	if (phase < 32U)
		return 0x10;
	if (phase < 40U)
		return (uint8_t)(0x10U + (phase - 31U) * 0x1cU);
	return 0xf0;
}

static int early_motion_axis_is_neutral(uint8_t value)
{
	return value >= 0x68 && value <= 0x98;
}

static uint8_t early_motion_dominant_axis(uint8_t x, uint8_t y)
{
	const int dx = x > 0x80 ? x - 0x80 : 0x80 - x;
	const int dy = y > 0x80 ? y - 0x80 : 0x80 - y;
	return dx > dy ? x : y;
}

static void update_early_motion_input(void)
{
	uint8_t first_x = g_mouse_x;
	uint8_t first_y = g_mouse_y;
	uint8_t second_x = 0x80;
	uint8_t second_y = 0x80;
	uint8_t channel5;
	uint8_t channel7;
	uint8_t buttons = 0;
	const int first_action = host_left_reflection();
	const int second_action = host_right_reflection();
	const int first_left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 ||
		(GetAsyncKeyState('A') & 0x8000) != 0;
	const int first_right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 ||
		(GetAsyncKeyState('D') & 0x8000) != 0;
	const int first_up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0 ||
		(GetAsyncKeyState('W') & 0x8000) != 0;
	const int first_down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
		(GetAsyncKeyState('S') & 0x8000) != 0;
	const int second_left = (GetAsyncKeyState('J') & 0x8000) != 0;
	const int second_right = (GetAsyncKeyState('L') & 0x8000) != 0;
	const int second_up = (GetAsyncKeyState('I') & 0x8000) != 0;
	const int second_down = (GetAsyncKeyState('K') & 0x8000) != 0;

	if (!g_core || (g_rom.kind != DRGQST_ROM_EPO_CROK &&
		g_rom.kind != DRGQST_ROM_TAK_ZUBA))
		return;

	if (g_controller_reading.connected &&
		g_controller_reading.active_source == XAVIX_CONTROLLER_SOURCE_GAMEPAD &&
		g_controller_reading.gamepad_axes_valid)
	{
		first_x = g_controller_reading.gamepad_axis_x[0];
		first_y = g_controller_reading.gamepad_axis_y[0];
		second_x = g_controller_reading.gamepad_axis_x[1];
		second_y = g_controller_reading.gamepad_axis_y[1];
	}
	else if (g_controller_reading.connected &&
		g_controller_reading.active_source == XAVIX_CONTROLLER_SOURCE_WII_REMOTE)
	{
		if (g_controller_reading.reflector[0].visible)
		{
			first_x = g_controller_reading.reflector[0].x;
			first_y = g_controller_reading.reflector[0].y;
		}
		if (g_controller_reading.reflector[1].visible)
		{
			second_x = g_controller_reading.reflector[1].x;
			second_y = g_controller_reading.reflector[1].y;
		}
	}
	else if (second_action && !first_action)
	{
		second_x = g_mouse_x;
		second_y = g_mouse_y;
		first_x = 0x80;
		first_y = 0x80;
	}

	if (first_left != first_right) first_x = first_left ? 0x10 : 0xf0;
	if (first_up != first_down) first_y = first_up ? 0x10 : 0xf0;
	if (second_left != second_right) second_x = second_left ? 0x10 : 0xf0;
	if (second_up != second_down) second_y = second_up ? 0x10 : 0xf0;
	channel5 = early_motion_dominant_axis(first_x, first_y);
	channel7 = early_motion_dominant_axis(second_x, second_y);

	/* The firmware low-pass filters sixteen alternating ADC5/ADC7 samples.
	 * Hold each synthesized excursion long enough to survive that filter. */
	if (first_action && early_motion_axis_is_neutral(channel5))
		channel5 = early_motion_swing_sample(g_early_motion_phase);
	if (second_action && early_motion_axis_is_neutral(channel7))
		channel7 = early_motion_swing_sample(g_early_motion_phase + 32U);

	drgqst_core_set_early_motion_input(g_core, channel5, channel7);

	/* Croket's red button and Zuba's sword triggers remain independent of
	 * their two analog motion channels. */
	if (controller_action_held(XAVIX_CONTROLLER_DEFENSE)) buttons |= 0x01;
	if (g_rom.kind == DRGQST_ROM_TAK_ZUBA &&
		controller_action_held(XAVIX_CONTROLLER_SPECIAL)) buttons |= 0x02;
	g_core->machine.state.input0 = buttons;
	g_early_motion_phase = (g_early_motion_phase + 1U) & 63U;
}
static void update_sdb_input(void)
{
	uint8_t player1_x = g_mouse_x;
	uint8_t player1_y = g_mouse_y;
	uint8_t player2_x = 0x80;
	uint8_t player2_y = 0x80;
	int player1_left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 ||
		(GetAsyncKeyState('A') & 0x8000) != 0;
	int player1_right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 ||
		(GetAsyncKeyState('D') & 0x8000) != 0;
	int player1_up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0 ||
		(GetAsyncKeyState('W') & 0x8000) != 0;
	int player1_down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
		(GetAsyncKeyState('S') & 0x8000) != 0;
	int player2_left = (GetAsyncKeyState('J') & 0x8000) != 0;
	int player2_right = (GetAsyncKeyState('L') & 0x8000) != 0;
	int player2_up = (GetAsyncKeyState('I') & 0x8000) != 0;
	int player2_down = (GetAsyncKeyState('K') & 0x8000) != 0;

	if (!g_core || g_rom.kind != DRGQST_ROM_EPO_SDB)
		return;
	if (g_controller_reading.connected &&
		g_controller_reading.active_source ==
			XAVIX_CONTROLLER_SOURCE_GAMEPAD &&
		g_controller_reading.gamepad_axes_valid)
	{
		player1_x = g_controller_reading.gamepad_axis_x[0];
		player1_y = g_controller_reading.gamepad_axis_y[0];
		player2_x = g_controller_reading.gamepad_axis_x[1];
		player2_y = g_controller_reading.gamepad_axis_y[1];
	}
	if (player1_left != player1_right)
		player1_x = player1_left ? 0x01 : 0xfe;
	if (player1_up != player1_down)
		player1_y = player1_up ? 0x01 : 0xfe;
	if (player2_left != player2_right)
		player2_x = player2_left ? 0x01 : 0xfe;
	if (player2_up != player2_down)
		player2_y = player2_up ? 0x01 : 0xfe;
	drgqst_core_set_sdb_input(g_core, 0, player1_x, player1_y,
		host_left_reflection());
	drgqst_core_set_sdb_input(g_core, 1, player2_x, player2_y,
		host_right_reflection());
}

static void update_epo_bowl_input(void)
{
	uint8_t input = 0;
	int up;
	int down;
	int left;
	int right;

	if (!g_core || g_rom.kind != DRGQST_ROM_EPO_BOWL)
		return;
	resolve_digital_directions(0, &up, &down, &left, &right);
	if (host_left_reflection()) input |= 0x01;
	if (host_right_reflection()) input |= 0x02;
	if (up && !down) input |= 0x04;
	else if (down && !up) input |= 0x08;
	if (right && !left) input |= 0x10;
	else if (left && !right) input |= 0x20;
	g_core->machine.state.input0 = input;
}

static void update_tak_chq_input(void)
{
	uint8_t wheel = g_tak_chq_wheel;
	uint8_t player2_wheel = 0x80;
	uint8_t input = 0;
	const int left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 ||
		(GetAsyncKeyState('A') & 0x8000) != 0;
	const int right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 ||
		(GetAsyncKeyState('D') & 0x8000) != 0;

	if (!g_core || g_rom.kind != DRGQST_ROM_TAK_CHQ)
		return;
	if (left != right)
	{
		g_tak_chq_mouse_delta = 0;
		wheel = xavix_controller_approach_axis(wheel,
			left ? 0x40 : 0xc0, 2);
	}
	else if (g_left_button || g_right_button)
	{
		if (g_tak_chq_mouse_delta)
		{
			int position = (int)wheel + g_tak_chq_mouse_delta;
			if (position < 0x40) position = 0x40;
			if (position > 0xc0) position = 0xc0;
			wheel = (uint8_t)position;
			g_tak_chq_mouse_delta = 0;
		}
		else
			wheel = xavix_controller_approach_axis(wheel, 0x80, 4);
	}
	else if (g_controller_reading.connected &&
		g_controller_reading.active_source ==
			XAVIX_CONTROLLER_SOURCE_GAMEPAD &&
		g_controller_reading.gamepad_axes_valid)
	{
		g_tak_chq_mouse_delta = 0;
		wheel = xavix_controller_curve_racing_axis(
			g_controller_reading.gamepad_axis_x[0]);
		player2_wheel = xavix_controller_curve_racing_axis(
			g_controller_reading.gamepad_axis_x[1]);
	}
	else
		wheel = xavix_controller_approach_axis(wheel, 0x80, 4);
	g_tak_chq_wheel = wheel;

	/* The physical grip has accelerator, brake and rear command buttons.
	 * P0 probes identify them as active-high $20, $10 and $80 respectively. */
	if ((GetAsyncKeyState(VK_UP) & 0x8000) ||
		(GetAsyncKeyState('W') & 0x8000) ||
		(GetAsyncKeyState(VK_SPACE) & 0x8000) ||
		host_left_reflection())
		input |= 0x20;
	if ((GetAsyncKeyState(VK_DOWN) & 0x8000) ||
		(GetAsyncKeyState('S') & 0x8000) ||
		(GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
		host_right_reflection())
		input |= 0x10;
	if ((GetAsyncKeyState('C') & 0x8000) ||
		(GetAsyncKeyState(VK_MBUTTON) & 0x8000) ||
		controller_action_held(XAVIX_CONTROLLER_SPECIAL))
		input |= 0x80;

	g_core->machine.state.anport_regs[2] =
		xavix_controller_encode_racing_wheel(wheel);
	g_core->machine.state.anport_regs[3] =
		xavix_controller_encode_racing_wheel(player2_wheel);
	g_core->machine.state.input0 = input;
}
static void release_held_host_inputs(HWND window)
{
	g_left_button = 0;
	g_right_button = 0;
	g_takecopter_mouse_drag_valid = 0;
	g_takecopter_direction_pulse = TAKECOPTER_TILT_NEUTRAL;
	g_takecopter_direction_frames = 0;
	g_takecopter_boost_phase = 0;
	g_tak_chq_mouse_delta = 0;
	g_tak_chq_mouse_recentering = 0;
	g_tak_chq_wheel = 0x80;
	g_motion_racing_phase = 0;
	g_early_motion_phase = 0;
	g_rad_mtrk_wheel_direction = 0x80;
	if (g_rom.kind == DRGQST_ROM_EPO_DTCJ)
	{
		g_mouse_x = 0x80;
		g_mouse_y = 0x80;
	}
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
	g_xavix2_area_gesture_frame = 0;
	g_xavix2_area_gesture_frames = 0;
	g_xavix2_gesture_kind = 0;
	if (GetCapture() == window)
		ReleaseCapture();
	update_core_mouse();
}

static void advance_ttv_special_gesture(void)
{
	if (g_ttv_spin_held ||
		controller_action_held(XAVIX_CONTROLLER_SPECIAL))
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

static int join_application_path(const wchar_t *name, wchar_t *path,
	size_t path_length)
{
	size_t base_length = wcslen(g_executable_directory);
	size_t name_length = name ? wcslen(name) : 0;
	int separator = base_length &&
		g_executable_directory[base_length - 1] != L'\\' &&
		g_executable_directory[base_length - 1] != L'/';

	if (!name_length || base_length + (size_t)separator + name_length >=
		path_length)
		return 0;
	memcpy(path, g_executable_directory, base_length * sizeof(*path));
	if (separator)
		path[base_length++] = L'\\';
	memcpy(path + base_length, name, (name_length + 1) * sizeof(*path));
	return 1;
}

static int ensure_application_directory(const wchar_t *path)
{
	DWORD attributes;

	if (CreateDirectoryW(path, NULL))
		return 1;
	if (GetLastError() != ERROR_ALREADY_EXISTS)
		return 0;
	attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int initialize_application_paths(void)
{
	return join_application_path(L"XaviXEmu.ini", g_ini_path,
			sizeof(g_ini_path) / sizeof(g_ini_path[0])) &&
		join_application_path(L"save", g_save_directory,
			sizeof(g_save_directory) / sizeof(g_save_directory[0])) &&
		join_application_path(L"snap", g_snap_directory,
			sizeof(g_snap_directory) / sizeof(g_snap_directory[0])) &&
		ensure_application_directory(g_save_directory) &&
		ensure_application_directory(g_snap_directory);
}

static void load_application_settings(void)
{
	wchar_t language[32];
	int scale;

	GetPrivateProfileStringW(L"Interface", L"Language", L"auto",
		language, sizeof(language) / sizeof(language[0]), g_ini_path);
	if (_wcsicmp(language, L"zh-TW") == 0)
		g_language_preference = LANGUAGE_PREFERENCE_ZH_TW;
	else if (_wcsicmp(language, L"ja") == 0)
		g_language_preference = LANGUAGE_PREFERENCE_JAPANESE;
	else if (_wcsicmp(language, L"fr") == 0)
		g_language_preference = LANGUAGE_PREFERENCE_FRENCH;
	else if (_wcsicmp(language, L"en") == 0)
		g_language_preference = LANGUAGE_PREFERENCE_ENGLISH;
	else
		g_language_preference = LANGUAGE_PREFERENCE_AUTO;
	g_language = resolved_language(g_language_preference);
	g_stretch_4_3 = GetPrivateProfileIntW(L"Video", L"Stretch4x3", 1,
		g_ini_path) != 0;
	g_high_resolution_3d = GetPrivateProfileIntW(L"Video",
		L"HighResolution3D", 0, g_ini_path) != 0;
	g_record_window_size = GetPrivateProfileIntW(L"Video",
		L"RecordWindowSize", 1, g_ini_path) != 0;
	g_recording_format = GetPrivateProfileIntW(L"Video",
		L"RecordingFormat", XAVIX_VIDEO_FORMAT_AVI, g_ini_path) ==
		XAVIX_VIDEO_FORMAT_MP4 ? XAVIX_VIDEO_FORMAT_MP4 :
		XAVIX_VIDEO_FORMAT_AVI;
	scale = GetPrivateProfileIntW(L"Video", L"WindowScale", DEFAULT_SCALE,
		g_ini_path);
	g_window_scale = scale >= 1 && scale <= 4 ? scale : DEFAULT_SCALE;
	GetPrivateProfileStringW(L"Library", L"RomDirectory", L"",
		g_rom_directory, sizeof(g_rom_directory) / sizeof(g_rom_directory[0]),
		g_ini_path);
	scale = GetPrivateProfileIntW(L"Library", L"SortColumn",
		XAVIX_GAME_SORT_TITLE, g_ini_path);
	g_game_library_sort = scale >= XAVIX_GAME_SORT_TITLE &&
		scale <= XAVIX_GAME_SORT_FILE ?
		(enum xavix_game_library_sort)scale : XAVIX_GAME_SORT_TITLE;
}

static int persistence_file_is_missing(const wchar_t *directory,
	enum drgqst_persistence_kind kind)
{
	wchar_t path[MAX_PATH];
	wchar_t error[256];
	DWORD windows_error;

	if (!drgqst_persistence_get_path(directory, kind, path,
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

	if (drgqst_persistence_load(g_save_directory, stored_kind, rom_sha1,
		payload, payload_capacity, payload_size, error, error_length))
		return 1;
	if (!persistence_file_is_missing(g_save_directory, stored_kind))
		return 0;
	if (drgqst_persistence_load(g_executable_directory, stored_kind, rom_sha1,
		payload, payload_capacity, payload_size, error, error_length))
	{
		if (loaded_from_legacy)
			*loaded_from_legacy = 1;
		return 1;
	}
	if (!persistence_file_is_missing(g_executable_directory, stored_kind) ||
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
			!drgqst_persistence_save(g_save_directory,
				persistence_kind_for_rom(DRGQST_PERSISTENCE_EEPROM, rom_kind),
				rom_sha1_for_kind(rom_kind), image, size,
				error, sizeof(error) / sizeof(error[0])))
			MessageBoxW(window, text->eeprom_save_error,
				text->eeprom_save_title, MB_OK | MB_ICONERROR);
	}
}

static void load_persistent_xavix2_eeprom(HWND window,
	xavix2_machine_t *machine, enum drgqst_rom_kind rom_kind)
{
	const interface_strings *text = interface_text();
	uint8_t image[DRGQST_PERSISTENCE_EEPROM24C04_SIZE];
	wchar_t error[384];
	size_t expected_size = eeprom_size_for_rom(rom_kind);
	size_t size = 0;
	int loaded_from_legacy = 0;

	if (!machine || !expected_size)
		return;
	if (load_persistence_data(DRGQST_PERSISTENCE_EEPROM, rom_kind,
		image, expected_size, &size, error,
		sizeof(error) / sizeof(error[0]), &loaded_from_legacy) &&
		size == expected_size)
	{
		xavix_eeprom_load_image(&machine->eeprom, image, size);
		if (loaded_from_legacy &&
			!drgqst_persistence_save(g_save_directory,
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

	if (!g_core && !g_xavix2)
		return 1;
	size = eeprom_size_for_rom(g_rom.kind);
	if (g_xavix2)
	{
		if (!size)
			return 1;
		eeprom = &g_xavix2->eeprom;
		if (!xavix_eeprom24c08_is_dirty(eeprom))
			return 1;
		xavix_eeprom_copy_image(eeprom, image, size);
		if (!drgqst_persistence_save(g_save_directory,
			persistence_kind_for_rom(DRGQST_PERSISTENCE_EEPROM, g_rom.kind),
			rom_sha1_for_kind(g_rom.kind), image, size, error,
			sizeof(error) / sizeof(error[0])))
		{
			if (show_error || !g_eeprom_error_shown)
				MessageBoxW(window, text->eeprom_save_error,
					text->eeprom_save_title, MB_OK | MB_ICONWARNING);
			g_eeprom_error_shown = 1;
			return 0;
		}
		xavix_eeprom24c08_clear_dirty(eeprom);
		g_eeprom_error_shown = 0;
		return 1;
	}
	if (rom_uses_parallel_nvram(g_rom.kind))
	{
		if (!drgqst_persistence_save(g_save_directory,
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
	if (!size)
		return 1;
	eeprom = &g_core->machine.state.peripherals.eeprom;
	if (!xavix_eeprom24c08_is_dirty(eeprom))
		return 1;
	xavix_eeprom_copy_image(eeprom, image, size);
	if (!drgqst_persistence_save(g_save_directory,
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

	if (!g_core && !g_xavix2)
		return;
	if (g_xavix2)
	{
		if (!eeprom_size_for_rom(g_rom.kind))
			return;
		eeprom = &g_xavix2->eeprom;
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
		return;
	}
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

	if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		return;

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
	if (g_core && rom_uses_tvpc_host_input(g_rom.kind))
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
			if (g_rom.kind == DRGQST_ROM_TVPC_DOR && delta_y < 0)
				g_tvpc_mouse_key_pending = 0x01;
			else if (g_rom.kind == DRGQST_ROM_TVPC_DOR &&
				delta_y > 0)
				g_tvpc_mouse_key_pending = 0x02;
		}
		g_tvpc_mouse_position_valid = 1;
	}
	g_mouse_x = new_mouse_x;
	g_mouse_y = new_mouse_y;
	update_core_mouse();
}

static void begin_takecopter_mouse_drag(int client_x, int client_y)
{
	g_mouse_x = 0x80;
	g_mouse_y = 0x80;
	g_takecopter_mouse_drag_x = client_x;
	g_takecopter_mouse_drag_y = client_y;
	g_takecopter_mouse_drag_valid = 1;
}

static void center_tak_chq_mouse(HWND window)
{
	display_viewport viewport = calculate_viewport(window);
	POINT center;

	center.x = viewport.x + viewport.width / 2;
	center.y = viewport.y + viewport.height / 2;
	g_tak_chq_mouse_recentering = 1;
	ClientToScreen(window, &center);
	SetCursorPos(center.x, center.y);
}

static void begin_tak_chq_mouse_steering(HWND window)
{
	g_tak_chq_mouse_delta = 0;
	center_tak_chq_mouse(window);
}

static void update_tak_chq_mouse_steering(HWND window, int client_x,
	int client_y)
{
	display_viewport viewport = calculate_viewport(window);
	int center_x = viewport.x + viewport.width / 2;
	int center_y = viewport.y + viewport.height / 2;
	int delta_x = client_x - center_x;
	int scaled;

	if (!g_left_button && !g_right_button)
		return;
	if (g_tak_chq_mouse_recentering &&
		delta_x >= -1 && delta_x <= 1 &&
		client_y - center_y >= -1 && client_y - center_y <= 1)
	{
		g_tak_chq_mouse_recentering = 0;
		return;
	}
	if (!delta_x)
		return;
	scaled = viewport.width > 0 ? MulDiv(delta_x, 192, viewport.width) : 0;
	if (!scaled)
		scaled = delta_x < 0 ? -1 : 1;
	g_tak_chq_mouse_delta += scaled;
	if (g_tak_chq_mouse_delta < -64) g_tak_chq_mouse_delta = -64;
	if (g_tak_chq_mouse_delta > 64) g_tak_chq_mouse_delta = 64;
	center_tak_chq_mouse(window);
}

static void update_takecopter_mouse_drag(HWND window, int client_x,
	int client_y)
{
	display_viewport viewport;
	int delta_x;
	int delta_y;
	int scaled_x;
	int scaled_y;
	int position;

	if (!g_right_button)
		return;
	if (!g_takecopter_mouse_drag_valid)
	{
		begin_takecopter_mouse_drag(client_x, client_y);
		return;
	}
	viewport = calculate_viewport(window);
	delta_x = client_x - g_takecopter_mouse_drag_x;
	delta_y = client_y - g_takecopter_mouse_drag_y;
	g_takecopter_mouse_drag_x = client_x;
	g_takecopter_mouse_drag_y = client_y;
	scaled_x = viewport.width > 0 ? MulDiv(delta_x, 255, viewport.width) : 0;
	scaled_y = viewport.height > 0 ? MulDiv(delta_y, 255, viewport.height) : 0;
	if (delta_x && !scaled_x) scaled_x = delta_x < 0 ? -1 : 1;
	if (delta_y && !scaled_y) scaled_y = delta_y < 0 ? -1 : 1;
	position = (int)g_mouse_x + scaled_x;
	g_mouse_x = (uint8_t)(position < 0 ? 0 : position > 255 ? 255 : position);
	position = (int)g_mouse_y + scaled_y;
	g_mouse_y = (uint8_t)(position < 0 ? 0 : position > 255 ? 255 : position);
}

static void update_controller_host_input(void)
{
	unsigned reflector;
	uint8_t old_x = g_mouse_x;
	uint8_t old_y = g_mouse_y;

	xavix_controller_input_update(&g_controller_input,
		&g_controller_reading);
	if (g_controller_reading.connected)
	{
		reflector = g_core || g_rom.kind == DRGQST_ROM_EPO_DTCJ ?
			(unsigned)xavix_controller_input_single_reflector(
				&g_controller_input) : 0;
		if (g_rom.kind == DRGQST_ROM_EPO_DTCJ && reflector < 2 &&
			g_controller_reading.active_source ==
				XAVIX_CONTROLLER_SOURCE_GAMEPAD &&
			g_controller_reading.gamepad_axes_valid)
		{
			g_mouse_x = g_controller_reading.gamepad_axis_x[reflector];
			g_mouse_y = g_controller_reading.gamepad_axis_y[reflector];
		}
		else if (reflector < 2 &&
			g_controller_reading.reflector[reflector].visible)
		{
			g_mouse_x = g_controller_reading.reflector[reflector].x;
			g_mouse_y = g_controller_reading.reflector[reflector].y;
		}
	}
	if (g_core && g_rom.kind == DRGQST_ROM_TTV_SW &&
		(old_x != g_mouse_x || old_y != g_mouse_y))
		g_ttv_sw_motion_frames = 1;

	if (controller_action_pressed(XAVIX_CONTROLLER_PRIMARY) ||
		controller_action_pressed(XAVIX_CONTROLLER_CONFIRM))
	{
		if (g_xavix2)
		{
			if (g_rom.kind == DRGQST_ROM_EPO_DTCJ)
				pulse_takecopter_tilt(TAKECOPTER_TILT_FORWARD);
			else if (g_rom.kind == DRGQST_ROM_BAN_BLDJ &&
				controller_action_pressed(XAVIX_CONTROLLER_PRIMARY))
			{
				g_xavix2_area_gesture_frame = 0;
				g_xavix2_area_gesture_frames = 8;
				g_xavix2_gesture_kind = XAVIX2_GESTURE_BLDJ_FIRST_ATTACK;
			}
			else if (g_rom.kind == DRGQST_ROM_BAN_DB2J ||
				g_rom.kind == DRGQST_ROM_BAN_DBZ)
			{
				g_xavix2_area_gesture_frame = 0;
				g_xavix2_area_gesture_frames = 8;
				g_xavix2_gesture_kind =
					g_rom.kind == DRGQST_ROM_BAN_DBZ ?
					XAVIX2_GESTURE_DBZ_SINGLE_CLOSE :
					XAVIX2_GESTURE_DB2J_EDGE;
			}
			else
			{
				g_naruto_execute_delay = 2;
				g_naruto_execute_frames = 4;
			}
		}
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_left_pulse_frames = 4;
	}
	if (controller_action_pressed(XAVIX_CONTROLLER_SECONDARY))
	{
		if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
			pulse_takecopter_tilt(TAKECOPTER_TILT_BACKWARD);
		else if (g_xavix2 && g_rom.kind == DRGQST_ROM_BAN_BLDJ)
		{
			g_xavix2_area_gesture_frame = 0;
			g_xavix2_area_gesture_frames = 8;
			g_xavix2_gesture_kind = XAVIX2_GESTURE_BLDJ_SECOND_ATTACK;
		}
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_right_pulse_frames = 4;
	}
	if ((controller_action_pressed(XAVIX_CONTROLLER_SPECIAL) ||
		controller_action_pressed(XAVIX_CONTROLLER_TWO_HAND)) && g_xavix2)
	{
		if (g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		{
			if (controller_action_pressed(XAVIX_CONTROLLER_SPECIAL))
				start_takecopter_boost();
			if (controller_action_pressed(XAVIX_CONTROLLER_TWO_HAND))
				pulse_takecopter_tilt(TAKECOPTER_TILT_RIGHT);
		}
		else if (g_rom.kind == DRGQST_ROM_BAN_DBZ ||
			g_rom.kind == DRGQST_ROM_BAN_BLDJ)
		{
			g_xavix2_area_gesture_frame = 0;
			g_xavix2_area_gesture_frames = 8;
			g_xavix2_gesture_kind =
				g_rom.kind == DRGQST_ROM_BAN_DBZ ?
				XAVIX2_GESTURE_DBZ_BOTH_CLOSE :
				XAVIX2_GESTURE_BLDJ_BOTH_ATTACK;
		}
	}
	if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
	{
		if (controller_action_pressed(XAVIX_CONTROLLER_DEFENSE))
			pulse_takecopter_tilt(TAKECOPTER_TILT_LEFT);
		if (controller_action_pressed(XAVIX_CONTROLLER_DEFLECT))
			pulse_takecopter_tilt(TAKECOPTER_TILT_NEUTRAL);
	}
	if ((controller_action_pressed(XAVIX_CONTROLLER_DEFLECT) ||
		controller_action_pressed(XAVIX_CONTROLLER_DEFENSE)) && g_xavix2 &&
		g_rom.kind == DRGQST_ROM_BAN_DBZ)
	{
		g_xavix2_area_gesture_frame = 0;
		g_xavix2_area_gesture_frames = 4;
		g_xavix2_gesture_kind = XAVIX2_GESTURE_DBZ_DEFLECT;
	}
	if (controller_action_pressed(XAVIX_CONTROLLER_SPECIAL) && g_core &&
		g_rom.kind == DRGQST_ROM_BAN_ONEP)
		drgqst_core_trigger_bazooka(g_core);
	if (controller_action_pressed(XAVIX_CONTROLLER_CONFIRM) && g_core &&
		g_rom.kind == DRGQST_ROM_EPO_HAMD)
		g_hamd_confirm_frames = 4;
	update_core_mouse();
}

static uint32_t epoch_takecopter_ir_code(void)
{
	int force_neutral = controller_action_held(XAVIX_CONTROLLER_DEFLECT);
	int up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0 ||
		(GetAsyncKeyState('W') & 0x8000) != 0 ||
		(GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 || g_left_button ||
		controller_action_held(XAVIX_CONTROLLER_PRIMARY) ||
		controller_action_held(XAVIX_CONTROLLER_CONFIRM);
	int down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
		(GetAsyncKeyState('S') & 0x8000) != 0 ||
		controller_action_held(XAVIX_CONTROLLER_SECONDARY);
	int left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 ||
		(GetAsyncKeyState('A') & 0x8000) != 0 ||
		controller_action_held(XAVIX_CONTROLLER_DEFENSE);
	int right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 ||
		(GetAsyncKeyState('D') & 0x8000) != 0 ||
		controller_action_held(XAVIX_CONTROLLER_TWO_HAND);

	/* The ROM recognises the acceleration event only after each side is stable
	 * for roughly ten reports, with no neutral report between the two sides. */
	if (g_takecopter_boost_phase)
	{
		up = down = left = right = 0;
		if (g_takecopter_boost_phase <= 12)
			down = 1;
		else if (g_takecopter_boost_phase <= 24)
			up = 1;
		else
			force_neutral = 1;
		if (++g_takecopter_boost_phase > 36)
			g_takecopter_boost_phase = 0;
	}
	else if (force_neutral)
		up = down = left = right = 0;
	else if (!up && !down && !left && !right &&
		g_takecopter_direction_frames)
	{
		switch (g_takecopter_direction_pulse)
		{
		case TAKECOPTER_TILT_FORWARD: up = 1; break;
		case TAKECOPTER_TILT_BACKWARD: down = 1; break;
		case TAKECOPTER_TILT_LEFT: left = 1; break;
		case TAKECOPTER_TILT_RIGHT: right = 1; break;
		default: force_neutral = 1; break;
		}
		--g_takecopter_direction_frames;
	}

	/* The head-mounted Take-copter reports a 4-by-4 tilt grid. Keyboard and
	 * mapped buttons take priority; mouse drag, direct analogue-stick, and Wii
	 * pointer positions share the same centred virtual tilt. */
	if (!force_neutral && !up && !down && !left && !right)
	{
		up = g_mouse_y < 0x60;
		down = g_mouse_y > 0x9f;
		left = g_mouse_x < 0x60;
		right = g_mouse_x > 0x9f;
	}
	return xavix_takecopter_ir_code(up, down, left, right);
}

static uint8_t xavix2_reflector_x(uint8_t x)
{
	return (uint8_t)(1 + ((unsigned)x * 54 + 127) / 255);
}

static uint8_t xavix2_reflector_y(uint8_t y)
{
	return (uint8_t)(55 - ((unsigned)y * 54 + 127) / 255);
}

static void run_xavix2_frame(void)
{
	uint8_t packet[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	uint32_t pio_input = 0;
	int dragon_ball_motion = g_rom.kind == DRGQST_ROM_BAN_DB2J ||
		g_rom.kind == DRGQST_ROM_BAN_DBZ;
	int controller_second = g_controller_reading.connected &&
		g_controller_reading.reflector[1].visible;
	int joined_hands = g_naruto_joined_hands ||
		controller_action_held(XAVIX_CONTROLLER_DEFENSE) ||
		controller_action_held(XAVIX_CONTROLLER_TWO_HAND);
	int bldj_gesture_active = g_rom.kind == DRGQST_ROM_BAN_BLDJ &&
		g_xavix2_area_gesture_frame < g_xavix2_area_gesture_frames;
	int bldj_needs_second = bldj_gesture_active &&
		(g_xavix2_gesture_kind == XAVIX2_GESTURE_BLDJ_SECOND_ATTACK ||
		 g_xavix2_gesture_kind == XAVIX2_GESTURE_BLDJ_BOTH_ATTACK);
	unsigned width;
	unsigned height;
	unsigned stride;
	uint8_t sample_x = xavix2_reflector_x(g_mouse_x);
	uint8_t sample_y = xavix2_reflector_y(g_mouse_y);
	uint8_t sample2_x = controller_second ? xavix2_reflector_x(
		g_controller_reading.reflector[1].x) : sample_x;
	uint8_t sample2_y = controller_second ? xavix2_reflector_y(
		g_controller_reading.reflector[1].y) : sample_y;
	if (g_rom.kind == DRGQST_ROM_BAN_DBZ)
	{
		/* Its tracker accepts only the calibrated camera rectangle.  Leave four
		 * raw units for the second blob so neither marker disappears at an edge. */
		if (sample_x < 0x0c) sample_x = 0x0c;
		if (sample_x > 0x30) sample_x = 0x30;
		if (sample_y < 0x12) sample_y = 0x12;
		if (sample_y > 0x2a) sample_y = 0x2a;
		if (sample2_x < 0x0c) sample2_x = 0x0c;
		if (sample2_x > 0x34) sample2_x = 0x34;
		if (sample2_y < 0x12) sample2_y = 0x12;
		if (sample2_y > 0x2e) sample2_y = 0x2e;
	}

	if (g_naruto_execute_delay && !dragon_ball_motion)
		--g_naruto_execute_delay;
	else if (!dragon_ball_motion &&
		(g_left_button ||
		 controller_action_held(XAVIX_CONTROLLER_PRIMARY) ||
		 controller_action_held(XAVIX_CONTROLLER_CONFIRM) ||
		 g_naruto_execute_frames))
	{
		pio_input = UINT32_C(1) << 16;
		if (g_naruto_execute_frames)
			--g_naruto_execute_frames;
	}

	packet[0] = sample_x;
	packet[1] = sample_y;
	packet[2] = dragon_ball_motion ? 0x28 : 0x20;
	if (g_right_button || joined_hands || dragon_ball_motion ||
		controller_second || bldj_needs_second ||
		controller_action_held(XAVIX_CONTROLLER_CONFIRM) ||
		controller_action_held(XAVIX_CONTROLLER_SPECIAL))
	{
		packet[3] = controller_second ? sample2_x : dragon_ball_motion ?
			(uint8_t)(sample_x <= 0x33 ? sample_x + 4 : sample_x - 4) :
			sample_x;
		/* Blue Dragon rejects two perfectly overlapping reflectors as one
		 * blob.  Its confirm gesture needs a small, still-visible separation.
		 * Naruto deliberately uses coincident hands for its guard gesture. */
		packet[4] = controller_second ? sample2_y :
			g_rom.kind == DRGQST_ROM_BAN_BLDJ ?
			(uint8_t)(sample_y <= 0x35 ? sample_y + 2 : sample_y - 2) :
			g_rom.kind == DRGQST_ROM_BAN_DBZ ?
			(uint8_t)(sample_y <= 0x33 ? sample_y + 4 : sample_y - 4) :
			sample_y;
		packet[5] = dragon_ball_motion ? 0x28 : 0x20;
	}
	if (bldj_gesture_active)
	{
		unsigned phase = g_xavix2_area_gesture_frame++;
		/* Blue Dragon exposes independent close/reopen events for its two
		 * reflectors (RAM $1130 bits 0 and 8).  Four absent samples followed
		 * by four restored samples reliably cross the firmware's history
		 * filter and are consumed by the battle action routines. */
		if (phase < 4 &&
			(g_xavix2_gesture_kind == XAVIX2_GESTURE_BLDJ_FIRST_ATTACK ||
			 g_xavix2_gesture_kind == XAVIX2_GESTURE_BLDJ_BOTH_ATTACK))
			packet[2] = 0;
		if (phase < 4 &&
			(g_xavix2_gesture_kind == XAVIX2_GESTURE_BLDJ_SECOND_ATTACK ||
			 g_xavix2_gesture_kind == XAVIX2_GESTURE_BLDJ_BOTH_ATTACK))
			packet[5] = 0;
		if (g_xavix2_area_gesture_frame == g_xavix2_area_gesture_frames)
			g_xavix2_gesture_kind = XAVIX2_GESTURE_NONE;
	}
	if (dragon_ball_motion && g_xavix2_area_gesture_frame <
		g_xavix2_area_gesture_frames)
	{
		unsigned phase = g_xavix2_area_gesture_frame++;
		/* The Dragon Bands report two reflector positions and areas.  Keep
		 * distinct gestures instead of collapsing every host button into the
		 * same packet: basic attack hides one hand, the two-hand primitive hides
		 * both, and the keyboard sweep advances both blobs eight raw units on
		 * each 30 Hz classifier update. */
		if (g_xavix2_gesture_kind == XAVIX2_GESTURE_DBZ_SINGLE_CLOSE)
		{
			packet[3] = packet[4] = packet[5] = 0;
		}
		else if (g_xavix2_gesture_kind == XAVIX2_GESTURE_DBZ_BOTH_CLOSE)
			memset(packet, 0, 6);
		else if (g_xavix2_gesture_kind == XAVIX2_GESTURE_DBZ_DEFLECT)
		{
			int delta = (int)(phase + 1) * 4;
			if (sample_x > 0x20)
				delta = -delta;
			packet[0] = (uint8_t)((int)packet[0] + delta);
			packet[3] = (uint8_t)((int)packet[3] + delta);
		}
		else
		{
			/* DB2J story arrows trigger on a reflector leaving and re-entering
			 * their hit box.  Move the pair aside for half the pulse, then let the
			 * user's target position return; retain the area transition needed by
			 * later gesture classifiers. */
			if (phase < 4)
			{
				int delta = sample_x <= 0x1b ? 12 : -12;
				packet[0] = (uint8_t)((int)packet[0] + delta);
				packet[3] = (uint8_t)((int)packet[3] + delta);
			}
			if (phase >= 2 && phase < 6)
				packet[2] = packet[5] = 0x08;
		}
		if (g_xavix2_area_gesture_frame == g_xavix2_area_gesture_frames)
			g_xavix2_gesture_kind = XAVIX2_GESTURE_NONE;
	}
	if (g_rom.kind == DRGQST_ROM_BAN_DB2J &&
		(g_left_button || controller_action_held(XAVIX_CONTROLLER_PRIMARY) ||
		 controller_action_held(XAVIX_CONTROLLER_CONFIRM)))
		pio_input |= UINT32_C(1) << 16;
	if (g_rom.kind == DRGQST_ROM_BAN_DB2J &&
		(g_right_button ||
		 controller_action_held(XAVIX_CONTROLLER_SECONDARY)))
		pio_input |= UINT32_C(1) << 19;
	if (g_rom.kind == DRGQST_ROM_EPO_DTCJ)
	{
		xavix2_machine_update_takecopter_timer_rate(g_xavix2);
		(void)xavix2_machine_transmit_epoch_ir(g_xavix2,
			epoch_takecopter_ir_code());
	}
	(void)xavix2_machine_run_video_frame(g_xavix2,
		xavix2_uses_motion_packet(g_rom.kind) ? packet : NULL, pio_input);
	win_audio_submit(&g_audio_output,
		xavix2_machine_frame_audio(g_xavix2),
		XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME);
	g_framebuffer = xavix2_machine_visible_frame(g_xavix2,
		&width, &height, &stride);
	g_frame_width = width;
	g_frame_height = height;
	g_frame_stride = stride;
	g_frame_pixel_scale = xavix2_machine_frame_scale(g_xavix2);
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
		xavix_video_recorder_active(&g_video_recorder) ?
		L"XaviXEmu REC | %.1f FPS | %.2f ms/frame | dropped %llu | guest %.1f M/s | IRQ %.1f/s | audio drop %llu / under %llu" :
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

static void release_recording_surface(void)
{
	if (g_recording_dc && g_recording_old_bitmap)
		SelectObject(g_recording_dc, g_recording_old_bitmap);
	if (g_recording_bitmap)
		DeleteObject(g_recording_bitmap);
	if (g_recording_dc)
		DeleteDC(g_recording_dc);
	g_recording_dc = NULL;
	g_recording_bitmap = NULL;
	g_recording_old_bitmap = NULL;
	g_recording_pixels = NULL;
	g_recording_width = 0;
	g_recording_height = 0;
}

static int create_recording_surface(HWND window, unsigned width,
	unsigned height, wchar_t *error, size_t error_length)
{
	BITMAPINFO bitmap_info;
	HDC device;
	HDC memory;
	HBITMAP bitmap;
	HGDIOBJ old_bitmap;
	void *pixels = NULL;

	if (!width || !height || width > INT_MAX || height > INT_MAX ||
		(uint64_t)width * height * 4 > SIZE_MAX)
	{
		_snwprintf(error, error_length,
			L"The recording display dimensions are invalid.");
		return 0;
	}
	device = GetDC(window);
	if (!device)
	{
		_snwprintf(error, error_length,
			L"The recording display is not available.");
		return 0;
	}
	memory = CreateCompatibleDC(device);
	memset(&bitmap_info, 0, sizeof(bitmap_info));
	bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
	bitmap_info.bmiHeader.biWidth = (LONG)width;
	bitmap_info.bmiHeader.biHeight = -(LONG)height;
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;
	bitmap = memory ? CreateDIBSection(device, &bitmap_info,
		DIB_RGB_COLORS, &pixels, NULL, 0) : NULL;
	ReleaseDC(window, device);
	if (!memory || !bitmap || !pixels)
	{
		if (bitmap) DeleteObject(bitmap);
		if (memory) DeleteDC(memory);
		_snwprintf(error, error_length,
			L"The recording display surface could not be created.");
		return 0;
	}
	old_bitmap = SelectObject(memory, bitmap);
	if (!old_bitmap || old_bitmap == HGDI_ERROR)
	{
		DeleteObject(bitmap);
		DeleteDC(memory);
		_snwprintf(error, error_length,
			L"The recording display surface could not be selected.");
		return 0;
	}
	release_recording_surface();
	g_recording_dc = memory;
	g_recording_bitmap = bitmap;
	g_recording_old_bitmap = old_bitmap;
	g_recording_pixels = (uint32_t *)pixels;
	g_recording_width = (int)width;
	g_recording_height = (int)height;
	return 1;
}

static int render_recording_frame(void)
{
	display_viewport viewport;
	RECT client;
	int scale_x;
	int scale_y;

	if (!g_recording_dc || !g_recording_pixels ||
		g_recording_width <= 0 || g_recording_height <= 0 ||
		!g_frame_width || !g_frame_height)
		return 0;
	SetRect(&client, 0, 0, g_recording_width, g_recording_height);
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = g_recording_width;
	viewport.height = g_recording_height;
	scale_x = g_recording_width / (int)logical_frame_width();
	scale_y = g_recording_height / (int)logical_frame_height();
	viewport.scale = g_stretch_4_3 ? scale_y :
		(scale_x < scale_y ? scale_x : scale_y);
	if (viewport.scale < 1)
		viewport.scale = 1;
	if (!render_display(g_recording_dc, &client, &viewport))
		return 0;
	GdiFlush();
	return 1;
}

static void stop_video_recording(HWND window, int show_result)
{
	wchar_t path[MAX_PATH];
	wchar_t error[384];
	wchar_t message[MAX_PATH + 96];

	if (!xavix_video_recorder_active(&g_video_recorder))
	{
		release_recording_surface();
		return;
	}
	if (!xavix_video_recorder_stop(&g_video_recorder, path,
		sizeof(path) / sizeof(path[0]), error,
		sizeof(error) / sizeof(error[0])))
	{
		release_recording_surface();
		if (show_result)
			MessageBoxW(window, error, L"Recording failed",
				MB_OK | MB_ICONERROR);
		return;
	}
	release_recording_surface();
	g_window_status = WINDOW_STATUS_RUNNING;
	update_window_title(window);
	if (show_result)
	{
		_snwprintf(message, sizeof(message) / sizeof(message[0]),
			L"Recording saved:\r\n\r\n%ls", path);
		message[sizeof(message) / sizeof(message[0]) - 1] = L'\0';
		MessageBoxW(window, message, L"Recording saved",
			MB_OK | MB_ICONINFORMATION);
	}
}

static void begin_exit_confirmation(HWND window)
{
	if (!emulator_loaded() || g_exit_confirmation)
		return;
	release_held_host_inputs(window);
	stop_frame_clock(window);
	win_audio_shutdown(&g_audio_output);
	g_exit_confirmation = 1;
	g_resume_enter_blocked = 0;
	InvalidateRect(window, NULL, FALSE);
}

static void resume_from_exit_confirmation(HWND window)
{
	if (!emulator_loaded() || !g_exit_confirmation)
		return;
	g_exit_confirmation = 0;
	g_resume_enter_blocked = 1;
	win_audio_init(&g_audio_output);
	win_audio_open(&g_audio_output);
	start_frame_clock(window);
	SetFocus(window);
	InvalidateRect(window, NULL, FALSE);
}

static void return_to_game_library(HWND window)
{
	if (!emulator_loaded())
		return;
	g_exit_confirmation = 0;
	g_resume_enter_blocked = 0;
	release_held_host_inputs(window);
	stop_frame_clock(window);
	stop_video_recording(window, 0);
	save_persistent_eeprom(window, 1);
	win_audio_shutdown(&g_audio_output);
	free(g_core);
	free(g_xavix2);
	g_core = NULL;
	g_xavix2 = NULL;
	drgqst_rom_release(&g_rom);
	xavix_controller_input_set_profile(&g_controller_input, NULL);
	xavix_controller_input_set_maximum_step(&g_controller_input, 12);
	memset(&g_controller_reading, 0, sizeof(g_controller_reading));
	g_framebuffer = g_idle_framebuffer;
	g_frame_width = FRAME_WIDTH;
	g_frame_height = FRAME_HEIGHT;
	g_frame_stride = FRAME_WIDTH;
	g_frame_pixel_scale = 1;
	g_window_status = WINDOW_STATUS_IDLE;
	EnableMenuItem(g_menu, ID_STATE_SAVE, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(g_menu, ID_STATE_LOAD, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(g_menu, ID_FILE_SCREENSHOT, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(g_menu, ID_VIEW_HIGH_RESOLUTION_3D,
		MF_BYCOMMAND | MF_GRAYED);
	update_xavix2_audio_channel_menu();
	if (g_fullscreen)
		toggle_fullscreen(window);
	set_game_library_visible(window, 1);
	update_window_title(window);
	DrawMenuBar(window);
	InvalidateRect(window, NULL, TRUE);
}

static void start_video_recording(HWND window)
{
	display_viewport viewport;
	unsigned width;
	unsigned height;
	wchar_t path[MAX_PATH];
	wchar_t error[384];

	if (!emulator_loaded() ||
		xavix_video_recorder_active(&g_video_recorder))
		return;
	viewport = calculate_viewport(window);
	width = g_record_window_size ? (unsigned)viewport.width : g_frame_width;
	height = g_record_window_size ? (unsigned)viewport.height : g_frame_height;
	if (g_recording_format == XAVIX_VIDEO_FORMAT_MP4)
	{
		/* H.264 4:2:0 encoders require complete two-pixel chroma blocks. */
		width &= ~1U;
		height &= ~1U;
	}
	if (!create_recording_surface(window, width, height, error,
		sizeof(error) / sizeof(error[0])))
	{
		MessageBoxW(window, error, L"Recording failed", MB_OK | MB_ICONERROR);
		return;
	}
	if (!xavix_video_recorder_start_format(&g_video_recorder,
		g_snap_directory, width, height, g_recording_format,
		path, sizeof(path) / sizeof(path[0]), error,
		sizeof(error) / sizeof(error[0])))
	{
		release_recording_surface();
		MessageBoxW(window, error, L"Recording failed", MB_OK | MB_ICONERROR);
		return;
	}
	update_window_title(window);
}

static void record_video_frame(HWND window, const int16_t *audio,
	size_t audio_frames)
{
	wchar_t error[384];
	wchar_t stop_error[384];
	wchar_t path[MAX_PATH];

	if (!xavix_video_recorder_active(&g_video_recorder))
		return;
	if (!render_recording_frame())
	{
		_snwprintf(error, sizeof(error) / sizeof(error[0]),
			L"The current display could not be rendered for recording.");
		goto failure;
	}
	if (xavix_video_recorder_write_frame(&g_video_recorder,
		g_recording_pixels, (unsigned)g_recording_width, audio, audio_frames,
		error, sizeof(error) / sizeof(error[0])))
		return;

failure:
	(void)xavix_video_recorder_stop(&g_video_recorder, path,
		sizeof(path) / sizeof(path[0]), stop_error,
		sizeof(stop_error) / sizeof(stop_error[0]));
	release_recording_surface();
	g_window_status = WINDOW_STATUS_RUNNING;
	update_window_title(window);
	MessageBoxW(window, error, L"Recording stopped", MB_OK | MB_ICONERROR);
}

static void run_due_frames(HWND window)
{
	LARGE_INTEGER now;
	LARGE_INTEGER frame_start;
	LARGE_INTEGER frame_end;
	unsigned frames = 0;

	if (!emulator_loaded() || g_exit_confirmation)
		return;
	QueryPerformanceCounter(&now);
	while (now.QuadPart >= g_next_frame_counter && frames < 3)
	{
		QueryPerformanceCounter(&frame_start);
		update_controller_host_input();
		if (g_xavix2)
		{
			/* Later frames in the same callback are invisible catch-up work.
			 * Keep CPU, input and audio at 60 Hz but do not rasterize another
			 * heavily overdrawn DBZ frame that will never be presented. */
			xavix2_machine_set_skip_render(g_xavix2, frames != 0);
			run_xavix2_frame();
			record_video_frame(window,
				xavix2_machine_frame_audio(g_xavix2),
				XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME);
			poll_persistent_eeprom(window);
		}
		else
		{
			update_hamd_input();
			update_tvpc_keyboard();
			update_digital_direction_input();
			update_early_motion_input();
			update_sdb_input();
			update_epo_bowl_input();
			update_tak_chq_input();
			update_core_mouse();
			g_framebuffer = drgqst_core_run_frame(g_core);
			advance_ttv_special_gesture();
			advance_ban_onep_menu_input();
			update_cursor_presentation();
			win_audio_submit(&g_audio_output,
				drgqst_core_frame_audio(g_core),
				DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME);
			record_video_frame(window, drgqst_core_frame_audio(g_core),
				DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME);
			poll_persistent_eeprom(window);
		}
		QueryPerformanceCounter(&frame_end);
		g_timing_core_counter += frame_end.QuadPart - frame_start.QuadPart;
		g_timing_frames++;
		g_next_frame_counter += g_frame_counter_step;
		/* A costly frame can cross the following deadline.  Recheck against
		 * its completion time inside this callback so catch-up frames are
		 * emulated together and only the final result is presented.  Using the
		 * stale entry timestamp deferred catch-up to later WM_TIMER messages,
		 * exposing alternating slow/fast frames and bursty audio submission. */
		now = frame_end;
		++frames;
	}
	if (g_xavix2)
		xavix2_machine_set_skip_render(g_xavix2, 0);
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

	if (!emulator_loaded())
		return 0;
	state_size = g_xavix2 ? xavix2_machine_state_size() :
		drgqst_state_serialized_size();
	state = (uint8_t *)malloc(state_size);
	if (!state)
	{
		MessageBoxW(window, text->state_save_memory_error,
			text->state_save_title, MB_OK | MB_ICONERROR);
		return 0;
	}
	error[0] = L'\0';
	encoded = (g_xavix2 ?
		xavix2_machine_state_save(g_xavix2, state, state_size, &written) :
		drgqst_state_save(g_core, state, state_size, &written)) &&
		written == state_size;
	success = encoded && drgqst_persistence_save(
			g_save_directory,
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

	if (!emulator_loaded())
		return 0;
	state_size = g_xavix2 ? xavix2_machine_state_size() :
		drgqst_state_serialized_size();
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
	if (g_xavix2)
	{
		unsigned width;
		unsigned height;
		unsigned stride;

		/* A runtime snapshot may contain an older copy of the serial EEPROM.
		 * Keep the game's durable 24C04 image authoritative across F7. */
		if (eeprom_size)
		{
			save_persistent_eeprom(window, 1);
			eeprom = &g_xavix2->eeprom;
			xavix_eeprom_copy_image(eeprom, eeprom_image, eeprom_size);
			eeprom_generation = eeprom->write_generation;
		}
		success = xavix2_machine_state_load(g_xavix2, state, loaded);
		if (!success)
		{
			free(state);
			MessageBoxW(window, text->state_incompatible_error,
				text->state_load_title, MB_OK | MB_ICONERROR);
			return 0;
		}
		if (eeprom_size)
		{
			eeprom = &g_xavix2->eeprom;
			memcpy(eeprom->data, eeprom_image, eeprom_size);
			eeprom->dirty = 0;
			eeprom->write_generation = eeprom_generation;
			g_eeprom_generation = eeprom_generation;
			g_eeprom_settle_frames = 0;
		}
		if (loaded_from_legacy &&
			!drgqst_persistence_save(g_save_directory,
				persistence_kind_for_rom(DRGQST_PERSISTENCE_RUNTIME_STATE,
					g_rom.kind),
				rom_sha1_for_kind(g_rom.kind), state, loaded,
				error, sizeof(error) / sizeof(error[0])))
			MessageBoxW(window, text->state_save_error,
				text->state_save_title, MB_OK | MB_ICONERROR);
		free(state);
		g_naruto_joined_hands = 0;
		g_naruto_execute_delay = 0;
		g_naruto_execute_frames = 0;
		g_xavix2_area_gesture_frame = 0;
		g_xavix2_area_gesture_frames = 0;
		g_xavix2_gesture_kind = 0;
		g_takecopter_direction_pulse = TAKECOPTER_TILT_NEUTRAL;
		g_takecopter_direction_frames = 0;
		g_takecopter_boost_phase = 0;
		if (g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		{
			g_takecopter_mouse_drag_valid = 0;
			g_mouse_x = 0x80;
			g_mouse_y = 0x80;
		}
		win_audio_shutdown(&g_audio_output);
		win_audio_init(&g_audio_output);
		win_audio_open(&g_audio_output);
		g_framebuffer = xavix2_machine_visible_frame(g_xavix2,
			&width, &height, &stride);
		g_frame_width = width;
		g_frame_height = height;
		g_frame_stride = stride;
		g_frame_pixel_scale = xavix2_machine_frame_scale(g_xavix2);
		drgqst_cursor_presentation_reset(&g_cursor_presentation);
		update_xavix2_audio_channel_menu();
		start_frame_clock(window);
		g_window_status = WINDOW_STATUS_STATE_LOADED;
		update_window_title(window);
		InvalidateRect(window, NULL, FALSE);
		return 1;
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
		!drgqst_persistence_save(g_save_directory,
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
	g_xavix2_area_gesture_frame = 0;
	g_xavix2_area_gesture_frames = 0;
	g_xavix2_gesture_kind = 0;
	if (rom_uses_tvpc_host_input(g_rom.kind))
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
	xavix2_machine_set_motion_packet_address(machine,
		xavix2_motion_packet_address(image->kind));
	xavix2_machine_set_fixed_pio_input(machine,
		xavix2_fixed_pio_input(image->kind));
	/* Take-copter uses one IRQ-7 tick per 60 Hz video frame in every
	 * display mode; synchronize old F5 states immediately. */
	if (image->kind == DRGQST_ROM_EPO_DTCJ)
		xavix2_machine_update_takecopter_timer_rate(machine);
	load_persistent_xavix2_eeprom(window, machine, image->kind);

	stop_video_recording(window, 1);
	stop_frame_clock(window);
	save_persistent_eeprom(window, 1);
	free(g_core);
	free(g_xavix2);
	drgqst_rom_release(&g_rom);
	g_core = NULL;
	g_xavix2 = machine;
	g_xavix2_audio_mute_mask = 0;
	xavix2_audio_set_mute_mask(&g_xavix2->audio,
		g_xavix2_audio_mute_mask);
	xavix2_machine_set_high_resolution_3d(g_xavix2,
		g_high_resolution_3d);
	g_rom = *image;
	memset(image, 0, sizeof(*image));
	xavix_controller_input_set_profile(&g_controller_input,
		drgqst_rom_short_name(g_rom.kind));
	xavix_controller_input_set_maximum_step(&g_controller_input, 12);
	g_eeprom_generation = g_xavix2->eeprom.write_generation;
	g_eeprom_settle_frames = 0;
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
	g_xavix2_area_gesture_frame = 0;
	g_xavix2_area_gesture_frames = 0;
	g_xavix2_gesture_kind = 0;
	g_takecopter_mouse_drag_valid = 0;
	g_takecopter_direction_pulse = TAKECOPTER_TILT_NEUTRAL;
	g_takecopter_direction_frames = 0;
	g_takecopter_boost_phase = 0;
	g_mouse_x = 0x80;
	g_mouse_y = 0x80;
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
	EnableMenuItem(g_menu, ID_STATE_SAVE, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_STATE_LOAD, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_FILE_SCREENSHOT,
		MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_VIEW_HIGH_RESOLUTION_3D,
		MF_BYCOMMAND | MF_ENABLED);
	DrawMenuBar(window);
	update_xavix2_audio_channel_menu();
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
		if (result)
		{
			set_game_library_visible(window, 0);
			/* The scale selection is persisted when its menu item is chosen.
			 * A library-first launch used to keep the library's 1080x720 outer
			 * window, so the saved 4x setting appeared to shrink after every
			 * restart.  Reapply it only after the ROM has established its actual
			 * logical frame dimensions. */
			resize_for_scale(window, g_window_scale);
		}
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

	stop_video_recording(window, 1);
	stop_frame_clock(window);
	save_persistent_eeprom(window, 1);
	free(g_core);
	free(g_xavix2);
	drgqst_rom_release(&g_rom);
	g_core = core;
	g_xavix2 = NULL;
	g_rom = image;
	xavix_controller_input_set_profile(&g_controller_input,
		drgqst_rom_short_name(g_rom.kind));
	/* Dragon Quest rejects slow optical paths before they reach its direction
	 * classifier.  Its verified 12-frame calibration stroke advances about
	 * 17 host units per frame, beyond the ordinary virtual-cursor limit. */
	xavix_controller_input_set_maximum_step(&g_controller_input,
		g_rom.kind == DRGQST_ROM_DRAGON_QUEST ? 24 : 12);
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
	g_xavix2_area_gesture_frame = 0;
	g_xavix2_area_gesture_frames = 0;
	g_xavix2_gesture_kind = 0;
	g_takecopter_mouse_drag_valid = 0;
	g_takecopter_direction_pulse = TAKECOPTER_TILT_NEUTRAL;
	g_takecopter_direction_frames = 0;
	g_takecopter_boost_phase = 0;
	g_mouse_x = 0x80;
	g_mouse_y = 0x80;
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
	update_controller_host_input();
	update_digital_direction_input();
	update_early_motion_input();
	update_sdb_input();
	update_epo_bowl_input();
	update_core_mouse();
	g_framebuffer = drgqst_core_run_frame(g_core);
	g_frame_width = FRAME_WIDTH;
	g_frame_height = FRAME_HEIGHT;
	g_frame_stride = FRAME_WIDTH;
	g_frame_pixel_scale = 1;
	update_cursor_presentation();
	win_audio_submit(&g_audio_output, drgqst_core_frame_audio(g_core),
		DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME);
	g_window_status = WINDOW_STATUS_RUNNING;
	update_window_title(window);
	EnableMenuItem(g_menu, ID_STATE_SAVE, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_STATE_LOAD, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_FILE_SCREENSHOT,
		MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(g_menu, ID_VIEW_HIGH_RESOLUTION_3D,
		MF_BYCOMMAND | MF_GRAYED);
	DrawMenuBar(window);
	update_xavix2_audio_channel_menu();
	start_frame_clock(window);
	set_game_library_visible(window, 0);
	resize_for_scale(window, g_window_scale);
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

	/* XaviX2 titles provide their own in-game cursors.  In particular,
	 * Naruto no longer needs the host's blue diagnostic marker, and
	 * Take-copter's head-tilt input is not a screen-position cursor. */
	if (!g_core || rom_has_internal_cursor(g_rom.kind) ||
		g_rom.kind == DRGQST_ROM_EPO_HAMD ||
		rom_uses_tvpc_host_input(g_rom.kind) ||
		g_rom.kind == DRGQST_ROM_TAK_CHQ ||
		g_rom.kind == DRGQST_ROM_EPO_ES2J ||
		g_rom.kind == DRGQST_ROM_EPO_HAMC ||
		g_rom.kind == DRGQST_ROM_TOM_DPGM ||
		g_rom.kind == DRGQST_ROM_EPO_MINI ||
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

static void draw_exit_confirmation(HDC device, const RECT *client)
{
	RECT panel;
	RECT title_rectangle;
	RECT instruction_rectangle;
	HBRUSH background;
	HBRUSH border;
	HFONT title_font;
	HFONT instruction_font;
	HGDIOBJ previous_font;
	int client_width = client->right - client->left;
	int client_height = client->bottom - client->top;
	int panel_width = client_width - 32;
	int panel_height = 150;
	const wchar_t *title = localized_text(L"遊戲已暫停",
		L"ゲームを一時停止しました", L"Jeu en pause", L"Game paused");
	const wchar_t *instruction = localized_text(
		L"再按一次 ESC：退出遊戲並回到選單\r\n按 ENTER：繼續遊戲",
		L"もう一度 ESC：ゲームを終了してメニューへ戻る\r\nENTER：ゲームを続ける",
		L"ESC à nouveau : quitter et revenir au menu\r\nENTER : continuer le jeu",
		L"Press ESC again: quit to the game library\r\nPress ENTER: continue playing");

	if (panel_width > 640) panel_width = 640;
	if (panel_height > client_height - 24) panel_height = client_height - 24;
	if (panel_width < 160 || panel_height < 80)
		return;
	panel.left = client->left + (client_width - panel_width) / 2;
	panel.top = client->top + (client_height - panel_height) / 2;
	panel.right = panel.left + panel_width;
	panel.bottom = panel.top + panel_height;
	background = CreateSolidBrush(RGB(12, 24, 38));
	border = CreateSolidBrush(RGB(92, 176, 255));
	FillRect(device, &panel, background);
	FrameRect(device, &panel, border);
	InflateRect(&panel, -1, -1);
	FrameRect(device, &panel, border);
	DeleteObject(background);
	DeleteObject(border);

	SetBkMode(device, TRANSPARENT);
	SetTextColor(device, RGB(255, 255, 255));
	title_font = CreateFontW(-MulDiv(22, GetDeviceCaps(device, LOGPIXELSY), 72),
		0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	instruction_font = CreateFontW(
		-MulDiv(13, GetDeviceCaps(device, LOGPIXELSY), 72),
		0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	title_rectangle = panel;
	title_rectangle.top += 14;
	title_rectangle.bottom = title_rectangle.top + 42;
	instruction_rectangle = panel;
	instruction_rectangle.left += 12;
	instruction_rectangle.right -= 12;
	instruction_rectangle.top = title_rectangle.bottom + 2;
	instruction_rectangle.bottom -= 10;
	previous_font = SelectObject(device, title_font ? title_font :
		GetStockObject(DEFAULT_GUI_FONT));
	DrawTextW(device, title, -1, &title_rectangle,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SelectObject(device, instruction_font ? instruction_font :
		GetStockObject(DEFAULT_GUI_FONT));
	DrawTextW(device, instruction, -1, &instruction_rectangle,
		DT_CENTER | DT_VCENTER | DT_WORDBREAK);
	SelectObject(device, previous_font);
	if (title_font) DeleteObject(title_font);
	if (instruction_font) DeleteObject(instruction_font);
}

static void paint_window(HWND window)
{
	PAINTSTRUCT paint;
	HDC device = BeginPaint(window, &paint);
	RECT client;
	display_viewport viewport = calculate_viewport(window);

	GetClientRect(window, &client);
	(void)render_display(device, &client, &viewport);
	if (g_exit_confirmation)
		draw_exit_confirmation(device, &client);
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

static HBITMAP create_library_placeholder(const wchar_t *title)
{
	BITMAPINFO info;
	HBITMAP bitmap;
	void *pixels = NULL;
	HDC dc;
	HGDIOBJ previous;
	RECT rectangle = { 0, 0, 160, 120 };
	uint32_t *pixel;
	unsigned i;

	memset(&info, 0, sizeof(info));
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = 160;
	info.bmiHeader.biHeight = -120;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
	if (!bitmap || !pixels)
		return bitmap;
	pixel = (uint32_t *)pixels;
	for (i = 0; i < 160U * 120U; ++i)
		pixel[i] = (i / 160U < 8 || i / 160U >= 112) ?
			UINT32_C(0x003f5f7f) : UINT32_C(0x001a2530);
	dc = CreateCompatibleDC(NULL);
	if (!dc)
		return bitmap;
	previous = SelectObject(dc, bitmap);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(232, 240, 248));
	SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
	InflateRect(&rectangle, -8, -8);
	DrawTextW(dc, title, -1, &rectangle,
		DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
	SelectObject(dc, previous);
	DeleteDC(dc);
	return bitmap;
}

static HBITMAP load_library_thumbnail(const wchar_t *path,
	const wchar_t *fallback_title)
{
	IWICImagingFactory *factory = NULL;
	IWICBitmapDecoder *decoder = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICBitmapScaler *scaler = NULL;
	IWICFormatConverter *converter = NULL;
	BITMAPINFO info;
	HBITMAP bitmap = NULL;
	void *pixels = NULL;
	HRESULT result;

	if (!path || !path[0])
		return create_library_placeholder(fallback_title);
	result = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
		CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateDecoderFromFilename(factory, path,
			NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
	if (SUCCEEDED(result))
		result = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
	if (SUCCEEDED(result))
		result = IWICBitmapScaler_Initialize(scaler,
			(IWICBitmapSource *)frame, 160, 120,
			WICBitmapInterpolationModeFant);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateFormatConverter(factory, &converter);
	if (SUCCEEDED(result))
		result = IWICFormatConverter_Initialize(converter,
			(IWICBitmapSource *)scaler, &GUID_WICPixelFormat32bppBGR,
			WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
	if (SUCCEEDED(result))
	{
		memset(&info, 0, sizeof(info));
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = 160;
		info.bmiHeader.biHeight = -120;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS,
			&pixels, NULL, 0);
		if (!bitmap || !pixels || FAILED(IWICFormatConverter_CopyPixels(
			converter, NULL, 160 * 4, 160 * 120 * 4, (BYTE *)pixels)))
		{
			if (bitmap) DeleteObject(bitmap);
			bitmap = NULL;
		}
	}
	if (converter) IWICFormatConverter_Release(converter);
	if (scaler) IWICBitmapScaler_Release(scaler);
	if (frame) IWICBitmapFrameDecode_Release(frame);
	if (decoder) IWICBitmapDecoder_Release(decoder);
	if (factory) IWICImagingFactory_Release(factory);
	return bitmap ? bitmap : create_library_placeholder(fallback_title);
}

static const wchar_t *game_library_status_name(
	enum xavix_game_support_status status)
{
	switch (status)
	{
	case XAVIX_GAME_SUPPORT_FULLY_PLAYABLE:
		return localized_text(L"完整可玩", L"完全プレイ可能",
			L"Entièrement jouable", L"Fully playable");
	case XAVIX_GAME_SUPPORT_PLAYABLE:
		return localized_text(L"可玩（有已知問題）", L"プレイ可能（既知の問題あり）",
			L"Jouable (problèmes connus)", L"Playable (known issues)");
	case XAVIX_GAME_SUPPORT_NOT_WORKING:
		return localized_text(L"尚未支援", L"未対応",
			L"Non pris en charge", L"Not working");
	case XAVIX_GAME_SUPPORT_INITIAL:
	default:
		return localized_text(L"初步支援", L"暫定対応",
			L"Prise en charge initiale", L"Initial support");
	}
}

static void update_game_library_labels(void)
{
	if (g_game_library_view)
	{
		LVCOLUMNW column;
		const wchar_t *titles[6];
		titles[0] = localized_text(L"遊戲名稱", L"ゲーム名",
			L"Titre du jeu", L"Game title");
		titles[1] = localized_text(L"支援狀態", L"対応状況",
			L"État", L"Support status");
		titles[2] = localized_text(L"發售年份", L"発売年",
			L"Sortie", L"Release");
		titles[3] = localized_text(L"主機類型", L"ハードウェア",
			L"Plate-forme", L"Platform");
		titles[4] = localized_text(L"廠商", L"メーカー",
			L"Éditeur", L"Maker");
		titles[5] = localized_text(L"ROM 檔案", L"ROMファイル",
			L"Fichier ROM", L"ROM file");
		memset(&column, 0, sizeof(column));
		column.mask = LVCF_TEXT;
		for (column.iSubItem = 0; column.iSubItem < 6; ++column.iSubItem)
		{
			column.pszText = (wchar_t *)titles[column.iSubItem];
			ListView_SetColumn(g_game_library_view, column.iSubItem, &column);
		}
	}
	if (g_game_library_choose_button)
		SetWindowTextW(g_game_library_choose_button,
			localized_text(L"選擇 ROM 資料夾...", L"ROMフォルダーを選択...",
				L"Choisir le dossier ROM...", L"Choose ROM folder..."));
	if (g_game_library_refresh_button)
		SetWindowTextW(g_game_library_refresh_button,
			localized_text(L"重新掃描", L"再スキャン",
				L"Réanalyser", L"Refresh"));
}

static void layout_game_library(HWND window)
{
	RECT client;
	int width;
	int height;
	if (!g_game_library_view)
		return;
	GetClientRect(window, &client);
	width = client.right - client.left;
	height = client.bottom - client.top;
	MoveWindow(g_game_library_choose_button, 12, 10, 180, 30, TRUE);
	MoveWindow(g_game_library_refresh_button, 202, 10, 110, 30, TRUE);
	MoveWindow(g_game_library_view, 12, 50,
		width > 24 ? width - 24 : 1, height > 62 ? height - 62 : 1, TRUE);
}

static void populate_game_library_view(void)
{
	size_t i;
	if (!g_game_library_view)
		return;
	ListView_DeleteAllItems(g_game_library_view);
	ListView_SetImageList(g_game_library_view, NULL, LVSIL_SMALL);
	if (g_game_library_images)
		ImageList_Destroy(g_game_library_images);
	g_game_library_images = ImageList_Create(160, 120,
		ILC_COLOR32, (int)g_game_library.count + 1, 8);
	if (g_game_library_images)
		ImageList_SetBkColor(g_game_library_images, CLR_NONE);
	for (i = 0; i < g_game_library.count; ++i)
	{
		xavix_game_library_entry *entry = &g_game_library.entries[i];
		LVITEMW item;
		HBITMAP thumbnail = load_library_thumbnail(entry->thumbnail,
			entry->metadata->title);
		int image_index = -1;
		if (g_game_library_images && thumbnail)
			image_index = ImageList_Add(g_game_library_images, thumbnail, NULL);
		if (thumbnail) DeleteObject(thumbnail);
		memset(&item, 0, sizeof(item));
		item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
		item.iItem = (int)i;
		item.pszText = (wchar_t *)entry->metadata->title;
		item.iImage = image_index;
		item.lParam = (LPARAM)i;
		ListView_InsertItem(g_game_library_view, &item);
		ListView_SetItemText(g_game_library_view, (int)i, 1,
			(wchar_t *)game_library_status_name(
				xavix_game_support_status_for_kind(entry->metadata->kind)));
		ListView_SetItemText(g_game_library_view, (int)i, 2,
			(wchar_t *)entry->metadata->release);
		ListView_SetItemText(g_game_library_view, (int)i, 3,
			(wchar_t *)xavix_game_platform_name(entry->metadata->platform));
		ListView_SetItemText(g_game_library_view, (int)i, 4,
			(wchar_t *)entry->metadata->maker);
		ListView_SetItemText(g_game_library_view, (int)i, 5,
			entry->file_name);
	}
	if (g_game_library_images)
		ListView_SetImageList(g_game_library_view,
			g_game_library_images, LVSIL_SMALL);
}

static int refresh_game_library(HWND window, int show_error)
{
	wchar_t error[384];
	if (!g_rom_directory[0])
	{
		memset(&g_game_library, 0, sizeof(g_game_library));
		populate_game_library_view();
		return 0;
	}
	if (!xavix_game_library_scan(g_rom_directory, g_snap_directory,
		&g_game_library, error, sizeof(error) / sizeof(error[0])))
	{
		if (show_error)
			MessageBoxW(window, error,
				localized_text(L"遊戲清單", L"ゲーム一覧",
					L"Bibliothèque de jeux", L"Game library"),
				MB_OK | MB_ICONERROR);
		return 0;
	}
	xavix_game_library_sort_entries(&g_game_library, g_game_library_sort);
	populate_game_library_view();
	return 1;
}

static void set_game_library_visible(HWND window, int visible)
{
	g_game_library_visible = visible != 0;
	if (!g_game_library_view)
		return;
	update_game_library_labels();
	ShowWindow(g_game_library_choose_button,
		g_game_library_visible ? SW_SHOW : SW_HIDE);
	ShowWindow(g_game_library_refresh_button,
		g_game_library_visible ? SW_SHOW : SW_HIDE);
	ShowWindow(g_game_library_view,
		g_game_library_visible ? SW_SHOW : SW_HIDE);
	if (g_game_library_visible)
	{
		layout_game_library(window);
		if (!g_game_library.count && g_rom_directory[0])
			refresh_game_library(window, 1);
		SetCursor(LoadCursorW(NULL, IDC_ARROW));
	}
	InvalidateRect(window, NULL, TRUE);
}

static int CALLBACK browse_rom_directory_callback(HWND dialog, UINT message,
	LPARAM parameter, LPARAM data)
{
	(void)parameter;
	if (message == BFFM_INITIALIZED && data)
		SendMessageW(dialog, BFFM_SETSELECTIONW, TRUE, data);
	return 0;
}

static void choose_rom_directory(HWND window)
{
	BROWSEINFOW browse;
	LPITEMIDLIST selection;
	LPITEMIDLIST computer = NULL;
	wchar_t directory[MAX_PATH];
	memset(&browse, 0, sizeof(browse));
	browse.hwndOwner = window;
	/* Start at This PC rather than Desktop so every mounted drive is visible. */
	if (FAILED(SHGetSpecialFolderLocation(window, CSIDL_DRIVES, &computer)))
		computer = NULL;
	browse.pidlRoot = computer;
	browse.lpszTitle = localized_text(L"選擇存放 ROM ZIP 的資料夾（包含子目錄）",
		L"ROM ZIPを保存するフォルダーを選択（サブフォルダーを含む）",
		L"Choisissez le dossier ROM ZIP (sous-dossiers inclus)",
		L"Choose the ROM ZIP folder (including subfolders)");
	browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
	browse.lpfn = browse_rom_directory_callback;
	browse.lParam = g_rom_directory[0] ? (LPARAM)g_rom_directory : 0;
	selection = SHBrowseForFolderW(&browse);
	if (!selection)
	{
		if (computer) CoTaskMemFree(computer);
		return;
	}
	if (SHGetPathFromIDListW(selection, directory))
	{
		wcsncpy(g_rom_directory, directory,
			sizeof(g_rom_directory) / sizeof(g_rom_directory[0]) - 1);
		g_rom_directory[sizeof(g_rom_directory) /
			sizeof(g_rom_directory[0]) - 1] = L'\0';
		write_ini_string(L"Library", L"RomDirectory", g_rom_directory);
		refresh_game_library(window, 1);
		set_game_library_visible(window, 1);
	}
	CoTaskMemFree(selection);
	if (computer) CoTaskMemFree(computer);
}
static void initialize_game_library_controls(HWND window)
{
	LVCOLUMNW column;
	HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	HINSTANCE instance = (HINSTANCE)GetWindowLongPtrW(window, GWLP_HINSTANCE);
	static const int widths[] = { 390, 118, 82, 105, 160, 150 };
	static const wchar_t *const titles[] = {
		L"Game title", L"Support status", L"Release", L"Platform",
		L"Maker", L"ROM file"
	};
	unsigned i;

	g_game_library_choose_button = CreateWindowExW(0, L"BUTTON", L"",
		WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, window,
		(HMENU)(INT_PTR)ID_LIBRARY_CHOOSE_DIRECTORY, instance, NULL);
	g_game_library_refresh_button = CreateWindowExW(0, L"BUTTON", L"",
		WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, window,
		(HMENU)(INT_PTR)ID_LIBRARY_REFRESH, instance, NULL);
	g_game_library_view = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
		WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
		LVS_SHOWSELALWAYS, 0, 0, 0, 0, window, NULL, instance, NULL);
	SendMessageW(g_game_library_choose_button, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(g_game_library_refresh_button, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(g_game_library_view, WM_SETFONT, (WPARAM)font, TRUE);
	ListView_SetExtendedListViewStyle(g_game_library_view,
		LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
	{
		memset(&column, 0, sizeof(column));
		column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		column.pszText = (wchar_t *)titles[i];
		column.cx = widths[i];
		column.iSubItem = (int)i;
		ListView_InsertColumn(g_game_library_view, (int)i, &column);
	}
	update_game_library_labels();
	refresh_game_library(window, 0);
	set_game_library_visible(window, 1);
}

static xavix_game_library_entry *selected_library_game(void)
{
	int selected;
	if (!g_game_library_view)
		return NULL;
	selected = ListView_GetNextItem(g_game_library_view, -1, LVNI_SELECTED);
	return selected >= 0 && (size_t)selected < g_game_library.count ?
		&g_game_library.entries[selected] : NULL;
}

static void open_selected_library_game(HWND window)
{
	xavix_game_library_entry *entry = selected_library_game();
	if (entry)
		load_rom(window, entry->path, 1);
}

static void configure_selected_library_game(HWND window)
{
	xavix_game_library_entry *entry = selected_library_game();
	if (entry && xavix_game_control_profile_for_kind(entry->metadata->kind))
		show_controller_settings_for_game(window, entry->metadata->kind,
			entry->metadata->title);
}

static void show_game_library_context_menu(HWND window)
{
	POINT screen;
	LVHITTESTINFO hit;
	HMENU menu;
	UINT command;
	if (!g_game_library_view || !GetCursorPos(&screen))
		return;
	memset(&hit, 0, sizeof(hit));
	hit.pt = screen;
	ScreenToClient(g_game_library_view, &hit.pt);
	if (ListView_HitTest(g_game_library_view, &hit) < 0)
		return;
	ListView_SetItemState(g_game_library_view, hit.iItem,
		LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	menu = CreatePopupMenu();
	if (!menu)
		return;
	AppendMenuW(menu, MF_STRING, ID_LIBRARY_OPEN_GAME,
		localized_text(L"開始遊戲", L"ゲームを開始",
			L"Lancer le jeu", L"Start game"));
	if (xavix_game_control_profile_for_kind(
		g_game_library.entries[hit.iItem].metadata->kind))
		AppendMenuW(menu, MF_STRING, ID_LIBRARY_CONTROLLER_SETTINGS,
			localized_text(L"這款遊戲的專屬控制設定...", L"このゲーム専用の操作設定...",
				L"Commandes propres à ce jeu...", L"Game-specific control settings..."));
	command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
		screen.x, screen.y, 0, window, NULL);
	DestroyMenu(menu);
	if (command == ID_LIBRARY_OPEN_GAME)
		open_selected_library_game(window);
	else if (command == ID_LIBRARY_CONTROLLER_SETTINGS)
		configure_selected_library_game(window);
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
	wchar_t prefix[64];
	wchar_t saved_path[MAX_PATH];
	const char *short_name;
	size_t prefix_length;

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
	short_name = drgqst_rom_short_name(g_rom.kind);
	for (prefix_length = 0; short_name[prefix_length] &&
		prefix_length + 1 < sizeof(prefix) / sizeof(prefix[0]); ++prefix_length)
		prefix[prefix_length] = (wchar_t)(unsigned char)short_name[prefix_length];
	prefix[prefix_length] = L'\0';
	if (!xavix_screenshot_save_png_named(g_capture_dc, &source_rectangle,
		g_executable_directory, prefix, saved_path,
		sizeof(saved_path) / sizeof(saved_path[0]), error,
		sizeof(error) / sizeof(error[0])))
		goto failure;
	if (g_game_library.count)
	{
		size_t entry_index;
		for (entry_index = 0; entry_index < g_game_library.count; ++entry_index)
		{
			xavix_game_library_entry *entry =
				&g_game_library.entries[entry_index];
			if (entry->metadata->kind == g_rom.kind && !entry->thumbnail[0])
			{
				wcsncpy(entry->thumbnail, saved_path, MAX_PATH - 1);
				entry->thumbnail[MAX_PATH - 1] = L'\0';
			}
		}
		if (g_game_library_visible)
			populate_game_library_view();
	}
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
	if (g_exit_confirmation &&
		((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
		 message == WM_COMMAND))
		return 0;
	switch (message)
	{
	case WM_CREATE:
		initialize_game_library_controls(window);
		return 0;

	case WM_SIZE:
		if (g_game_library_visible)
			layout_game_library(window);
		break;

	case WM_NOTIFY:
		if (((NMHDR *)lparam)->hwndFrom == g_game_library_view)
		{
			NMHDR *header = (NMHDR *)lparam;
			if (header->code == NM_CUSTOMDRAW)
			{
				NMLVCUSTOMDRAW *draw = (NMLVCUSTOMDRAW *)lparam;
				if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
					return CDRF_NOTIFYITEMDRAW;
				if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT &&
					draw->nmcd.dwItemSpec < g_game_library.count)
				{
					enum xavix_game_support_status status =
						xavix_game_support_status_for_kind(
							g_game_library.entries[draw->nmcd.dwItemSpec].metadata->kind);
					draw->clrText =
						(status == XAVIX_GAME_SUPPORT_FULLY_PLAYABLE ||
						 status == XAVIX_GAME_SUPPORT_PLAYABLE) ?
						RGB(0, 128, 0) : status == XAVIX_GAME_SUPPORT_INITIAL ?
						RGB(184, 134, 11) : RGB(178, 34, 34);
					return CDRF_NEWFONT;
				}
				return CDRF_DODEFAULT;
			}
			if (header->code == NM_RCLICK)
			{
				show_game_library_context_menu(window);
				return 0;
			}
			if (header->code == LVN_COLUMNCLICK)
			{
				NMLISTVIEW *list = (NMLISTVIEW *)lparam;
				static const enum xavix_game_library_sort sorts[6] = {
					XAVIX_GAME_SORT_TITLE, XAVIX_GAME_SORT_STATUS,
					XAVIX_GAME_SORT_RELEASE, XAVIX_GAME_SORT_PLATFORM,
					XAVIX_GAME_SORT_MAKER, XAVIX_GAME_SORT_FILE
				};
				if (list->iSubItem >= 0 && list->iSubItem < 6)
				{
					g_game_library_sort = sorts[list->iSubItem];
					write_ini_integer(L"Library", L"SortColumn",
						g_game_library_sort);
					xavix_game_library_sort_entries(&g_game_library,
						g_game_library_sort);
					populate_game_library_view();
				}
				return 0;
			}
			if (header->code == NM_DBLCLK)
			{
				open_selected_library_game(window);
				return 0;
			}
			if (header->code == LVN_KEYDOWN &&
				((NMLVKEYDOWN *)lparam)->wVKey == VK_RETURN)
			{
				open_selected_library_game(window);
				return 0;
			}
		}
		break;

	case WM_INITMENUPOPUP:
		if ((HMENU)wparam == g_xavix2_channel_menu)
			update_xavix2_audio_channel_menu();
		break;

	case WM_COMMAND:
		if (LOWORD(wparam) == ID_XAVIX2_AUDIO_ENABLE_ALL && g_xavix2)
		{
			g_xavix2_audio_mute_mask = 0;
			xavix2_audio_set_mute_mask(&g_xavix2->audio, 0);
			update_xavix2_audio_channel_menu();
			return 0;
		}
		if (LOWORD(wparam) == ID_XAVIX2_AUDIO_MUTE_ALL && g_xavix2)
		{
			g_xavix2_audio_mute_mask = UINT64_MAX;
			xavix2_audio_set_mute_mask(&g_xavix2->audio, UINT64_MAX);
			update_xavix2_audio_channel_menu();
			return 0;
		}
		if (LOWORD(wparam) >= ID_XAVIX2_AUDIO_CHANNEL_FIRST &&
			LOWORD(wparam) <= ID_XAVIX2_AUDIO_CHANNEL_LAST && g_xavix2)
		{
			unsigned channel = LOWORD(wparam) -
				ID_XAVIX2_AUDIO_CHANNEL_FIRST;
			g_xavix2_audio_mute_mask ^= UINT64_C(1) << channel;
			xavix2_audio_set_mute_mask(&g_xavix2->audio,
				g_xavix2_audio_mute_mask);
			update_xavix2_audio_channel_menu();
			return 0;
		}
		switch (LOWORD(wparam))
		{
		case ID_FILE_GAME_LIBRARY:
			set_game_library_visible(window, 1);
			return 0;
		case ID_FILE_SET_ROM_DIRECTORY:
		case ID_LIBRARY_CHOOSE_DIRECTORY:
			choose_rom_directory(window);
			return 0;
		case ID_FILE_REFRESH_LIBRARY:
		case ID_LIBRARY_REFRESH:
			refresh_game_library(window, 1);
			set_game_library_visible(window, 1);
			return 0;
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
		case ID_VIEW_HIGH_RESOLUTION_3D:
			toggle_high_resolution_3d(window);
			return 0;
		case ID_VIEW_RECORD_WINDOW_SIZE:
			g_record_window_size = !g_record_window_size;
			write_ini_integer(L"Video", L"RecordWindowSize",
				g_record_window_size);
			CheckMenuItem(g_menu, ID_VIEW_RECORD_WINDOW_SIZE,
				MF_BYCOMMAND | (g_record_window_size ?
					MF_CHECKED : MF_UNCHECKED));
			return 0;
		case ID_VIEW_RECORD_FORMAT_AVI:
		case ID_VIEW_RECORD_FORMAT_MP4:
			if (!xavix_video_recorder_active(&g_video_recorder))
			{
				g_recording_format = LOWORD(wparam) ==
					ID_VIEW_RECORD_FORMAT_MP4 ? XAVIX_VIDEO_FORMAT_MP4 :
					XAVIX_VIDEO_FORMAT_AVI;
				write_ini_integer(L"Video", L"RecordingFormat",
					g_recording_format);
				CheckMenuRadioItem(g_recording_format_menu,
					ID_VIEW_RECORD_FORMAT_AVI, ID_VIEW_RECORD_FORMAT_MP4,
					LOWORD(wparam), MF_BYCOMMAND);
			}
			return 0;
		case ID_VIEW_FULLSCREEN:
			toggle_fullscreen(window);
			return 0;
		case ID_LANGUAGE_AUTO:
			set_language_preference(window, LANGUAGE_PREFERENCE_AUTO);
			return 0;
		case ID_LANGUAGE_ZH_TW:
			set_language_preference(window, LANGUAGE_PREFERENCE_ZH_TW);
			return 0;
		case ID_LANGUAGE_JAPANESE:
			set_language_preference(window, LANGUAGE_PREFERENCE_JAPANESE);
			return 0;
		case ID_LANGUAGE_FRENCH:
			set_language_preference(window, LANGUAGE_PREFERENCE_FRENCH);
			return 0;
		case ID_LANGUAGE_ENGLISH:
			set_language_preference(window, LANGUAGE_PREFERENCE_ENGLISH);
			return 0;
		case ID_CONTROLLER_SETTINGS:
			show_controller_settings(window);
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
		if (g_core && g_rom.kind == DRGQST_ROM_TAK_CHQ &&
			(g_left_button || g_right_button))
			update_tak_chq_mouse_steering(window, GET_X_LPARAM(lparam),
				GET_Y_LPARAM(lparam));
		else if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
			update_takecopter_mouse_drag(window, GET_X_LPARAM(lparam),
				GET_Y_LPARAM(lparam));
		else
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
		if (g_core && g_rom.kind == DRGQST_ROM_TAK_CHQ)
		{
			begin_tak_chq_mouse_steering(window);
			return 0;
		}
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_left_pulse_frames = 4;
		if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		{
			pulse_takecopter_tilt(TAKECOPTER_TILT_FORWARD);
			return 0;
		}
		if (g_xavix2)
		{
			if (g_rom.kind == DRGQST_ROM_BAN_BLDJ)
			{
				g_xavix2_area_gesture_frame = 0;
				g_xavix2_area_gesture_frames = 8;
				g_xavix2_gesture_kind = XAVIX2_GESTURE_BLDJ_FIRST_ATTACK;
			}
			else if (g_rom.kind == DRGQST_ROM_BAN_DB2J ||
				g_rom.kind == DRGQST_ROM_BAN_DBZ)
			{
				g_xavix2_area_gesture_frame = 0;
				g_xavix2_area_gesture_frames = 8;
				g_xavix2_gesture_kind =
					g_rom.kind == DRGQST_ROM_BAN_DBZ ?
					XAVIX2_GESTURE_DBZ_SINGLE_CLOSE :
					XAVIX2_GESTURE_DB2J_EDGE;
			}
			else
			{
				g_naruto_execute_delay = 2;
				g_naruto_execute_frames = 4;
			}
		}
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_LBUTTONUP:
		g_left_button = 0;
		if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		{
			if (!g_right_button) ReleaseCapture();
			return 0;
		}
		if (g_core && g_rom.kind == DRGQST_ROM_TAK_CHQ)
		{
			g_tak_chq_mouse_delta = 0;
			g_tak_chq_mouse_recentering = 0;
			if (!g_right_button) ReleaseCapture();
			return 0;
		}
		if (!g_right_button)
			ReleaseCapture();
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_RBUTTONDOWN:
		SetFocus(window);
		SetCapture(window);
		g_right_button = 1;
		if (g_core && g_rom.kind == DRGQST_ROM_TAK_CHQ)
		{
			begin_tak_chq_mouse_steering(window);
			return 0;
		}
		if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		{
			begin_takecopter_mouse_drag(GET_X_LPARAM(lparam),
				GET_Y_LPARAM(lparam));
			return 0;
		}
		if (g_xavix2 && (g_rom.kind == DRGQST_ROM_BAN_BLDJ ||
			g_rom.kind == DRGQST_ROM_BAN_DB2J ||
			g_rom.kind == DRGQST_ROM_BAN_DBZ))
		{
			g_xavix2_area_gesture_frame = 0;
			g_xavix2_area_gesture_frames = 8;
			g_xavix2_gesture_kind = g_rom.kind == DRGQST_ROM_BAN_BLDJ ?
				XAVIX2_GESTURE_BLDJ_SECOND_ATTACK :
				g_rom.kind == DRGQST_ROM_BAN_DBZ ?
				XAVIX2_GESTURE_DBZ_BOTH_CLOSE : XAVIX2_GESTURE_DB2J_EDGE;
		}
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_right_pulse_frames = 4;
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_RBUTTONUP:
		g_right_button = 0;
		if (g_core && g_rom.kind == DRGQST_ROM_TAK_CHQ)
		{
			g_tak_chq_mouse_delta = 0;
			g_tak_chq_mouse_recentering = 0;
			if (!g_left_button) ReleaseCapture();
			return 0;
		}
		if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		{
			g_takecopter_mouse_drag_valid = 0;
			g_mouse_x = 0x80;
			g_mouse_y = 0x80;
			if (!g_left_button) ReleaseCapture();
			return 0;
		}
		if (!g_left_button)
			ReleaseCapture();
		set_mouse_position(window, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_MBUTTONDOWN:
		SetFocus(window);
		if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ)
		{
			start_takecopter_boost();
			return 0;
		}
		if (g_core && g_rom.kind == DRGQST_ROM_EPO_HAMD)
			g_hamd_confirm_frames = 4;
		return 0;

	case WM_CAPTURECHANGED:
		if ((HWND)lparam != window)
		{
			g_left_button = 0;
			g_right_button = 0;
			g_takecopter_mouse_drag_valid = 0;
			g_takecopter_direction_pulse = TAKECOPTER_TILT_NEUTRAL;
			g_takecopter_direction_frames = 0;
			g_takecopter_boost_phase = 0;
			g_tak_chq_mouse_delta = 0;
			g_tak_chq_mouse_recentering = 0;
			if (g_rom.kind == DRGQST_ROM_EPO_DTCJ)
			{
				g_mouse_x = 0x80;
				g_mouse_y = 0x80;
			}
			update_core_mouse();
		}
		return 0;

	case WM_SETCURSOR:
		if (LOWORD(lparam) == HTCLIENT && emulator_loaded() &&
			!g_game_library_visible)
		{
			SetCursor(NULL);
			return TRUE;
		}
		break;

	case WM_SYSKEYDOWN:
		if (g_exit_confirmation && wparam == VK_RETURN)
			return 0;
		if (wparam == VK_RETURN && (lparam & ((LPARAM)1 << 29)) &&
			!(lparam & ((LPARAM)1 << 30)))
		{
			toggle_fullscreen(window);
			return 0;
		}
		break;

	case WM_KEYDOWN:
		if (wparam == VK_RETURN && g_resume_enter_blocked)
			return 0;
		if (g_exit_confirmation)
		{
			if (!(lparam & ((LPARAM)1 << 30)))
			{
				if (wparam == VK_ESCAPE)
					return_to_game_library(window);
				else if (wparam == VK_RETURN)
					resume_from_exit_confirmation(window);
			}
			return 0;
		}
		if (wparam == VK_ESCAPE && emulator_loaded())
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				begin_exit_confirmation(window);
			return 0;
		}
		if (wparam == VK_F9 && emulator_loaded() &&
			!(lparam & ((LPARAM)1 << 30)))
		{
			start_video_recording(window);
			return 0;
		}
		if (wparam == VK_F10 && !(lparam & ((LPARAM)1 << 30)))
		{
			stop_video_recording(window, 1);
			return 0;
		}
		if (wparam == VK_F11 && !(lparam & ((LPARAM)1 << 30)))
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
		if (g_xavix2 && g_rom.kind == DRGQST_ROM_EPO_DTCJ &&
			(wparam == VK_SPACE || wparam == VK_RETURN))
		{
			if (!(lparam & ((LPARAM)1 << 30)))
			{
				if (wparam == VK_SPACE)
					start_takecopter_boost();
				else
					pulse_takecopter_tilt(TAKECOPTER_TILT_FORWARD);
			}
			return 0;
		}
		if (wparam == VK_SPACE && g_xavix2)
		{
			if (g_rom.kind == DRGQST_ROM_BAN_BLDJ)
			{
				g_naruto_joined_hands = 1;
				if (!(lparam & ((LPARAM)1 << 30)))
				{
					g_xavix2_area_gesture_frame = 0;
					g_xavix2_area_gesture_frames = 8;
					g_xavix2_gesture_kind =
						XAVIX2_GESTURE_BLDJ_BOTH_ATTACK;
				}
			}
			else if (g_rom.kind == DRGQST_ROM_BAN_DB2J ||
				g_rom.kind == DRGQST_ROM_BAN_DBZ)
			{
				if (!(lparam & ((LPARAM)1 << 30)))
				{
					g_xavix2_area_gesture_frame = 0;
					g_xavix2_area_gesture_frames =
						g_rom.kind == DRGQST_ROM_BAN_DBZ ? 4 : 8;
					g_xavix2_gesture_kind =
						g_rom.kind == DRGQST_ROM_BAN_DBZ ?
						XAVIX2_GESTURE_DBZ_DEFLECT :
						XAVIX2_GESTURE_DB2J_EDGE;
				}
			}
			else
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
		if (g_core && !(wparam == VK_ESCAPE && g_fullscreen) &&
			set_tvpc_keyboard_key(wparam, lparam, 1))
		{
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
		if (wparam == VK_F5 && emulator_loaded())
		{
			if (!(lparam & ((LPARAM)1 << 30)))
				save_runtime_state(window);
			return 0;
		}
		if (wparam == VK_F7 && emulator_loaded())
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
		if (wparam == VK_RETURN && g_resume_enter_blocked)
		{
			g_resume_enter_blocked = 0;
			return 0;
		}
		if (g_exit_confirmation)
			return 0;
		if (wparam == VK_SPACE && g_xavix2)
		{
			if (g_rom.kind != DRGQST_ROM_BAN_DB2J &&
				g_rom.kind != DRGQST_ROM_BAN_DBZ)
				g_naruto_joined_hands = 0;
			return 0;
		}
		if (set_tvpc_keyboard_key(wparam, lparam, 0))
		{
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
			int minimum_width = g_game_library_visible ? 720 :
				(g_stretch_4_3 ?
					((int)logical_frame_height() * 4 + 1) / 3 :
					(int)logical_frame_width());
			int minimum_height = g_game_library_visible ? 480 :
				(int)logical_frame_height();
			RECT rectangle = { 0, 0, minimum_width, minimum_height };
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
		stop_video_recording(window, 0);
		save_persistent_eeprom(window, 1);
		win_audio_shutdown(&g_audio_output);
		release_capture_surface();
		if (g_game_library_images)
		{
			ImageList_Destroy(g_game_library_images);
			g_game_library_images = NULL;
		}
		xavix_controller_input_shutdown(&g_controller_input);
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
	INITCOMMONCONTROLSEX common_controls;
	HRESULT com_result;

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
	xavix_video_recorder_init(&g_video_recorder);
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
	if (!initialize_executable_directory() || !initialize_application_paths())
	{
		MessageBoxW(NULL, interface_text()->storage_directory_error,
			interface_text()->window_title_idle, MB_OK | MB_ICONERROR);
		if (arguments)
			LocalFree(arguments);
		return 1;
	}
	load_application_settings();
	xavix_controller_input_init(&g_controller_input, g_ini_path);
	memset(&common_controls, 0, sizeof(common_controls));
	common_controls.dwSize = sizeof(common_controls);
	common_controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
	InitCommonControlsEx(&common_controls);
	com_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
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
		initial_rom ? FRAME_WIDTH * DEFAULT_SCALE : 1080,
		initial_rom ? FRAME_HEIGHT * DEFAULT_SCALE : 720,
		NULL, g_menu, instance, NULL);
	if (!window)
	{
		if (arguments)
			LocalFree(arguments);
		timeEndPeriod(1);
		return 1;
	}

	DragAcceptFiles(window, TRUE);
	if (initial_rom)
		resize_for_scale(window, g_window_scale);
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
	if (SUCCEEDED(com_result))
		CoUninitialize();
	return (int)message.wParam;
}
