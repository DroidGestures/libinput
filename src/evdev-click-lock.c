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

#include "config.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "evdev.h"

#define DEFAULT_CLICK_LOCK_TIMEOUT usec_from_millis(1200)
#define MAX_CLICK_LOCK_TIMEOUT usec_from_millis(10000)
#define DEFAULT_CLICK_LOCK_THRESHOLD 1.0 /* mm */
#define MAX_CLICK_LOCK_THRESHOLD 20.0    /* mm */

/*
 * Click lock: a button held without moving for longer than the timeout
 * stays logically down after the physical release. The next button press
 * on this device releases it.
 *
 * The filter sits in evdev_pointer_notify_button(), after left-handed
 * mapping and middle button emulation and before the key down count, so
 * it sees the logical buttons and a swallowed release leaves the button
 * down for the rest of libinput. A locked button is released through
 * evdev_pointer_post_button(), below the filter.
 *
 * States, per device:
 *
 *             press of a button that can lock,
 *             no other button down
 *    IDLE ------------------------------------> PENDING
 *     ^ ^ ^                                      |  |
 *     | | |  release before the timeout, motion  |  |
 *     | | |  past the threshold, another press   |  |
 *     | | +--------------------------------------+  |
 *     | |                                           | release at or after
 *     | |    press of another button:               | the timeout:
 *     | |    locked button released                 | swallowed
 *     | +--------------------------------------- LOCKED
 *     |                                             |
 *     | release of that press:                      | press of the locked
 *     | swallowed                                   | button: swallowed,
 *     |                                             | locked button released
 *    UNLOCKING <------------------------------------+
 *
 * - IDLE: nothing held
 * - PENDING: an eligible button is held
 * - LOCKED: the physical release was swallowed, the button is logically
 *   down
 * - UNLOCKING: the press that released the lock was swallowed and is
 *   still down. Its release is swallowed too, the click that unlocks is
 *   not a click of its own.
 *
 * Whether a release locks is decided on the event timestamps: a release
 * at or after press time + timeout locks. There is no timer, nothing
 * happens at the timeout itself. With a timeout of 0 every release of a
 * button that is still PENDING locks.
 *
 * Disabling click lock, taking the button out of the buttons that can
 * lock, suspending the device and the like go from PENDING and LOCKED to
 * IDLE and release a locked button. UNLOCKING stays, the release of that
 * press is swallowed even if click lock was disabled in between. Only
 * where the device's buttons are reset and that release is never going
 * to come does UNLOCKING go to IDLE as well.
 *
 * No hold starts while another button is down, and that includes the
 * swallowed press in UNLOCKING. So there is never more than one swallowed
 * press, remembered by its button and its source: a physical button and a
 * tap of the same button can overlap, and a tap during a swallowed
 * physical press must leave the release of that press swallowed.
 *
 * Two presses of one button can overlap within a source too, e.g. the
 * middle button and the middle button emulated from left and right. They
 * are not told apart: the first release of that button is the swallowed
 * one, the button is then up with the last release, as it is for any two
 * overlapping presses of one button.
 *
 * Tapping and three-finger drag have drag locks of their own. A press of
 * a click lock button that such a drag holds releases the drag first.
 * Otherwise the press would not change the button's down count and be
 * lost.
 */

static inline const char *
click_lock_state_to_str(enum evdev_click_lock_state state)
{
	switch (state) {
	CASE_RETURN_STRING(CLICK_LOCK_IDLE);
	CASE_RETURN_STRING(CLICK_LOCK_PENDING);
	CASE_RETURN_STRING(CLICK_LOCK_LOCKED);
	CASE_RETURN_STRING(CLICK_LOCK_UNLOCKING);
	}

	return NULL;
}

static inline bool
click_lock_buttons_contain(const uint32_t *buttons, size_t nbuttons, uint32_t code)
{
	for (size_t i = 0; i < nbuttons; i++) {
		if (buttons[i] == code)
			return true;
	}

	return false;
}

static void
click_lock_set_state(struct evdev_device *device, enum evdev_click_lock_state state)
{
	evdev_log_debug(device,
			"click lock: %s → %s (%s)\n",
			click_lock_state_to_str(device->click_lock.state),
			click_lock_state_to_str(state),
			evdev_usage_code_name(device->click_lock.button));

	device->click_lock.state = state;
}

static void
click_lock_unlock(struct evdev_device *device, usec_t time)
{
	evdev_usage_t button = device->click_lock.button;

	/* Clear the state first, posting the release runs the deferred
	 * left-handed and scroll method changes */
	click_lock_set_state(device, CLICK_LOCK_IDLE);
	evdev_pointer_post_button(device, time, button, LIBINPUT_BUTTON_STATE_RELEASED);
}

