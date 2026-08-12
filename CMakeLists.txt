cmake_minimum_required(VERSION 3.20)

project(XaviXEmu VERSION 0.3.0 LANGUAGES C RC)

set(MINIZ_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")

add_executable(xavixemu WIN32
	src/xavixemu.rc
	src/main.c
	src/cursor_presentation.c
	src/core/drgqst_core.c
	src/core/drgqst_state.c
	src/core/xavix_audio.c
	src/core/xavix_cpu.c
	src/core/xavix_machine.c
	src/core/xavix_peripherals.c
	src/core/xavix_video.c
	src/xavix2/xavix2_cpu.c
	src/xavix2/xavix2_audio.c
	src/xavix2/xavix2_machine.c
	src/persistence.c
	src/rom_loader.c
	src/screenshot.c
	src/sha1.c
	src/win_audio.c
	${MINIZ_DIRECTORY}/miniz.c
)

set_target_properties(xavixemu PROPERTIES
	OUTPUT_NAME XaviXEmu
)

target_include_directories(xavixemu PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/core
	${CMAKE_CURRENT_SOURCE_DIR}/src/xavix2
	${MINIZ_DIRECTORY}
)

target_compile_definitions(xavixemu PRIVATE
	UNICODE
	_UNICODE
	WIN32_LEAN_AND_MEAN
	NOMINMAX
	MINIZ_NO_TIME
	MINIZ_NO_STDIO
	MINIZ_NO_ARCHIVE_WRITING_APIS
	MINIZ_NO_ZLIB_APIS
	MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(xavixemu PRIVATE
		-Os
		-fdata-sections
		-ffunction-sections
		-fno-ident
		-Wall
		-Wextra
		-Wpedantic
	)
	target_link_options(xavixemu PRIVATE
		-s
		-Wl,--gc-sections
	)
endif()

target_link_libraries(xavixemu PRIVATE
	comdlg32
	gdi32
	shell32
	user32
	winmm
	ole32
	windowscodecs
)

install(TARGETS xavixemu RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)

include(CTest)
if(BUILD_TESTING)
	add_executable(sha1-test
		tests/sha1_test.c
		src/sha1.c
	)
	target_include_directories(sha1-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	add_test(NAME sha1-test COMMAND sha1-test)

	add_executable(xavix-machine-test
		tests/xavix_machine_test.c
		src/core/xavix_machine.c
		src/core/xavix_peripherals.c
	)
	target_include_directories(xavix-machine-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	add_test(NAME xavix-machine-test COMMAND xavix-machine-test)

	add_executable(xavix-cpu-test
		tests/xavix_cpu_test.c
		src/core/xavix_cpu.c
	)
	target_include_directories(xavix-cpu-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
	add_test(NAME xavix-cpu-test COMMAND xavix-cpu-test)

	add_executable(xavix2-cpu-test
		tests/xavix2_cpu_test.c
		src/xavix2/xavix2_cpu.c
	)
	target_include_directories(xavix2-cpu-test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/xavix2
	)
	add_test(NAME xavix2-cpu-test COMMAND xavix2-cpu-test)

	add_executable(xavix2-machine-test
		tests/xavix2_machine_test.c
		src/xavix2/xavix2_cpu.c
		src/xavix2/xavix2_audio.c
		src/xavix2/xavix2_machine.c
		src/core/xavix_peripherals.c
	)
	target_include_directories(xavix2-machine-test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/core
		${CMAKE_CURRENT_SOURCE_DIR}/src/xavix2
	)
	add_test(NAME xavix2-machine-test COMMAND xavix2-machine-test)

	add_executable(xavix2-audio-test
		tests/xavix2_audio_test.c
		src/xavix2/xavix2_audio.c
	)
	target_include_directories(xavix2-audio-test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/xavix2
	)
	add_test(NAME xavix2-audio-test COMMAND xavix2-audio-test)

	add_executable(xavix-video-test
		tests/xavix_video_test.c
		src/core/xavix_video.c
	)
	target_include_directories(xavix-video-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
	add_test(NAME xavix-video-test COMMAND xavix-video-test)

	add_executable(xavix-peripherals-test
		tests/xavix_peripherals_test.c
		src/core/xavix_peripherals.c
	)
	target_include_directories(xavix-peripherals-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
	add_test(NAME xavix-peripherals-test COMMAND xavix-peripherals-test)

	add_executable(xavix-audio-test
		tests/xavix_audio_test.c
		src/core/xavix_audio.c
	)
	target_include_directories(xavix-audio-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
	add_test(NAME xavix-audio-test COMMAND xavix-audio-test)

	add_executable(persistence-test
		tests/persistence_test.c
		src/persistence.c
	)
	target_include_directories(persistence-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_compile_definitions(persistence-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
	add_test(NAME persistence-test COMMAND persistence-test)

	add_executable(screenshot-test
		tests/screenshot_test.c
		src/screenshot.c
	)
	target_include_directories(screenshot-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_compile_definitions(screenshot-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
	target_link_libraries(screenshot-test PRIVATE gdi32 ole32 windowscodecs)
	add_test(NAME screenshot-test COMMAND screenshot-test)

	add_executable(cursor-presentation-test
		tests/cursor_presentation_test.c
		src/cursor_presentation.c
	)
	target_include_directories(cursor-presentation-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	add_test(NAME cursor-presentation-test COMMAND cursor-presentation-test)

	add_executable(win-audio-test
		tests/win_audio_test.c
		src/win_audio.c
	)
	target_include_directories(win-audio-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_compile_definitions(win-audio-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
	target_link_libraries(win-audio-test PRIVATE winmm)
	add_test(NAME win-audio-test COMMAND win-audio-test)

	add_executable(drgqst-state-test
		tests/drgqst_state_test.c
		src/core/drgqst_core.c
		src/core/drgqst_state.c
		src/core/xavix_audio.c
		src/core/xavix_cpu.c
		src/core/xavix_machine.c
		src/core/xavix_peripherals.c
		src/core/xavix_video.c
	)
	target_include_directories(drgqst-state-test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/src/core
	)
	add_test(NAME drgqst-state-test COMMAND drgqst-state-test)

	add_executable(rom-verify
		tests/rom_verify.c
		src/rom_loader.c
		src/sha1.c
		${MINIZ_DIRECTORY}/miniz.c
	)
	target_include_directories(rom-verify PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${MINIZ_DIRECTORY}
	)
	target_compile_definitions(rom-verify PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		MINIZ_NO_TIME
		MINIZ_NO_STDIO
		MINIZ_NO_ARCHIVE_WRITING_APIS
		MINIZ_NO_ZLIB_APIS
		MINIZ_NO_ZLIB_COMPATIBLE_NAMES
	)

	add_executable(drgqst-boot-probe
		tests/drgqst_boot_probe.c
		src/core/drgqst_core.c
		src/core/xavix_audio.c
		src/core/xavix_cpu.c
		src/core/xavix_machine.c
		src/core/xavix_peripherals.c
		src/core/xavix_video.c
		src/rom_loader.c
		src/sha1.c
		${MINIZ_DIRECTORY}/miniz.c
	)

	add_executable(xavix2-boot-probe
		tests/xavix2_boot_probe.c
		src/xavix2/xavix2_cpu.c
		src/xavix2/xavix2_audio.c
		src/xavix2/xavix2_machine.c
		src/core/xavix_peripherals.c
		src/rom_loader.c
		src/sha1.c
		${MINIZ_DIRECTORY}/miniz.c
	)
	target_include_directories(xavix2-boot-probe PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/src/core
		${CMAKE_CURRENT_SOURCE_DIR}/src/xavix2
		${MINIZ_DIRECTORY}
	)
	target_compile_definitions(xavix2-boot-probe PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		MINIZ_NO_TIME
		MINIZ_NO_STDIO
		MINIZ_NO_ARCHIVE_WRITING_APIS
		MINIZ_NO_ZLIB_APIS
		MINIZ_NO_ZLIB_COMPATIBLE_NAMES
	)
	target_include_directories(drgqst-boot-probe PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/src/core
		${MINIZ_DIRECTORY}
	)
	target_compile_definitions(drgqst-boot-probe PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		MINIZ_NO_TIME
		MINIZ_NO_STDIO
		MINIZ_NO_ARCHIVE_WRITING_APIS
		MINIZ_NO_ZLIB_APIS
		MINIZ_NO_ZLIB_COMPATIBLE_NAMES
	)

	add_executable(drgqst-frame-probe
		tests/drgqst_frame_probe.c
		src/core/drgqst_core.c
		src/core/drgqst_state.c
		src/core/xavix_audio.c
		src/core/xavix_cpu.c
		src/core/xavix_machine.c
		src/core/xavix_peripherals.c
		src/core/xavix_video.c
		src/persistence.c
		src/rom_loader.c
		src/sha1.c
		${MINIZ_DIRECTORY}/miniz.c
	)
	target_include_directories(drgqst-frame-probe PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/src/core
		${MINIZ_DIRECTORY}
	)
	target_compile_definitions(drgqst-frame-probe PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		MINIZ_NO_TIME
		MINIZ_NO_STDIO
		MINIZ_NO_ARCHIVE_WRITING_APIS
		MINIZ_NO_ZLIB_APIS
		MINIZ_NO_ZLIB_COMPATIBLE_NAMES
	)
endif()
