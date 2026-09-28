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
// UTC offset in hours; adjust for your zone and daylight saving time
ntp_timeZoneOfs -7

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

// DP 115 - sensor test mode (0 off, 2 PIR test, 4 sound test, 5 light meter)
setChannelType 12 TextField
setChannelLabel 12 Test_mode
linkTuyaMCUOutputToChannel 115 enum 12

// DP 125 / 126 - light level and peak sound level
// (reported only while test mode is 5 or 3/4)
setChannelType 13 ReadOnly
setChannelLabel 13 Light_level
linkTuyaMCUOutputToChannel 125 val 13
setChannelType 14 ReadOnly
setChannelLabel 14 Sound_peak
linkTuyaMCUOutputToChannel 126 val 14

// DP 105 - status-colour LED level (red + 1/5 green, shows yellow)
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
