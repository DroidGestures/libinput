/*
 * Copyright © 2026 Owen Kent
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <config.h>

#include <libinput.h>
#include <math.h>

#include "libinput-util.h"
#include "litest.h"

/* Buttons given as arguments, for a function that takes an array and its
 * length */
#define BUTTONS(...) \
	(const uint32_t[]){ __VA_ARGS__ }, \
		ARRAY_LENGTH(((const uint32_t[]){ __VA_ARGS__ }))

#define set_buttons(device_, ...) \
	libinput_device_config_click_lock_set_buttons(device_, BUTTONS(__VA_ARGS__))

#define click_lock_enable(dev_, ...) \
	litest_enable_click_lock(dev_, BUTTONS(__VA_ARGS__))

static void
click_lock_setup_buttons(struct litest_device *dev,
			 const uint32_t *buttons,
			 size_t nbuttons)
{
	litest_enable_click_lock(dev, buttons, nbuttons);

	if (libinput_device_config_middle_emulation_is_available(dev->libinput_device))
		litest_disable_middleemu(dev);

	litest_drain_events(dev->libinput);
}

#define click_lock_setup(dev_, ...) \
	click_lock_setup_buttons(dev_, BUTTONS(__VA_ARGS__))

static void
click(struct litest_device *dev, unsigned int button)
{
	litest_button_click_debounced(dev, dev->libinput, button, true);
	litest_button_click_debounced(dev, dev->libinput, button, false);
}

/* A click that is too short to lock. The timeout is out of reach for it
 * so that a slow test runner does not turn the click into a hold. */
static void
short_click(struct litest_device *dev, unsigned int button)
{
	struct libinput_device *device = dev->libinput_device;
	uint32_t timeout = libinput_device_config_click_lock_get_timeout(device);

	libinput_device_config_click_lock_set_timeout(device, 10000);
	click(dev, button);
	libinput_device_config_click_lock_set_timeout(device, timeout);
}

/* press, hold past the timeout, release */
static void
hold(struct litest_device *dev, unsigned int button)
{
	litest_button_click_debounced(dev, dev->libinput, button, true);
	litest_timeout_click_lock(dev->libinput);
	litest_button_click_debounced(dev, dev->libinput, button, false);
}

static void
move(struct litest_device *dev, int dx, int dy)
{
	litest_event(dev, EV_REL, REL_X, dx);
	litest_event(dev, EV_REL, REL_Y, dy);
	litest_event(dev, EV_SYN, SYN_REPORT, 0);
	litest_dispatch(dev->libinput);
}

/* For a device that is not a litest device */
static void
uinput_button(struct libevdev_uinput *uinput,
	      struct libinput *li,
	      unsigned int button,
	      bool is_press)
{
	libevdev_uinput_write_event(uinput, EV_KEY, button, is_press);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);
	litest_timeout_debounce(li);
}

/* A locked button: its press came through, its release did not */
static void
assert_locked(struct libinput *li, unsigned int button)
{
	litest_assert_button_event(li, button, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);
}