/* Ends whatever click lock holds on this device: a locked button is
 * released now, a button that is still physically down gets its own
 * release later and is simply no longer tracked. A swallowed press that
 * is still down is not held by click lock, its release stays swallowed. */
void
evdev_click_lock_release(struct evdev_device *device, usec_t time)
{
	switch (device->click_lock.state) {
	case CLICK_LOCK_IDLE:
	case CLICK_LOCK_UNLOCKING:
		break;
	case CLICK_LOCK_PENDING:
		click_lock_set_state(device, CLICK_LOCK_IDLE);
		break;
	case CLICK_LOCK_LOCKED:
		click_lock_unlock(device, time);
		break;
	}
}

/* Ends a hold but leaves a locked button alone */
void
evdev_click_lock_cancel_hold(struct evdev_device *device)
{
	if (device->click_lock.state == CLICK_LOCK_PENDING)
		click_lock_set_state(device, CLICK_LOCK_IDLE);
}

/* The physical buttons were reset while a swallowed press was down, e.g.
 * on suspend: its release is not going to come, so the next release of
 * that button belongs to a new press. */
void
evdev_click_lock_forget_buttons(struct evdev_device *device)
{
	if (device->click_lock.state == CLICK_LOCK_UNLOCKING)
		click_lock_set_state(device, CLICK_LOCK_IDLE);
}

static bool
click_lock_has_button(struct evdev_device *device, evdev_usage_t button)
{
	return device->click_lock.mode != LIBINPUT_CONFIG_CLICK_LOCK_DISABLED &&
	       evdev_usage_type(button) == EV_KEY &&
	       click_lock_buttons_contain(device->click_lock.buttons,
					  device->click_lock.nbuttons,
					  evdev_usage_code(button));
}

static bool
click_lock_any_button_down(const struct evdev_device *device)
{
	for (unsigned int code = BTN_MISC; code <= KEY_MAX; code++) {
		if (device->key_count[code] != 0 &&
		    evdev_usage_is_button(evdev_usage_from_code(EV_KEY, code)))
			return true;
	}

	/* The on-button scroll button is held. Its press is only posted
	 * together with its release, so it is not in the down counts. */
	if (device->scroll.method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN &&
	    device->scroll.button_scroll_state != BUTTONSCROLL_IDLE)
		return true;

	/* The press that released a lock is still down. It was swallowed,
	 * so it is not in the down counts either. */
	if (device->click_lock.state == CLICK_LOCK_UNLOCKING)
		return true;

	return false;
}

static bool
click_lock_is_eligible(struct evdev_device *device, evdev_usage_t button)
{
	if (!click_lock_has_button(device, button))
		return false;

	/* The on-button scroll button never locks */
	if (device->scroll.method == LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN &&
	    evdev_usage_cmp(button, device->scroll.button) == 0)
		return false;

	/* A button is down already. Either this one, e.g. through a tap,
	 * and the press does not change the logical button state. Or
	 * another one: a second button is not a hold, the same as a button
	 * pressed during a hold ends it. */
	if (click_lock_any_button_down(device))
		return false;

	return true;
}

static void
click_lock_start(struct evdev_device *device, usec_t time, evdev_usage_t button)
{
	device->click_lock.button = button;
	device->click_lock.deadline = usec_add(time, device->click_lock.timeout);
	device->click_lock.moved = (struct phys_coords){ 0.0, 0.0 };
	click_lock_set_state(device, CLICK_LOCK_PENDING);
}

/* The press that released a lock is not a click of its own, it is
 * swallowed together with its release */
static void
click_lock_swallow_press(struct evdev_device *device,
			 evdev_usage_t button,
			 enum evdev_click_lock_source source)
{
	device->click_lock.button = button;
	device->click_lock.source = source;
	click_lock_set_state(device, CLICK_LOCK_UNLOCKING);
}

static void
click_lock_release_drags(struct evdev_device *device, usec_t time, evdev_usage_t button)
{
	if (device->click_lock.release_drags)
		device->click_lock.release_drags(device, time, button);
}

/**
 * Filter a button event through click lock.
 *
 * @param source Presses from CLICK_LOCK_SOURCE_GESTURE never start a
 * hold, tapping and three-finger drag have their own drag lock. Such
 * presses still end a hold or a lock.
 *
 * @return true if the event is swallowed
 */
bool
evdev_click_lock_filter_button(struct evdev_device *device,
			       usec_t time,
			       evdev_usage_t button,
			       enum libinput_button_state state,
			       enum evdev_click_lock_source source)
{
	bool is_press = state == LIBINPUT_BUTTON_STATE_PRESSED;
	bool may_hold = source == CLICK_LOCK_SOURCE_BUTTON;

