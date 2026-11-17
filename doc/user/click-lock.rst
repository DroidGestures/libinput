.. _click_lock:

==============================================================================
Click lock
==============================================================================

Click lock is an accessibility feature for users who cannot keep a button
pressed for the length of a drag. Where enabled, a button that is held
down without moving for a while stays logically down after it is
physically released. The user can then move the pointer, e.g. to drag a
window or select text, and click again to release the button. Windows
provides the same feature as ClickLock.

Click lock is available on devices with relative motion and a left button,
e.g. mice, trackballs and trackpoints, and on touchpads with physical
buttons or clickpads. It is not available on devices that also report an
absolute position, e.g. the virtual mouse of some virtual machines. It is
disabled by default.

------------------------------------------------------------------------------
Behavior
------------------------------------------------------------------------------

The exact behavior of click lock is implementation defined. As a general
rule, the following can be expected in the timeout mode:

- a button press starts a hold. If the button is released before the
  timeout, it is a normal click
- if the button is released after the timeout, the release is discarded and
  the button stays logically down, it is *locked*
- if the device moves further than the motion threshold from where the
  button was pressed before the button is released, the hold ends and the
  button is not locked, the press is a normal drag
- pressing another button during the hold ends the hold, that button's
  events are passed on. A button pressed while another button is already
  down does not start a hold
- once locked, motion does not release the button. The next button press
  on the same device releases it:

  - if it is a press of the locked button, the press and its release are
    discarded, i.e. the click that releases the lock is not a click of its
    own
  - if it is a press of another button, the locked button is released and
    the other button's press is passed on

- a locked button is also released, immediately, when

  - click lock is disabled or the locked button is removed from the
    configured buttons
  - the device is removed or disabled (see Send Events Mode in
    :ref:`config_options`), or libinput is suspended
  - a touchpad stops sending events for another reason, e.g. it is disabled
    while an external mouse is connected, the lid is closed, the device is
    in tablet mode or a tablet tool is in proximity (touch arbitration)

Click lock is per device. A button press on one device does not release a
button locked on another device. The exception is a touchpad and its
trackpoint, which count as one device, see below.

------------------------------------------------------------------------------
Configuration
------------------------------------------------------------------------------

libinput provides **libinput_device_config_click_lock_set_mode()** to
select the click lock mode, and
**libinput_device_config_click_lock_set_timeout()**,
**libinput_device_config_click_lock_set_buttons()** and
**libinput_device_config_click_lock_set_threshold()** for the options below.

Click lock has the following configuration options:

- the mode, disabled by default. The only other mode is the timeout mode
  described above, other modes may be added in the future
- the timeout, i.e. how long a button has to be held to lock, 1.2s by
  default. The maximum is implementation-defined, currently 10s. There is no
  minimum: with a timeout of 0 a button that is released without having
  moved past the motion threshold locks, however short the press. Every
  click then locks the button and the next click releases it, so user
  interfaces may want to restrict the timeout to e.g. 200ms and above
- the buttons that can be locked, as a list of evdev button codes, e.g.
  ``BTN_LEFT``, ``BTN_RIGHT``, ``BTN_MIDDLE`` or ``BTN_SIDE``. The default
  is ``BTN_LEFT`` only. The buttons are the button codes as reported in
  button events, i.e. with :ref:`left-handed mode <config_options>` enabled
  ``BTN_LEFT`` is the physical right button. It is not an error to give a
  button that the device does not have. With an empty list no button is
  locked
- the motion threshold in mm, 1mm by default. The maximum is
  implementation-defined, currently 20mm. With a threshold of 0 any motion
  while the button is held ends the hold. For devices that do not provide
  their resolution, e.g. most mice, libinput assumes a resolution of
  1000dpi. For a trackpoint the distance is calculated from the relative
  motion it reports, as for a mouse, it is not the distance the finger
  moves. On a clickpad the finger that presses the pad is pinned and does
  not count as motion until it has moved out of the pinned area

------------------------------------------------------------------------------
Interaction with other features
------------------------------------------------------------------------------

Buttons emulated by :ref:`tapping <tapping>` and :ref:`three-finger drag
<drag_3fg>` never lock, both have their own drag lock. A tap still releases
a locked button like any other button press.

If a button that click lock can lock is pressed on a touchpad or on its
trackpoint while tap-and-drag or three-finger drag holds that same button,
the drag is released first. Depending on timing this may look like a double
click. A drag that holds a different button is left alone.

The button used for :ref:`on-button scrolling <button_scrolling>` never
locks, but pressing it releases a locked button. While it is held down, no
other button starts a hold.

With :ref:`middle button emulation <middle_button_emulation>` enabled, an
emulated middle button can lock if the middle button is one of the buttons
click lock can lock.

On laptops with a trackpoint, e.g. Lenovo ThinkPads, the buttons above the
touchpad (physical or :ref:`top software buttons <t440_support>`) are buttons
of the trackpoint device and use the trackpoint's click lock configuration.
For click lock the touchpad and the trackpoint count as one device: motion
on the touchpad ends a hold of a trackpoint button and vice versa, and a
button press on either releases a button locked on the other.