static void
assert_click(struct libinput *li, unsigned int button)
{
	litest_assert_button_event(li, button, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, button, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}

static void
assert_released(struct libinput *li, unsigned int button)
{
	litest_assert_button_event(li, button, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}

static void
assert_buttons_array(struct libinput_device *device,
		     const uint32_t *expected,
		     size_t nexpected)
{
	uint32_t buttons[8] = { 0 };
	size_t nbuttons;

	nbuttons = libinput_device_config_click_lock_get_buttons(device,
								 buttons,
								 ARRAY_LENGTH(buttons));
	litest_assert_int_eq(nbuttons, nexpected);
	for (size_t i = 0; i < nexpected; i++)
		litest_assert_int_eq(buttons[i], expected[i]);
}

#define assert_buttons(device_, ...) \
	assert_buttons_array(device_, BUTTONS(__VA_ARGS__))

START_TEST(click_lock_config_defaults)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	uint32_t buttons[2] = { 0, 0 };
	size_t nbuttons;

	litest_assert_int_eq(libinput_device_config_click_lock_get_modes(device),
			     (uint32_t)LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT);

	litest_assert_enum_eq(libinput_device_config_click_lock_get_mode(device),
			      LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);
	litest_assert_enum_eq(
		libinput_device_config_click_lock_get_default_mode(device),
		LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);
	litest_assert_int_eq(libinput_device_config_click_lock_get_timeout(device),
			     1200U);
	litest_assert_int_eq(
		libinput_device_config_click_lock_get_default_timeout(device),
		1200U);
	assert_buttons(device, BTN_LEFT);
	nbuttons = libinput_device_config_click_lock_get_default_buttons(
		device,
		buttons,
		ARRAY_LENGTH(buttons));
	litest_assert_int_eq(nbuttons, 1U);
	litest_assert_int_eq(buttons[0], (uint32_t)BTN_LEFT);
	litest_assert_int_eq(buttons[1], 0U);
	litest_assert_double_eq(libinput_device_config_click_lock_get_threshold(device),
				1.0);
	litest_assert_double_eq(
		libinput_device_config_click_lock_get_default_threshold(device),
		1.0);
}
END_TEST

START_TEST(click_lock_config_mode)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	/* Not a mode, more than one mode */
	const uint32_t invalid[] = {
		LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT << 1,
		LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT |
			(LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT << 1),
		UINT32_MAX,
	};

	status = libinput_device_config_click_lock_set_mode(
		device,
		LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_enum_eq(libinput_device_config_click_lock_get_mode(device),
			      LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT);
	litest_assert_enum_eq(
		libinput_device_config_click_lock_get_default_mode(device),
		LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);

	ARRAY_FOR_EACH(invalid, m) {
		enum libinput_config_click_lock_mode mode = *m;

		status = libinput_device_config_click_lock_set_mode(device, mode);
		litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
		litest_assert_enum_eq(
			libinput_device_config_click_lock_get_mode(device),
			LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT);
	}

	status = libinput_device_config_click_lock_set_mode(
		device,
		LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_enum_eq(libinput_device_config_click_lock_get_mode(device),
			      LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);
}
END_TEST

START_TEST(click_lock_config_timeout)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	const uint32_t valid[] = { 0, 1, 200, 1000, 5001, 10000 };
	const uint32_t invalid[] = { 10001, UINT32_MAX };

	ARRAY_FOR_EACH(valid, t) {
		status = libinput_device_config_click_lock_set_timeout(device, *t);
		litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
		litest_assert_int_eq(
			libinput_device_config_click_lock_get_timeout(device),
			*t);
	}

	ARRAY_FOR_EACH(invalid, t) {
		status = libinput_device_config_click_lock_set_timeout(device, *t);
		litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
		litest_assert_int_eq(
			libinput_device_config_click_lock_get_timeout(device),
			10000U);
	}

	litest_assert_int_eq(
		libinput_device_config_click_lock_get_default_timeout(device),
		1200U);
}
END_TEST

START_TEST(click_lock_config_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	/* Not a pointer button code: tools, keys, what is not a key code at
	 * all */
	const uint32_t invalid[] = {
		BTN_DIGI,           BTN_TOOL_FINGER, BTN_TOUCH,
		BTN_STYLUS - 1,     KEY_RESERVED,    KEY_A,
		BTN_MISC - 1,       KEY_MAX,         KEY_MAX + 1,
		0x10000 | BTN_LEFT, UINT32_MAX,
	};
	uint32_t many[33];
	size_t nbuttons;

	/* The buttons are returned in the order they were set in */
	status = set_buttons(device, BTN_MIDDLE, BTN_LEFT, BTN_RIGHT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_buttons(device, BTN_MIDDLE, BTN_LEFT, BTN_RIGHT);

	/* The new buttons replace the old ones */
	status = set_buttons(device, BTN_LEFT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_buttons(device, BTN_LEFT);

	/* A button given more than once counts once */
	status = set_buttons(device, BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_RIGHT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_buttons(device, BTN_RIGHT, BTN_LEFT);

	/* Buttons the device does not have are accepted, whatever kind of
	 * pointer button they are */
	status = set_buttons(device, BTN_GEAR_UP, BTN_SIDE, BTN_JOYSTICK, BTN_0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_buttons(device, BTN_GEAR_UP, BTN_SIDE, BTN_JOYSTICK, BTN_0);

	/* One invalid button and none of the buttons is set */
	ARRAY_FOR_EACH(invalid, b) {
		status = set_buttons(device, *b);
		litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
		status = set_buttons(device, BTN_MIDDLE, *b, BTN_LEFT);
		litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
		assert_buttons(device, BTN_GEAR_UP, BTN_SIDE, BTN_JOYSTICK, BTN_0);
	}
	status = libinput_device_config_click_lock_set_buttons(device, NULL, 1);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
	assert_buttons(device, BTN_GEAR_UP, BTN_SIDE, BTN_JOYSTICK, BTN_0);

	/* 32 different buttons at most */
	for (size_t i = 0; i < ARRAY_LENGTH(many); i++)
		many[i] = BTN_MISC + i;
	status = libinput_device_config_click_lock_set_buttons(device,
							       many,
							       ARRAY_LENGTH(many));
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
	assert_buttons(device, BTN_GEAR_UP, BTN_SIDE, BTN_JOYSTICK, BTN_0);
	many[32] = many[0];
	status = libinput_device_config_click_lock_set_buttons(device,
							       many,
							       ARRAY_LENGTH(many));
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	nbuttons = libinput_device_config_click_lock_get_buttons(device,
								 many,
								 ARRAY_LENGTH(many));
	litest_assert_int_eq(nbuttons, 32U);
	litest_assert_int_eq(many[0], (uint32_t)BTN_MISC);
	litest_assert_int_eq(many[31], (uint32_t)BTN_MISC + 31);

	/* No buttons at all, with or without an array */
	status = libinput_device_config_click_lock_set_buttons(device, invalid, 0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_buttons_array(device, NULL, 0);
	status = set_buttons(device, BTN_LEFT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_click_lock_set_buttons(device, NULL, 0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_buttons_array(device, NULL, 0);
}
END_TEST

START_TEST(click_lock_config_get_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	const uint32_t untouched = KEY_MAX;
	uint32_t buttons[4];
	size_t nbuttons;

	status = set_buttons(device, BTN_MIDDLE, BTN_RIGHT, BTN_LEFT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* The return value is the number of buttons written, never more
	 * than the array has room for */
	ARRAY_FOR_EACH(buttons, b)
		*b = untouched;
	nbuttons = libinput_device_config_click_lock_get_buttons(device, buttons, 0);
	litest_assert_int_eq(nbuttons, 0U);
	litest_assert_int_eq(buttons[0], untouched);
	nbuttons = libinput_device_config_click_lock_get_buttons(device, NULL, 4);
	litest_assert_int_eq(nbuttons, 0U);

	nbuttons = libinput_device_config_click_lock_get_buttons(device, buttons, 2);
	litest_assert_int_eq(nbuttons, 2U);
	litest_assert_int_eq(buttons[0], (uint32_t)BTN_MIDDLE);
	litest_assert_int_eq(buttons[1], (uint32_t)BTN_RIGHT);
	litest_assert_int_eq(buttons[2], untouched);

	nbuttons = libinput_device_config_click_lock_get_buttons(device, buttons, 4);
	litest_assert_int_eq(nbuttons, 3U);
	litest_assert_int_eq(buttons[0], (uint32_t)BTN_MIDDLE);
	litest_assert_int_eq(buttons[1], (uint32_t)BTN_RIGHT);
	litest_assert_int_eq(buttons[2], (uint32_t)BTN_LEFT);
	litest_assert_int_eq(buttons[3], untouched);

	/* The defaults are not what is set */
	ARRAY_FOR_EACH(buttons, b)
		*b = untouched;
	nbuttons = libinput_device_config_click_lock_get_default_buttons(device,
									 buttons,
									 0);
	litest_assert_int_eq(nbuttons, 0U);
	litest_assert_int_eq(buttons[0], untouched);
	nbuttons =
		libinput_device_config_click_lock_get_default_buttons(device, NULL, 4);
	litest_assert_int_eq(nbuttons, 0U);
	nbuttons = libinput_device_config_click_lock_get_default_buttons(device,
									 buttons,
									 4);
	litest_assert_int_eq(nbuttons, 1U);
	litest_assert_int_eq(buttons[0], (uint32_t)BTN_LEFT);
	litest_assert_int_eq(buttons[1], untouched);
}
END_TEST

START_TEST(click_lock_config_threshold)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	const double valid[] = { 0.0, 0.254, 1.0, 20.0 };
	const double invalid[] = { -0.1, 20.01, NAN, INFINITY, -INFINITY };

	ARRAY_FOR_EACH(valid, t) {
		status = libinput_device_config_click_lock_set_threshold(device, *t);
		litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
		litest_assert_double_eq(
			libinput_device_config_click_lock_get_threshold(device),
			*t);
	}

	ARRAY_FOR_EACH(invalid, t) {
		status = libinput_device_config_click_lock_set_threshold(device, *t);
		litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
		litest_assert_double_eq(
			libinput_device_config_click_lock_get_threshold(device),
			20.0);
	}

	litest_assert_double_eq(
		libinput_device_config_click_lock_get_default_threshold(device),
		1.0);
}
END_TEST

START_TEST(click_lock_config_unavailable)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	const uint32_t untouched = KEY_MAX;
	uint32_t buttons[1] = { untouched };
	size_t nbuttons;

	litest_assert_int_eq(libinput_device_config_click_lock_get_modes(device), 0U);

	status = libinput_device_config_click_lock_set_mode(
		device,
		LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_click_lock_set_mode(
		device,
		LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_click_lock_set_mode(
		device,
		LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT << 1);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);

	status = libinput_device_config_click_lock_set_timeout(device, 1000);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_click_lock_set_timeout(device, 0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	/* Unsupported comes before invalid */
	status = libinput_device_config_click_lock_set_timeout(device, 10001);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = set_buttons(device, BTN_LEFT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = set_buttons(device, KEY_A);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_click_lock_set_buttons(device, NULL, 0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_click_lock_set_threshold(device, 1.0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);
	status = libinput_device_config_click_lock_set_threshold(device, NAN);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_UNSUPPORTED);

	litest_assert_enum_eq(libinput_device_config_click_lock_get_mode(device),
			      LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);
	litest_assert_enum_eq(
		libinput_device_config_click_lock_get_default_mode(device),
		LIBINPUT_CONFIG_CLICK_LOCK_DISABLED);
	litest_assert_int_eq(libinput_device_config_click_lock_get_timeout(device), 0U);
	litest_assert_int_eq(
		libinput_device_config_click_lock_get_default_timeout(device),
		0U);
	nbuttons = libinput_device_config_click_lock_get_buttons(device, buttons, 1);
	litest_assert_int_eq(nbuttons, 0U);
	litest_assert_int_eq(buttons[0], untouched);
	nbuttons = libinput_device_config_click_lock_get_default_buttons(device,
									 buttons,
									 1);
	litest_assert_int_eq(nbuttons, 0U);
	litest_assert_int_eq(buttons[0], untouched);
	litest_assert_double_eq(libinput_device_config_click_lock_get_threshold(device),
				0.0);
	litest_assert_double_eq(
		libinput_device_config_click_lock_get_default_threshold(device),
		0.0);
}
END_TEST

START_TEST(click_lock_config_unavailable_with_abs)
{
	struct libevdev_uinput *uinput;
	struct libinput_device *device;
	struct input_absinfo absinfo[] = {
		{ ABS_X, 0, 32767, 0, 0, 0 },
		{ ABS_Y, 0, 32767, 0, 0, 0 },
		{ -1, -1, -1, -1, -1, -1 },
	};

	/* A mouse that can also move the pointer through absolute axes,
	 * e.g. in a virtual machine: click lock would not see that motion */
	/* clang-format off */
	uinput = litest_create_uinput_abs_device("test device", NULL,
						 absinfo,
						 EV_KEY, BTN_LEFT,
						 EV_KEY, BTN_RIGHT,
						 EV_REL, REL_X,
						 EV_REL, REL_Y,
						 -1);
	/* clang-format on */
	_litest_context_destroy_ struct libinput *li = litest_create_context();
	device = libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));
	litest_assert_notnull(device);

	litest_assert_int_eq(libinput_device_config_click_lock_get_modes(device), 0U);
	litest_assert_enum_eq(libinput_device_config_click_lock_set_mode(
				      device,
				      LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT),
			      LIBINPUT_CONFIG_STATUS_UNSUPPORTED);

	libinput_path_remove_device(device);
	libevdev_uinput_destroy(uinput);
}
END_TEST

START_TEST(click_lock_disabled_by_default)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_disable_middleemu(dev);
	litest_drain_events(li);

	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_hold_locks)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* Motion after the lock never unlocks */
	for (int i = 0; i < 10; i++)
		move(dev, 10, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	/* The click that unlocks is not a click of its own */
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	/* And the next one is a normal click */
	short_click(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_short_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	/* The maximum timeout */
	status = libinput_device_config_click_lock_set_timeout(dev->libinput_device,
							       10000);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* Released before the timeout: a normal click */
	click(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_move_cancels)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);

	/* 1mm is ~40 units at 1000dpi */
	for (int i = 0; i < 10; i++)
		move(dev, 5, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_move_after_delay_cancels)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_click_lock(li);

	/* A hold that moves is a drag */
	for (int i = 0; i < 10; i++)
		move(dev, 0, -5);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_threshold)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	/* 10 units at the assumed 1000dpi */
	status = libinput_device_config_click_lock_set_threshold(dev->libinput_device,
								 0.254);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* Back and forth past the threshold, ending well inside it: what
	 * counts is the distance from the press, so still a hold */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	move(dev, 8, 0);
	move(dev, -8, 0);
	move(dev, 4, 0);
	move(dev, 0, 4);
	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);

	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	/* Well outside it is a drag */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	move(dev, 6, 0);
	move(dev, 0, 12);
	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_drain_events_of_type(li, LIBINPUT_EVENT_POINTER_MOTION);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_threshold_zero)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	status = libinput_device_config_click_lock_set_threshold(dev->libinput_device,
								 0.0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* Without any motion the button still locks */
	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	/* The smallest motion is a drag */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	move(dev, 1, 0);
	litest_drain_events_of_type(li, LIBINPUT_EVENT_POINTER_MOTION);
	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_timeout_zero)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	status = libinput_device_config_click_lock_set_timeout(dev->libinput_device, 0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* However short the press, a click that does not move locks */
	click(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	/* Motion within the threshold is not a drag */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	move(dev, 5, 0);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	/* A click that moves past the threshold does not lock: 1mm is ~40
	 * units at 1000dpi */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	for (int i = 0; i < 10; i++)
		move(dev, 5, 0);
	litest_assert_only_typed_events(li, LIBINPUT_EVENT_POINTER_MOTION);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);

	click(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_timeout_change_while_pending)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	/* The hold in progress keeps the timeout it started with */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	status = libinput_device_config_click_lock_set_timeout(dev->libinput_device,
							       5000);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_locked(li, BTN_LEFT);

	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	/* The next one uses the new timeout */
	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_other_button_releases)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* The lock is released, the other button passes through */
	litest_button_click_debounced(dev, li, BTN_RIGHT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);
	assert_released(li, BTN_RIGHT);
}
END_TEST

START_TEST(click_lock_other_button_while_pending)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_RIGHT, true);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);

	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(click_lock_other_button_already_down)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT, BTN_RIGHT);

	/* The right button is down but not a click lock button here: a
	 * hold of the left button does not start */
	set_buttons(dev->libinput_device, BTN_LEFT);
	litest_button_click_debounced(dev, li, BTN_RIGHT, true);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_PRESSED);
	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);
	assert_released(li, BTN_RIGHT);

	/* Nor when both can lock, whichever is released first */
	set_buttons(dev->libinput_device, BTN_LEFT, BTN_RIGHT);
	litest_button_click_debounced(dev, li, BTN_RIGHT, true);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);
	assert_released(li, BTN_RIGHT);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);

	/* With nothing else down it locks as usual */
	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_any_pointer_button_already_down)
{
	struct libinput_device *device;
	struct libevdev_uinput *uinput;
	enum libinput_config_status status;

	/* A mouse with a button outside the range of the mouse buttons */
	/* clang-format off */
	uinput = litest_create_uinput_device("test device", NULL,
					     EV_KEY, BTN_LEFT,
					     EV_KEY, BTN_RIGHT,
					     EV_KEY, BTN_MIDDLE,
					     EV_KEY, BTN_GEAR_UP,
					     EV_REL, REL_X,
					     EV_REL, REL_Y,
					     -1);
	/* clang-format on */
	_litest_context_destroy_ struct libinput *li = litest_create_context();
	device = libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));
	litest_assert_notnull(device);
	libinput_device_config_click_lock_set_timeout(device, 200);
	libinput_device_config_click_lock_set_mode(device,
						   LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT);
	litest_drain_events(li);

	/* Whatever pointer button is down, the left button does not start
	 * a hold */
	uinput_button(uinput, li, BTN_GEAR_UP, true);
	litest_assert_button_event(li, BTN_GEAR_UP, LIBINPUT_BUTTON_STATE_PRESSED);
	uinput_button(uinput, li, BTN_LEFT, true);
	litest_timeout_click_lock(li);
	uinput_button(uinput, li, BTN_LEFT, false);
	assert_click(li, BTN_LEFT);
	uinput_button(uinput, li, BTN_GEAR_UP, false);
	assert_released(li, BTN_GEAR_UP);

	/* And such a button can lock like any other */
	status = set_buttons(device, BTN_GEAR_UP);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	uinput_button(uinput, li, BTN_GEAR_UP, true);
	litest_timeout_click_lock(li);
	uinput_button(uinput, li, BTN_GEAR_UP, false);
	assert_locked(li, BTN_GEAR_UP);
	uinput_button(uinput, li, BTN_GEAR_UP, true);
	uinput_button(uinput, li, BTN_GEAR_UP, false);
	assert_released(li, BTN_GEAR_UP);

	libinput_path_remove_device(device);
	libevdev_uinput_destroy(uinput);
}
END_TEST

