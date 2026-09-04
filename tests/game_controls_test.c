#include "game_controls.h"

#include <assert.h>
#include <stddef.h>

#define NON_X2_GAME_COUNT 31
static const enum drgqst_rom_kind NON_X2_GAMES[NON_X2_GAME_COUNT] =
{
	DRGQST_ROM_DRAGON_QUEST, DRGQST_ROM_BAN_ONEP, DRGQST_ROM_BAN_OMT,
	DRGQST_ROM_TTV_LOTR, DRGQST_ROM_TTV_SW, DRGQST_ROM_TTV_SWJ,
	DRGQST_ROM_TTV_MX, DRGQST_ROM_TOM_JUMP, DRGQST_ROM_EPO_SDB,
	DRGQST_ROM_EPO_BOWL, DRGQST_ROM_EPO_HAMD, DRGQST_ROM_TVPC_DOR,
	DRGQST_ROM_TAK_CHQ, DRGQST_ROM_EPO_EBOX, DRGQST_ROM_EPO_ES2J,
	DRGQST_ROM_EPO_HAMC, DRGQST_ROM_TVPC_HAM, DRGQST_ROM_TVPC_HK,
	DRGQST_ROM_TOM_DPGM, DRGQST_ROM_EPO_MINI, DRGQST_ROM_RAD_MTRK,
	DRGQST_ROM_RAD_SNOW, DRGQST_ROM_RAD_SSX, DRGQST_ROM_RAD_SBW,
	DRGQST_ROM_TAK_GIN, DRGQST_ROM_TCARNAVI, DRGQST_ROM_TOMTHR,
	DRGQST_ROM_EPO_CROK, DRGQST_ROM_TAK_ZUBA, DRGQST_ROM_DUELMAST,
	DRGQST_ROM_EPO_GOLF
};

static void assert_action(enum drgqst_rom_kind kind,
	enum xavix_controller_action action,
	enum xavix_game_control_label expected)
{
	const xavix_game_control_profile *profile =
		xavix_game_control_profile_for_kind(kind);
	assert(profile != NULL);
	assert(profile->actions[action] == expected);
	assert(xavix_game_control_action_visible(profile, action) ==
		(expected != XAVIX_GAME_CONTROL_NONE));
}