	if (is_press && may_hold && click_lock_has_button(device, button))
		click_lock_release_drags(device, time, button);

	switch (device->click_lock.state) {
	case CLICK_LOCK_IDLE:
		if (is_press && may_hold && click_lock_is_eligible(device, button))
			click_lock_start(device, time, button);
		return false;
	case CLICK_LOCK_PENDING:
		/* Another press ends the hold, its events pass through */
		if (is_press) {
			click_lock_set_state(device, CLICK_LOCK_IDLE);
			return false;
		}

		if (evdev_usage_cmp(button, device->click_lock.button) != 0)
			return false;

		/* Released before the timeout: a normal click */
		if (usec_cmp(time, device->click_lock.deadline) < 0) {
			click_lock_set_state(device, CLICK_LOCK_IDLE);
			return false;
		}

		click_lock_set_state(device, CLICK_LOCK_LOCKED);
		return true;
	case CLICK_LOCK_LOCKED:
		if (is_press) {
			bool same =
				evdev_usage_cmp(button, device->click_lock.button) == 0;

			click_lock_unlock(device, time);

			if (same) {
				click_lock_swallow_press(device, button, source);
				return true;
			}
			return false;
		}

		/* The locked button is physically up and no hold starts
		 * while a button is down, so there is no press this release
		 * could belong to. Let it end the lock, the button is up
		 * for the rest of libinput after it. */
		if (evdev_usage_cmp(button, device->click_lock.button) == 0) {
			evdev_log_bug_libinput(
				device,
				"click lock: release of locked button %s\n",
				evdev_usage_code_name(button));
			click_lock_set_state(device, CLICK_LOCK_IDLE);
		}
		return false;
	case CLICK_LOCK_UNLOCKING:
		/* Only the release of the swallowed press is swallowed, a tap
		 * of the same button during a physical press is not it */
		if (!is_press && source == device->click_lock.source &&
		    evdev_usage_cmp(button, device->click_lock.button) == 0) {
			click_lock_set_state(device, CLICK_LOCK_IDLE);
			return true;
		}
		return false;
	}

	return false;
}

void
evdev_click_lock_handle_motion(struct evdev_device *device,
			       const struct phys_coords *delta)
{
	if (device->click_lock.state != CLICK_LOCK_PENDING)
		return;

	/* The distance from where the button was pressed, so moving back
	 * and forth within the threshold keeps the hold */
	device->click_lock.moved.x += delta->x;
	device->click_lock.moved.y += delta->y;

	if (length_in_mm(device->click_lock.moved) > device->click_lock.threshold) {
		evdev_log_debug(device, "click lock: moved, not locking\n");
		click_lock_set_state(device, CLICK_LOCK_IDLE);
	}
}

static uint32_t
click_lock_get_modes(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_CLICK_LOCK_TIMEOUT;
}

static enum libinput_config_status
click_lock_set_mode(struct libinput_device *device,
		    enum libinput_config_click_lock_mode mode)
{
	struct evdev_device *evdev = evdev_device(device);

	evdev->click_lock.mode = mode;

	/* Not deferred until the buttons are up: a locked button is down
	 * by definition and turning the feature off must release it. There
	 * is no event to take the time of that release from. */
	if (mode == LIBINPUT_CONFIG_CLICK_LOCK_DISABLED)
		evdev_click_lock_release(evdev,
					 libinput_now(evdev_libinput_context(evdev)));

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_click_lock_mode
click_lock_get_mode(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);

	return evdev->click_lock.mode;
}

static enum libinput_config_click_lock_mode
click_lock_get_default_mode(struct libinput_device *device)
{
	return LIBINPUT_CONFIG_CLICK_LOCK_DISABLED;
}

static enum libinput_config_status
click_lock_set_timeout(struct libinput_device *device, usec_t timeout)
{
	struct evdev_device *evdev = evdev_device(device);

	if (usec_cmp(timeout, MAX_CLICK_LOCK_TIMEOUT) > 0)
		return LIBINPUT_CONFIG_STATUS_INVALID;

	/* A hold in progress keeps its deadline */
	evdev->click_lock.timeout = timeout;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static usec_t
click_lock_get_timeout(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);

	return evdev->click_lock.timeout;
}

static usec_t
click_lock_get_default_timeout(struct libinput_device *device)
{
	return DEFAULT_CLICK_LOCK_TIMEOUT;
}

static const uint32_t click_lock_default_buttons[] = { BTN_LEFT };

static size_t
click_lock_copy_buttons(const uint32_t *from,
			size_t nfrom,
			uint32_t *buttons,
			size_t nbuttons)
{
	size_t count = min(nfrom, nbuttons);

	memcpy(buttons, from, count * sizeof(*buttons));

	return count;
}