START_TEST(click_lock_other_button_during_unlock_press)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT, BTN_RIGHT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* The press that unlocks is swallowed but the button is down: the
	 * right button does not start a hold meanwhile */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	assert_released(li, BTN_LEFT);

	hold(dev, BTN_RIGHT);
	assert_click(li, BTN_RIGHT);

	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_assert_empty_queue(li);

	short_click(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_seat_button_count)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct libinput_event_pointer *ptrev;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	litest_wait_for_event(li);
	_destroy_(libinput_event) *press = libinput_get_event(li);
	ptrev = litest_is_button_event(press, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_int_eq(libinput_event_pointer_get_seat_button_count(ptrev), 1U);
	litest_assert_empty_queue(li);

	/* While locked the button counts as down for the seat */
	click(dev, BTN_LEFT);
	litest_wait_for_event(li);
	_destroy_(libinput_event) *release = libinput_get_event(li);
	ptrev = litest_is_button_event(release,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_int_eq(libinput_event_pointer_get_seat_button_count(ptrev), 0U);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(click_lock_unlisted_button_ignored)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	/* the default buttons */
	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_RIGHT);
	assert_click(li, BTN_RIGHT);
	hold(dev, BTN_MIDDLE);
	assert_click(li, BTN_MIDDLE);
}
END_TEST