int main(void)
{
	size_t first;
	size_t second;
	const xavix_game_control_profile *profile;

	assert(xavix_game_control_profile_count() > 30);
	for (first = 0; first < NON_X2_GAME_COUNT; ++first)
	{
		assert(!drgqst_rom_is_xavix2(NON_X2_GAMES[first]));
		assert(xavix_game_control_profile_for_kind(
			NON_X2_GAMES[first]) != NULL);
	}
	for (first = 0; first < xavix_game_control_profile_count(); ++first)
	{
		profile = xavix_game_control_profile_at(first);
		assert(profile != NULL);
		assert(profile->kind != DRGQST_ROM_UNKNOWN);
		for (second = first + 1;
			second < xavix_game_control_profile_count(); ++second)
			assert(profile->kind !=
				xavix_game_control_profile_at(second)->kind);
		for (second = 0; second < XAVIX_CONTROLLER_ACTION_COUNT; ++second)
		{
			enum xavix_game_control_label label = profile->actions[second];
			unsigned language;
			assert(label >= XAVIX_GAME_CONTROL_NONE);
			assert(label < XAVIX_GAME_CONTROL_COUNT);
			if (label == XAVIX_GAME_CONTROL_NONE)
				continue;
			for (language = 0; language < 4; ++language)
				assert(xavix_game_control_label_text(label, language)[0]);
		}
	}
	assert(xavix_game_control_profile_at(
		xavix_game_control_profile_count()) == NULL);

	/* An entry exists only after host controls have actually been wired. */
	assert(xavix_game_control_profile_for_kind(DRGQST_ROM_UNKNOWN) == NULL);
	assert(xavix_game_control_profile_for_kind(DRGQST_ROM_EPO_DAB2J) == NULL);
	assert(xavix_game_control_profile_for_kind(DRGQST_ROM_EPO_PABJ) == NULL);
	assert(xavix_game_control_profile_for_kind(DRGQST_ROM_EPO_SSK2) == NULL);
	assert(xavix_game_control_profile_for_kind(DRGQST_ROM_EPO_SSKJ) == NULL);

	/* Dragon Quest exposes its real accessory actions, not generic buttons. */
	assert_action(DRGQST_ROM_DRAGON_QUEST, XAVIX_CONTROLLER_PRIMARY,
		XAVIX_GAME_CONTROL_ATTACK);
	assert_action(DRGQST_ROM_DRAGON_QUEST, XAVIX_CONTROLLER_SECONDARY,
		XAVIX_GAME_CONTROL_MAGIC);
	assert_action(DRGQST_ROM_DRAGON_QUEST, XAVIX_CONTROLLER_DEFENSE,
		XAVIX_GAME_CONTROL_DEFENSE);
	assert_action(DRGQST_ROM_DRAGON_QUEST, XAVIX_CONTROLLER_SPECIAL,
		XAVIX_GAME_CONTROL_SPECIAL);
	assert_action(DRGQST_ROM_DRAGON_QUEST, XAVIX_CONTROLLER_CONFIRM,
		XAVIX_GAME_CONTROL_NONE);

	/* Racing and sword games each expose their own physical controls. */
	assert_action(DRGQST_ROM_TAK_CHQ, XAVIX_CONTROLLER_PRIMARY,
		XAVIX_GAME_CONTROL_ACCELERATE);
	assert_action(DRGQST_ROM_TAK_CHQ, XAVIX_CONTROLLER_SECONDARY,
		XAVIX_GAME_CONTROL_BRAKE);
	assert_action(DRGQST_ROM_TAK_CHQ, XAVIX_CONTROLLER_SPECIAL,
		XAVIX_GAME_CONTROL_REAR_COMMAND);
	assert_action(DRGQST_ROM_TTV_SW, XAVIX_CONTROLLER_PRIMARY,
		XAVIX_GAME_CONTROL_SWORD_ATTACK);
	assert_action(DRGQST_ROM_TTV_SW, XAVIX_CONTROLLER_SPECIAL,
		XAVIX_GAME_CONTROL_SABER_SPIN);

	assert_action(DRGQST_ROM_TTV_MX, XAVIX_CONTROLLER_SPECIAL,
		XAVIX_GAME_CONTROL_PAUSE);
	assert_action(DRGQST_ROM_RAD_MTRK, XAVIX_CONTROLLER_DEFENSE,
		XAVIX_GAME_CONTROL_REVERSE);
	assert_action(DRGQST_ROM_TCARNAVI, XAVIX_CONTROLLER_SPECIAL,
		XAVIX_GAME_CONTROL_SIREN_TRANSFORM);
	assert_action(DRGQST_ROM_TCARNAVI, XAVIX_CONTROLLER_CONFIRM,
		XAVIX_GAME_CONTROL_IGNITION_KEY);
	assert_action(DRGQST_ROM_TOMTHR, XAVIX_CONTROLLER_SPECIAL,
		XAVIX_GAME_CONTROL_MAP);
	assert_action(DRGQST_ROM_TOMTHR, XAVIX_CONTROLLER_TWO_HAND,
		XAVIX_GAME_CONTROL_MICROPHONE);

	/* Take-copter exposes the physical head tilt and quick backward-to-forward
	 * acceleration gesture as independently bindable host actions. */
	profile = xavix_game_control_profile_for_kind(DRGQST_ROM_EPO_DTCJ);
	assert(profile != NULL);
	assert(profile->actions[XAVIX_CONTROLLER_PRIMARY] ==
		XAVIX_GAME_CONTROL_TILT_FORWARD);
	assert(profile->actions[XAVIX_CONTROLLER_SECONDARY] ==
		XAVIX_GAME_CONTROL_TILT_BACKWARD);
	assert(profile->actions[XAVIX_CONTROLLER_DEFENSE] ==
		XAVIX_GAME_CONTROL_TILT_LEFT);
	assert(profile->actions[XAVIX_CONTROLLER_TWO_HAND] ==
		XAVIX_GAME_CONTROL_TILT_RIGHT);
	assert(profile->actions[XAVIX_CONTROLLER_DEFLECT] ==
		XAVIX_GAME_CONTROL_TILT_CENTER);
	assert(profile->actions[XAVIX_CONTROLLER_SPECIAL] ==
		XAVIX_GAME_CONTROL_TAKECOPTER_BOOST);
	assert(profile->features & XAVIX_GAME_CONTROL_FEATURE_MOTION);

	/* The Take-copter accessory's physical axes are transposed relative to the
	 * old screen-pointer assumption: $01/$04 accelerate and $10/$40 steer. */
	assert(xavix_takecopter_ir_code(0, 0, 0, 0) ==
		UINT32_C(0x0199e667));
	assert(xavix_takecopter_ir_code(1, 0, 0, 0) ==
		UINT32_C(0x0181e787));
	assert(xavix_takecopter_ir_code(0, 1, 0, 0) ==
		UINT32_C(0x01986679));
	assert(xavix_takecopter_ir_code(0, 0, 1, 0) ==
		UINT32_C(0x019987ff));
	assert(xavix_takecopter_ir_code(0, 0, 0, 1) ==
		UINT32_C(0x0199e001));
	assert(xavix_takecopter_ir_code(1, 0, 1, 0) ==
		UINT32_C(0x0181861f));
	assert(xavix_takecopter_ir_code(0, 1, 0, 1) ==
		UINT32_C(0x0198601f));

	return 0;
}
