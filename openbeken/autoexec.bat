// OpenBeken autoexec.bat for the FT32F030C8 TuyaMCU PIR/TRIAC switch
// (product key urlxtrtlq9fjkv2d, MCU firmware 2.3.4)
// Use this instead of the template's short startup command, not in addition to it.

startDriver TuyaMCU
tuyaMcu_setBaudRate 9600
// The MCU must always see "connected to cloud" (status 4), otherwise it
// blinks for pairing and resets the Wi-Fi module after ~10 min.
tuyaMcu_defWiFiState 4
// Local time is needed for the MCU's schedules (it requests cmd 0x1C).
startDriver NTP
// Standard-time UTC offset in hours; adjust for your zone
ntp_timeZoneOfs -8
// Daylight saving time rules (needs openbeken/patches/0003), two rules of
//   nthWeek(0 = last) month weekday(1 = Sunday) hour minutesOfDSTafter
// in local time. Here (most of the US and Canada): DST starts the second
// Sunday of March at 2:00 and ends the first Sunday of November at 2:00.
// EU: time_setDST 0 3 1 2 60 0 10 1 3 0 (with the zone's standard offset)
time_setDST 2 3 1 2 60 1 11 1 2 0

// DP 1 - lamp (TRIAC)
setChannelType 1 Toggle
setChannelLabel 1 Lamp
linkTuyaMCUOutputToChannel 1 bool 1

// DP 101 / 102 - night-light brightness 0..100
setChannelType 2 Dimmer
setChannelLabel 2 White_LED
linkTuyaMCUOutputToChannel 101 val 2
setChannelType 3 Dimmer
setChannelLabel 3 Cyan_LED
linkTuyaMCUOutputToChannel 102 val 3

// DP 127 - LED brightness follows ambient light
setChannelType 4 Toggle
setChannelLabel 4 Auto_LED_dim
linkTuyaMCUOutputToChannel 127 bool 4

// DP 48 - PIR sensitivity 0..2 (enum)
setChannelType 5 TextField
setChannelLabel 5 PIR_sensitivity
linkTuyaMCUOutputToChannel 48 enum 5

// DP 7 - countdown, seconds
setChannelType 6 TextField
setChannelLabel 6 Countdown_s
linkTuyaMCUOutputToChannel 7 val 6

// DP 110 - lamp state when countdown ends
setChannelType 7 Toggle
setChannelLabel 7 Countdown_end_state
linkTuyaMCUOutputToChannel 110 bool 7

// DP 50 - PIR hold-off after manual off, seconds
setChannelType 8 TextField
setChannelLabel 8 Holdoff_s
linkTuyaMCUOutputToChannel 50 val 8

// DP 103 / 104 - lamp fade in / out (0 instant, 1 ~0.8 s, 2 ~6.7 s, 3 ~20 s)
setChannelType 9 TextField
setChannelLabel 9 Fade_in
linkTuyaMCUOutputToChannel 103 enum 9
setChannelType 10 TextField
setChannelLabel 10 Fade_out
linkTuyaMCUOutputToChannel 104 enum 10

// DP 106 - sound trigger threshold 20..100 (100 = off)
setChannelType 11 TextField
setChannelLabel 11 Sound_threshold
linkTuyaMCUOutputToChannel 106 val 11

// DP 115 (sensor test mode) and DP 125 / 126 (light level, sound peak) are
// left out: they are for setting up, which the web app's SSS10 tab does, and
// would only clutter Home Assistant. Channels 12-14 are unused.

// DP 105 - status-color LED level (red + 1/5 green, shows yellow)
setChannelType 15 Dimmer
setChannelLabel 15 Status_LED
linkTuyaMCUOutputToChannel 105 val 15

// DP 107 / 123 / 124 - dark thresholds for schedule light presets 0 / 1 / 2
// (light level 0..100 at or below which the room counts as dark;
// defaults 10 / 20 / 100; the built-in PIR night-light uses preset 2)
setChannelType 16 TextField
setChannelLabel 16 Dark_threshold_0
linkTuyaMCUOutputToChannel 107 val 16
setChannelType 17 TextField
setChannelLabel 17 Dark_threshold_1
linkTuyaMCUOutputToChannel 123 val 17
setChannelType 18 TextField
setChannelLabel 18 Dark_threshold_2
linkTuyaMCUOutputToChannel 124 val 18

// DP 128-131 - night-light levels 0..100: white, red, green, blue
// (new in this firmware; used by schedule entries in night-light mode)
setChannelType 19 Dimmer
setChannelLabel 19 Nightlight_white
linkTuyaMCUOutputToChannel 128 val 19
setChannelType 20 Dimmer
setChannelLabel 20 Nightlight_red
linkTuyaMCUOutputToChannel 129 val 20
setChannelType 21 Dimmer
setChannelLabel 21 Nightlight_green
linkTuyaMCUOutputToChannel 130 val 21
setChannelType 22 Dimmer
setChannelLabel 22 Nightlight_blue
linkTuyaMCUOutputToChannel 131 val 22

// DP 132 - restart request: the MCU reports 1 when the button is held 15 s
// (its reset line does not restart this module), then restarts itself on
// release. No channel type, so Home Assistant doesn't show it. The channel
// goes back to 0 first so the next request is a change again.
linkTuyaMCUOutputToChannel 132 bool 23
addChangeHandler Channel23 == 1 backlog setChannel 23 0; reboot