START_TEST(click_lock_listed_buttons_lock)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_RIGHT, BTN_MIDDLE);

	hold(dev, BTN_RIGHT);
	assert_locked(li, BTN_RIGHT);
	click(dev, BTN_RIGHT);
	assert_released(li, BTN_RIGHT);

	hold(dev, BTN_MIDDLE);
	assert_locked(li, BTN_MIDDLE);
	click(dev, BTN_MIDDLE);
	assert_released(li, BTN_MIDDLE);

	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_absent_button)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_assert(
		!libinput_device_pointer_has_button(dev->libinput_device, BTN_SIDE));

	/* One list for every device: a button this mouse does not have
	 * makes no difference */
	click_lock_setup(dev, BTN_SIDE, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	hold(dev, BTN_RIGHT);
	assert_click(li, BTN_RIGHT);
}
END_TEST

START_TEST(click_lock_extra_buttons_lock)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_SIDE, BTN_0);

	hold(dev, BTN_SIDE);
	assert_locked(li, BTN_SIDE);
	click(dev, BTN_SIDE);
	assert_released(li, BTN_SIDE);

	hold(dev, BTN_0);
	assert_locked(li, BTN_0);
	click(dev, BTN_0);
	assert_released(li, BTN_0);

	/* Another button releases it, also one that can lock */
	hold(dev, BTN_0);
	assert_locked(li, BTN_0);
	click(dev, BTN_SIDE);
	litest_assert_button_event(li, BTN_0, LIBINPUT_BUTTON_STATE_RELEASED);
	assert_click(li, BTN_SIDE);

	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);

	/* A button that is down keeps another one from starting a hold,
	 * here one that is not a click lock button */
	status = set_buttons(dev->libinput_device, BTN_SIDE);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_button_click_debounced(dev, li, BTN_0, true);
	litest_assert_button_event(li, BTN_0, LIBINPUT_BUTTON_STATE_PRESSED);
	hold(dev, BTN_SIDE);
	assert_click(li, BTN_SIDE);
	litest_button_click_debounced(dev, li, BTN_0, false);
	assert_released(li, BTN_0);
}
END_TEST

