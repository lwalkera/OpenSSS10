# OpenBeken patches: TuyaMCU firmware update

Patches for [OpenBK7231T_App](https://github.com/openshwprojects/OpenBK7231T_App)
that let OpenBeken update the MCU's firmware over the standard Tuya MCU
upgrade protocol (commands 0x0A/0x0B), so the switch can be updated in the
wall without SWD.

| Patch | Contents |
|---|---|
| `0001-…` | Fix: the length of frames with payloads over 255 bytes lost its high byte, so such frames were split (and parts of their payload parsed as packets) or discarded |
| `0002-…` | `tuyaMcu_otaStart` / `tuyaMcu_otaAbort` / `tuyaMcu_otaStatus`, progress on the main page, and the simulator test `Test_TuyaMCU_OTA` |
| `0003-…` | Send OTA packets without blocking the quick tick: blocking writes tripped the BK7231N's hardware watchdog. Other TuyaMCU traffic is held back during the transfer |

Made against upstream commit `d6641ad` (5 Oct 2026). Patch 0001 is also on
its own in the branch `tuyamcu-frame-length-fix` of
https://github.com/lwalkera/OpenBK7231T_App, for a separate pull request.

## Build and test

Apply them to a clone, e.g. `git am openbeken/patches/*.patch`. Run the
simulator tests the way OpenBeken's CI does (Ubuntu 22.04; newer glibc
versions clash with OpenBeken's `strcasestr` declaration):

    git submodule update --init libraries/berry
    mkdir -p libraries/berry/generate
    ./libraries/berry/tools/coc/coc -o libraries/berry/generate libraries/berry/src src/berry/modules -c include/berry_conf.h
    make -f custom.mk
    ./build/win_main -runUnitTests 2     # exit code = number of failed assertions

Firmware for the BK7231N comes from OpenBeken's online builds: push the
branch to a fork of OpenBK7231T_App and open a pull request (a draft is
fine). GitHub Actions attaches the binaries for every platform to the run.

## Use

After flashing OpenBeken with these patches, enlarge its LittleFS once. The
default 32 KB can't hold a 20 KB image next to `autoexec.bat`: littlefs needs
a spare block, and when it runs out of space the upload crashes OpenBeken.
Formatting erases the files, so save `autoexec.bat` first and upload it again
afterwards:

    curl -o autoexec.bat http://<device address>/api/lfs/autoexec.bat
    curl "http://<device address>/cm?cmnd=lfs_format%200x10000"
    curl --data-binary @autoexec.bat http://<device address>/api/lfs/autoexec.bat

Then:

    make ota IP=<device address>

This uploads `build/pir-switch.bin` to the device's LittleFS, runs
`tuyaMcu_otaStart mcu.bin` and follows the progress (see `tools/ota.py`).
The upload is paced at about 1.5 KB/s. OpenBeken writes it to flash at
about 2.7 KB/s and buffers anything faster in heap. A fast upload drops free
heap below 25 KB, and OpenBeken reboots if it stays that low for 5 s. The
whole update takes about a minute.