static enum libinput_config_status
click_lock_set_buttons(struct libinput_device *device,
		       const uint32_t *buttons,
		       size_t nbuttons)
{
	struct evdev_device *evdev = evdev_device(device);
	uint32_t list[CLICK_LOCK_MAX_BUTTONS];
	size_t count = 0;

	/* A button the device does not have is fine, it stays in the list
	 * and never sends an event */
	for (size_t i = 0; i < nbuttons; i++) {
		uint32_t code = buttons[i];

		if (code > KEY_MAX ||
		    !evdev_usage_is_button(evdev_usage_from_code(EV_KEY, code)))
			return LIBINPUT_CONFIG_STATUS_INVALID;

		if (click_lock_buttons_contain(list, count, code))
			continue;

		if (count == ARRAY_LENGTH(list))
			return LIBINPUT_CONFIG_STATUS_INVALID;

		list[count++] = code;
	}

	memcpy(evdev->click_lock.buttons, list, count * sizeof(*list));
	evdev->click_lock.nbuttons = count;

	/* Like disabling for the button that was taken out, the release
	 * has no event to take its time from either */
	if ((evdev->click_lock.state == CLICK_LOCK_PENDING ||
	     evdev->click_lock.state == CLICK_LOCK_LOCKED) &&
	    !click_lock_buttons_contain(list,
					count,
					evdev_usage_code(evdev->click_lock.button)))
		evdev_click_lock_release(evdev,
					 libinput_now(evdev_libinput_context(evdev)));

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static size_t
click_lock_get_buttons(struct libinput_device *device,
		       uint32_t *buttons,
		       size_t nbuttons)
{
	struct evdev_device *evdev = evdev_device(device);

	return click_lock_copy_buttons(evdev->click_lock.buttons,
				       evdev->click_lock.nbuttons,
				       buttons,
				       nbuttons);
}

static size_t
click_lock_get_default_buttons(struct libinput_device *device,
			       uint32_t *buttons,
			       size_t nbuttons)
{
	return click_lock_copy_buttons(click_lock_default_buttons,
				       ARRAY_LENGTH(click_lock_default_buttons),
				       buttons,
				       nbuttons);
}

static enum libinput_config_status
click_lock_set_threshold(struct libinput_device *device, double mm)
{
	struct evdev_device *evdev = evdev_device(device);

	if (!isfinite(mm) || mm < 0.0 || mm > MAX_CLICK_LOCK_THRESHOLD)
		return LIBINPUT_CONFIG_STATUS_INVALID;

	evdev->click_lock.threshold = mm;

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static double
click_lock_get_threshold(struct libinput_device *device)
{
	struct evdev_device *evdev = evdev_device(device);

	return evdev->click_lock.threshold;
}

static double
click_lock_get_default_threshold(struct libinput_device *device)
{
	return DEFAULT_CLICK_LOCK_THRESHOLD;
}

void
evdev_init_click_lock(struct evdev_device *device)
{
	device->click_lock.mode = LIBINPUT_CONFIG_CLICK_LOCK_DISABLED;
	device->click_lock.timeout = DEFAULT_CLICK_LOCK_TIMEOUT;
	device->click_lock.nbuttons =
		click_lock_copy_buttons(click_lock_default_buttons,
					ARRAY_LENGTH(click_lock_default_buttons),
					device->click_lock.buttons,
					ARRAY_LENGTH(device->click_lock.buttons));
	device->click_lock.threshold = DEFAULT_CLICK_LOCK_THRESHOLD;
	device->click_lock.state = CLICK_LOCK_IDLE;

	device->click_lock.config.get_modes = click_lock_get_modes;
	device->click_lock.config.set_mode = click_lock_set_mode;
	device->click_lock.config.get_mode = click_lock_get_mode;
	device->click_lock.config.get_default_mode = click_lock_get_default_mode;
	device->click_lock.config.set_timeout = click_lock_set_timeout;
	device->click_lock.config.get_timeout = click_lock_get_timeout;
	device->click_lock.config.get_default_timeout = click_lock_get_default_timeout;
	device->click_lock.config.set_buttons = click_lock_set_buttons;
	device->click_lock.config.get_buttons = click_lock_get_buttons;
	device->click_lock.config.get_default_buttons = click_lock_get_default_buttons;
	device->click_lock.config.set_threshold = click_lock_set_threshold;
	device->click_lock.config.get_threshold = click_lock_get_threshold;
	device->click_lock.config.get_default_threshold =
		click_lock_get_default_threshold;
	device->base.config.click_lock = &device->click_lock.config;
}