START_TEST(click_lock_buttons_setting)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_RIGHT);

	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
	hold(dev, BTN_MIDDLE);
	assert_click(li, BTN_MIDDLE);
	hold(dev, BTN_RIGHT);
	assert_locked(li, BTN_RIGHT);

	/* Taking the locked button out releases it, immediately */
	status = set_buttons(device, BTN_MIDDLE);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_released(li, BTN_RIGHT);

	/* A change that keeps it leaves the lock alone */
	hold(dev, BTN_MIDDLE);
	assert_locked(li, BTN_MIDDLE);
	status = set_buttons(device, BTN_MIDDLE, BTN_LEFT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_dispatch(li);
	litest_assert_empty_queue(li);
	click(dev, BTN_MIDDLE);
	assert_released(li, BTN_MIDDLE);

	status = set_buttons(device, BTN_LEFT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	hold(dev, BTN_MIDDLE);
	assert_click(li, BTN_MIDDLE);
	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* With no buttons nothing locks, that also releases the lock */
	status = libinput_device_config_click_lock_set_buttons(device, NULL, 0);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_released(li, BTN_LEFT);
	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_buttons_change_while_pending)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);

	status = set_buttons(dev->libinput_device, BTN_RIGHT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_empty_queue(li);

	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_disable_releases)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* The release is queued from within the config call */
	litest_disable_click_lock(dev);
	assert_released(li, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_disable_while_pending)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_click_lock(li);

	/* The button is still physically down, its own release follows */
	litest_disable_click_lock(dev);
	litest_assert_empty_queue(li);

	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_disable_during_unlock_click)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* The unlocking press was swallowed, so its release is too, even
	 * with the feature turned off in between */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	assert_released(li, BTN_LEFT);
	litest_disable_click_lock(dev);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_assert_empty_queue(li);

	click(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_sendevents_releases)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	status = libinput_device_config_send_events_set_mode(
		dev->libinput_device,
		LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_released(li, BTN_LEFT);

	status = libinput_device_config_send_events_set_mode(
		dev->libinput_device,
		LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_empty_queue(li);

	short_click(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_sendevents_while_pending)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_timeout_click_lock(li);

	/* The neutral-state release must not lock the button */
	status = libinput_device_config_send_events_set_mode(
		dev->libinput_device,
		LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	assert_released(li, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	status = libinput_device_config_send_events_set_mode(
		dev->libinput_device,
		LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_empty_queue(li);

	/* The debounce plugin did not see the release while the device was
	 * disabled and swallows the next click, see
	 * device_disable_release_buttons_reenable */
	click(dev, BTN_LEFT);
	litest_assert_empty_queue(li);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_sendevents_during_unlock_press)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	assert_released(li, BTN_LEFT);

	/* The release of the swallowed press comes while the device is
	 * disabled and is never seen */
	status = libinput_device_config_send_events_set_mode(
		dev->libinput_device,
		LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_empty_queue(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	status = libinput_device_config_send_events_set_mode(
		dev->libinput_device,
		LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	litest_assert_empty_queue(li);

	/* The debounce plugin swallows the next click, see
	 * click_lock_sendevents_while_pending */
	click(dev, BTN_LEFT);
	litest_assert_empty_queue(li);

	/* No release is swallowed for the one that was lost */
	short_click(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_suspend_releases)
{
	struct libinput *li;
	struct libinput_device *device;
	struct libinput_event *event;
	struct libevdev_uinput *uinput;

	/* clang-format off */
	uinput = litest_create_uinput_device("test device", NULL,
					     EV_KEY, BTN_LEFT,
					     EV_KEY, BTN_RIGHT,
					     EV_KEY, BTN_MIDDLE,
					     EV_REL, REL_X,
					     EV_REL, REL_Y,
					     -1);
	/* clang-format on */

	li = litest_create_context();
	device = libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));
	litest_assert_notnull(device);
	libinput_device_config_click_lock_set_timeout(device, 200);
	libinput_device_config_click_lock_set_mode(device,
						   LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT);
	litest_drain_events(li);

	uinput_button(uinput, li, BTN_LEFT, true);
	litest_timeout_click_lock(li);
	uinput_button(uinput, li, BTN_LEFT, false);
	assert_locked(li, BTN_LEFT);

	/* The release comes before the device goes away */
	libinput_suspend(li);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	event = libinput_get_event(li);
	litest_assert_event_type(event, LIBINPUT_EVENT_DEVICE_REMOVED);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	libinput_resume(li);
	litest_drain_events_of_type(li, LIBINPUT_EVENT_DEVICE_ADDED);

	/* A new device after the resume, click lock is off again */
	uinput_button(uinput, li, BTN_LEFT, true);
	uinput_button(uinput, li, BTN_LEFT, false);
	assert_click(li, BTN_LEFT);

	litest_destroy_context(li);
	libevdev_uinput_destroy(uinput);
}
END_TEST

START_TEST(click_lock_device_removal_releases)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct litest_device *mouse;
	struct libinput_event *event;

	mouse = litest_add_device(li, LITEST_MOUSE);
	click_lock_setup(mouse, BTN_LEFT);

	hold(mouse, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* The release comes before the device is removed... */
	litest_device_destroy(mouse);
	litest_dispatch(li);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	event = libinput_get_event(li);
	litest_assert_event_type(event, LIBINPUT_EVENT_DEVICE_REMOVED);
	libinput_event_destroy(event);
	litest_assert_empty_queue(li);

	/* ...so the seat count is back to zero */
	litest_disable_middleemu(dev);
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_wait_for_event(li);
	_destroy_(libinput_event) *press = libinput_get_event(li);
	struct libinput_event_pointer *ptrev =
		litest_is_button_event(press, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_int_eq(libinput_event_pointer_get_seat_button_count(ptrev), 1U);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_two_devices)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	struct litest_device *mouse;
	struct libinput_event_pointer *ptrev;

	mouse = litest_add_device(li, LITEST_MOUSE);
	click_lock_setup(dev, BTN_LEFT);
	click_lock_setup(mouse, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* Click lock is per device: a click on another device neither
	 * releases the lock nor locks itself */
	short_click(mouse, BTN_LEFT);
	litest_wait_for_event(li);
	_destroy_(libinput_event) *press = libinput_get_event(li);
	ptrev = litest_is_button_event(press, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_ptr_eq(libinput_event_get_device(press), mouse->libinput_device);
	litest_assert_int_eq(libinput_event_pointer_get_seat_button_count(ptrev), 2U);
	_destroy_(libinput_event) *release = libinput_get_event(li);
	ptrev = litest_is_button_event(release,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_int_eq(libinput_event_pointer_get_seat_button_count(ptrev), 1U);
	litest_assert_empty_queue(li);

	/* Removing the other device leaves the lock alone */
	litest_device_destroy(mouse);
	litest_drain_events_of_type(li, LIBINPUT_EVENT_DEVICE_REMOVED);
	litest_assert_empty_queue(li);

	click(dev, BTN_LEFT);
	litest_wait_for_event(li);
	_destroy_(libinput_event) *unlock = libinput_get_event(li);
	ptrev = litest_is_button_event(unlock,
				       BTN_LEFT,
				       LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_ptr_eq(libinput_event_get_device(unlock), dev->libinput_device);
	litest_assert_int_eq(libinput_event_pointer_get_seat_button_count(ptrev), 0U);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(click_lock_left_handed)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	status = libinput_device_config_left_handed_set(dev->libinput_device, 1);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	click_lock_setup(dev, BTN_LEFT);

	/* The buttons are those of the button events */
	hold(dev, BTN_RIGHT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_RIGHT);
	assert_released(li, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_click(li, BTN_RIGHT);
}
END_TEST

START_TEST(click_lock_left_handed_while_locked)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_LEFT);

	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);

	/* A locked button is down, the change waits for the unlock */
	status = libinput_device_config_left_handed_set(dev->libinput_device, 1);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);

	/* And applies right after it */
	click(dev, BTN_LEFT);
	assert_click(li, BTN_RIGHT);
}
END_TEST

START_TEST(click_lock_scroll_method_while_locked)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	enum libinput_config_status status;

	click_lock_setup(dev, BTN_RIGHT);

	hold(dev, BTN_RIGHT);
	assert_locked(li, BTN_RIGHT);

	/* A locked button is down, the change waits for the unlock */
	status = libinput_device_config_scroll_set_method(
		device,
		LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);
	status = libinput_device_config_scroll_set_button(device, BTN_LEFT);
	litest_assert_enum_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	/* So this left press is still a normal one, not the scroll button */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_assert_button_event(li, BTN_RIGHT, LIBINPUT_BUTTON_STATE_RELEASED);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_PRESSED);
	litest_assert_empty_queue(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	assert_released(li, BTN_LEFT);

	/* Now the left button scrolls */
	litest_button_click_debounced(dev, li, BTN_LEFT, true);
	litest_timeout_buttonscroll(li);
	for (int i = 0; i < 10; i++)
		move(dev, 0, 6);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_assert_scroll(li,
			     LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS,
			     LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
			     6);
	litest_assert_empty_queue(li);
}
END_TEST

START_TEST(click_lock_scroll_button)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	/* BTN_MIDDLE is the on-button scroll button on a trackpoint */
	litest_assert_enum_eq(
		libinput_device_config_scroll_get_method(dev->libinput_device),
		LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	click_lock_setup(dev, BTN_LEFT, BTN_MIDDLE);

	/* The scroll button never locks */
	hold(dev, BTN_MIDDLE);
	assert_click(li, BTN_MIDDLE);

	/* But its press still releases a lock */
	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_MIDDLE);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	assert_click(li, BTN_MIDDLE);
}
END_TEST

START_TEST(click_lock_scroll_button_held)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	litest_assert_enum_eq(
		libinput_device_config_scroll_get_method(dev->libinput_device),
		LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN);
	click_lock_setup(dev, BTN_LEFT);

	/* The scroll button is down but its press is only posted with its
	 * release. A button pressed now still does not start a hold. */
	litest_button_click_debounced(dev, li, BTN_MIDDLE, true);
	litest_assert_empty_queue(li);
	hold(dev, BTN_LEFT);
	assert_click(li, BTN_LEFT);

	litest_button_click_debounced(dev, li, BTN_MIDDLE, false);
	assert_click(li, BTN_MIDDLE);

	/* With the scroll button up the left button locks again */
	hold(dev, BTN_LEFT);
	assert_locked(li, BTN_LEFT);
	click(dev, BTN_LEFT);
	assert_released(li, BTN_LEFT);
}
END_TEST

START_TEST(click_lock_middleemu_no_middle)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_enable(dev, BTN_LEFT);
	litest_enable_middleemu(dev);
	litest_drain_events(li);

	hold(dev, BTN_LEFT);
	litest_timeout_middlebutton(li);
	assert_locked(li, BTN_LEFT);

	/* A right click while left is locked is not a middle click */
	litest_button_click_debounced(dev, li, BTN_RIGHT, true);
	litest_timeout_middlebutton(li);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);
	litest_assert_button_event(li, BTN_LEFT, LIBINPUT_BUTTON_STATE_RELEASED);
	assert_click(li, BTN_RIGHT);
}
END_TEST

START_TEST(click_lock_middleemu_middle_locks)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_enable(dev, BTN_MIDDLE);
	litest_enable_middleemu(dev);
	litest_drain_events(li);

	/* An emulated middle button held past the timeout locks */
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_RIGHT, true);
	litest_timeout_click_lock(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);
	assert_locked(li, BTN_MIDDLE);

	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_RIGHT, true);
	litest_timeout_middlebutton(li);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);
	assert_released(li, BTN_MIDDLE);
}
END_TEST

START_TEST(click_lock_middleemu_overlapping_unlock)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *li = dev->libinput;

	click_lock_enable(dev, BTN_MIDDLE);
	litest_enable_middleemu(dev);
	litest_drain_events(li);

	hold(dev, BTN_MIDDLE);
	assert_locked(li, BTN_MIDDLE);
	litest_button_click_debounced(dev, li, BTN_MIDDLE, true);
	assert_released(li, BTN_MIDDLE);

	/* The middle button is still held, its press was swallowed. The
	 * emulated middle button is a second press of the same button:
	 * one of the two releases is swallowed, the button is up with
	 * the last one. */
	litest_button_click(dev, BTN_LEFT, true);
	litest_button_click(dev, BTN_RIGHT, true);
	litest_dispatch(li);
	assert_locked(li, BTN_MIDDLE);
	litest_button_click_debounced(dev, li, BTN_LEFT, false);
	litest_button_click_debounced(dev, li, BTN_RIGHT, false);
	litest_assert_empty_queue(li);

	litest_button_click_debounced(dev, li, BTN_MIDDLE, false);
	assert_released(li, BTN_MIDDLE);

	short_click(dev, BTN_MIDDLE);
	assert_click(li, BTN_MIDDLE);
}
END_TEST

TEST_COLLECTION(click_lock)
{
	/* clang-format off */
	/* Mice, trackballs and pointing sticks */
	litest_add(click_lock_config_defaults, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ABSOLUTE);
	litest_add(click_lock_config_mode, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ABSOLUTE);
	litest_add(click_lock_config_timeout, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ABSOLUTE);
	litest_add(click_lock_config_buttons, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ABSOLUTE);
	litest_add(click_lock_config_get_buttons, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ABSOLUTE);
	litest_add(click_lock_config_threshold, LITEST_RELATIVE|LITEST_BUTTON, LITEST_ABSOLUTE);
	litest_add(click_lock_config_unavailable, LITEST_ANY, LITEST_RELATIVE|LITEST_TOUCHPAD);
	litest_add(click_lock_config_unavailable, LITEST_RELATIVE|LITEST_ABSOLUTE, LITEST_ANY);
	litest_add_no_device(click_lock_config_unavailable_with_abs);

	litest_add_for_device(click_lock_disabled_by_default, LITEST_MOUSE);
	litest_add_for_device(click_lock_hold_locks, LITEST_MOUSE);
	litest_add_for_device(click_lock_hold_locks, LITEST_LOGITECH_TRACKBALL);
	litest_add_for_device(click_lock_short_click, LITEST_MOUSE);
	litest_add_for_device(click_lock_move_cancels, LITEST_MOUSE);
	litest_add_for_device(click_lock_move_after_delay_cancels, LITEST_MOUSE);
	litest_add_for_device(click_lock_threshold, LITEST_MOUSE);
	litest_add_for_device(click_lock_threshold_zero, LITEST_MOUSE);
	litest_add_for_device(click_lock_timeout_zero, LITEST_MOUSE);
	litest_add_for_device(click_lock_timeout_change_while_pending, LITEST_MOUSE);
	litest_add_for_device(click_lock_other_button_releases, LITEST_MOUSE);
	litest_add_for_device(click_lock_other_button_while_pending, LITEST_MOUSE);
	litest_add_for_device(click_lock_other_button_already_down, LITEST_MOUSE);
	litest_add_no_device(click_lock_any_pointer_button_already_down);
	litest_add_for_device(click_lock_other_button_during_unlock_press, LITEST_MOUSE);
	litest_add_for_device(click_lock_seat_button_count, LITEST_MOUSE);

	litest_add_for_device(click_lock_unlisted_button_ignored, LITEST_MOUSE);
	litest_add_for_device(click_lock_listed_buttons_lock, LITEST_MOUSE);
	litest_add_for_device(click_lock_absent_button, LITEST_MOUSE);
	litest_add_for_device(click_lock_extra_buttons_lock, LITEST_MOUSE_ROCCAT);
	litest_add_for_device(click_lock_buttons_setting, LITEST_MOUSE);
	litest_add_for_device(click_lock_buttons_change_while_pending, LITEST_MOUSE);

	litest_add_for_device(click_lock_disable_releases, LITEST_MOUSE);
	litest_add_for_device(click_lock_disable_while_pending, LITEST_MOUSE);
	litest_add_for_device(click_lock_disable_during_unlock_click, LITEST_MOUSE);
	litest_add_for_device(click_lock_sendevents_releases, LITEST_MOUSE);
	litest_add_for_device(click_lock_sendevents_while_pending, LITEST_MOUSE);
	litest_add_for_device(click_lock_sendevents_during_unlock_press, LITEST_MOUSE);
	litest_add_no_device(click_lock_suspend_releases);
	litest_add_for_device(click_lock_device_removal_releases, LITEST_MOUSE);
	litest_add_for_device(click_lock_two_devices, LITEST_MOUSE);

	litest_add_for_device(click_lock_left_handed, LITEST_MOUSE);
	litest_add_for_device(click_lock_left_handed_while_locked, LITEST_MOUSE);
	litest_add_for_device(click_lock_scroll_method_while_locked, LITEST_MOUSE);
	litest_add_for_device(click_lock_scroll_button, LITEST_TRACKPOINT);
	litest_add_for_device(click_lock_scroll_button_held, LITEST_TRACKPOINT);
	litest_add_for_device(click_lock_middleemu_no_middle, LITEST_MOUSE);
	litest_add_for_device(click_lock_middleemu_middle_locks, LITEST_MOUSE);
	litest_add_for_device(click_lock_middleemu_overlapping_unlock, LITEST_MOUSE);
	/* clang-format on */
}
