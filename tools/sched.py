#!/usr/bin/env python3
"""Encode and decode schedule slots for DPs 116-122 (firmware_spec.md §13.2).

Each slot is a DP string holding base64 of:

    hdr_id, len (= 4 + 9n), enabled, weekday_mask,
    n x { start_hi, start_lo, end_hi, end_lo, mode, off_mode, preset,
          autooff_hi, autooff_lo }

with at most 4 entries. A payload shorter than 4 bytes (such as the empty
string) disables the slot.

Examples:

  # Slot 0: every day, PIR night-light 22:00-06:00, off after 2 min idle
  tools/sched.py encode --slot 0 --days all \\
      --entry 22:00-06:00,mode=pir,off=auto,autooff=120,preset=2

  # Slot 1: weekdays, lamp on 07:00-08:30
  tools/sched.py encode --slot 1 --days weekdays \\
      --entry 07:00-08:30,mode=on,off=end

  # As the DP JSON a Tuya client sends: {"117": "..."}
  tools/sched.py encode --slot 1 --days mon,wed --entry 18:00-23:00,mode=on,off=end --json

  tools/sched.py clear --slot 3
  tools/sched.py decode AA0BfwAABZ8CAgIAPA==

Entry fields (after the time range):
  mode=on|pir          on: switch on at the start time; pir: PIR-controlled
  off=none|end|auto    end: switch off at the end time;
                       auto: off after `autooff` seconds without motion/sound
  autooff=SECONDS      0..65535 (default 60)
  preset=0|1|2         light gate: dark threshold DP 107 / 123 / 124
                       (defaults 10 / 20 / 100; 100 = always dark enough)

The end time is exclusive; an end before the start runs overnight into the
next day. 24:00 means midnight at the end of the day.
"""
import argparse
import base64
import json
import sys

DP_FIRST = 116
SLOTS = 7
MAX_ENTRIES = 4

DAYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
DAY_GROUPS = {
    "all": 0x7F,
    "weekdays": 0x3E,
    "weekends": 0x41,
    "none": 0x00,
}

MODES = {"on": 1, "pir": 2}
OFF_MODES = {"none": 0, "end": 1, "auto": 2}


def parse_days(text):
    mask = 0
    for part in text.lower().split(","):
        part = part.strip()
        if part in DAY_GROUPS:
            mask |= DAY_GROUPS[part]
        elif part[:3] in DAYS:
            mask |= 1 << DAYS.index(part[:3])
        else:
            raise ValueError(f"unknown day '{part}' (use sun..sat, all, weekdays, weekends)")
    return mask


def format_days(mask):
    for name, value in DAY_GROUPS.items():
        if mask == value:
            return name
    return ",".join(d for i, d in enumerate(DAYS) if mask & (1 << i))


def parse_time(text):
    try:
        hours, minutes = text.split(":")
        h, m = int(hours), int(minutes)
    except ValueError:
        raise ValueError(f"bad time '{text}' (use HH:MM)")
    if not (0 <= m < 60 and (0 <= h < 24 or (h == 24 and m == 0))):
        raise ValueError(f"time '{text}' out of range")
    return h * 60 + m


def format_time(minutes):
    return f"{minutes // 60:02d}:{minutes % 60:02d}"


def lookup(table, value, what):
    if value.isdigit():
        return int(value)
    try:
        return table[value.lower()]
    except KeyError:
        raise ValueError(f"unknown {what} '{value}' (use {'|'.join(table)})")


def parse_entry(text):
    parts = text.split(",")
    span = parts[0]
    if "-" not in span:
        raise ValueError(f"entry '{text}' must start with START-END")
    start_text, end_text = span.split("-", 1)
    entry = {
        "start": parse_time(start_text),
        "end": parse_time(end_text),
        "mode": MODES["pir"],
        "off": OFF_MODES["auto"],
        "autooff": 60,
        "preset": 2,
    }
    for part in parts[1:]:
        if "=" not in part:
            raise ValueError(f"expected key=value, got '{part}'")
        key, value = (s.strip() for s in part.split("=", 1))
        key = key.lower()
        if key == "mode":
            entry["mode"] = lookup(MODES, value, "mode")
        elif key == "off":
            entry["off"] = lookup(OFF_MODES, value, "off mode")
        elif key == "autooff":
            entry["autooff"] = int(value)
        elif key == "preset":
            entry["preset"] = int(value)
        else:
            raise ValueError(f"unknown field '{key}'")

    if entry["start"] == entry["end"]:
        raise ValueError(f"entry '{text}': start equals end, it would never match")
    if not 0 <= entry["autooff"] <= 0xFFFF:
        raise ValueError("autooff must be 0..65535 seconds")
    if entry["preset"] not in (0, 1, 2):
        raise ValueError("preset must be 0, 1 or 2")
    for key in ("mode", "off"):
        if not 0 <= entry[key] <= 0x0F:
            raise ValueError(f"{key} must be 0..15")
    return entry


def encode(hdr_id, enabled, mask, entries):
    if len(entries) > MAX_ENTRIES:
        raise ValueError(f"at most {MAX_ENTRIES} entries per slot")
    payload = bytearray([hdr_id, 4 + 9 * len(entries), enabled, mask])
    for e in entries:
        payload += bytes([
            e["start"] >> 8, e["start"] & 0xFF,
            e["end"] >> 8, e["end"] & 0xFF,
            e["mode"], e["off"], e["preset"],
            e["autooff"] >> 8, e["autooff"] & 0xFF,
        ])
    return bytes(payload)


def decode(payload):
    """Decode like the firmware (sched_decode): extra entries are ignored."""
    if len(payload) < 4:
        return None
    entries = []
    for i in range(min((len(payload) - 4) // 9, MAX_ENTRIES)):
        q = payload[4 + 9 * i:13 + 9 * i]
        entries.append({
            "start": (q[0] << 8) | q[1],
            "end": (q[2] << 8) | q[3],
            "mode": q[4],
            "off": q[5],
            "preset": q[6],
            "autooff": (q[7] << 8) | q[8],
        })
    return {"hdr_id": payload[0], "len": payload[1], "enabled": payload[2],
            "mask": payload[3], "entries": entries}


def name_of(table, value):
    for name, v in table.items():
        if v == value:
            return name
    return str(value)


def describe(slot):
    if slot is None:
        return "slot disabled (payload shorter than 4 bytes)"
    lines = [f"id {slot['hdr_id']}, {'enabled' if slot['enabled'] else 'disabled'}, "
             f"days {format_days(slot['mask'])} (0x{slot['mask']:02X})"]
    if slot["len"] != 4 + 9 * len(slot["entries"]):
        lines.append(f"  note: length byte {slot['len']} does not match the entry count")
    for e in slot["entries"]:
        overnight = " (overnight)" if e["end"] < e["start"] else ""
        autooff = f" autooff={e['autooff']}s" if e["off"] == OFF_MODES["auto"] else ""
        lines.append(
            f"  {format_time(e['start'])}-{format_time(e['end'])}{overnight}: "
            f"mode={name_of(MODES, e['mode'])} off={name_of(OFF_MODES, e['off'])}"
            f"{autooff} preset={e['preset']}")
    return "\n".join(lines)


def output(slot, text, as_json):
    dp = DP_FIRST + slot
    if as_json:
        print(json.dumps({str(dp): text}), flush=True)
    else:
        print(f"DP {dp}: {text}", flush=True)


def check_slot(slot):
    if not 0 <= slot < SLOTS:
        raise ValueError(f"slot must be 0..{SLOTS - 1} (DP {DP_FIRST}..{DP_FIRST + SLOTS - 1})")


def cmd_encode(args):
    check_slot(args.slot)
    entries = [parse_entry(e) for e in args.entry]
    hdr_id = args.slot if args.id is None else args.id
    if not 0 <= hdr_id <= 0xFF:
        raise ValueError("id must be 0..255")
    payload = encode(hdr_id, 0 if args.disabled else 1, parse_days(args.days), entries)
    text = base64.b64encode(payload).decode()
    output(args.slot, text, args.json)
    if not args.json:
        print(describe(decode(payload)), file=sys.stderr)


def cmd_clear(args):
    check_slot(args.slot)
    output(args.slot, "", args.json)


def cmd_decode(args):
    try:
        payload = base64.b64decode(args.text, validate=True)
    except ValueError:
        raise ValueError("not valid base64")
    print(describe(decode(payload)))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("encode", help="build a slot's DP string")
    p.add_argument("--slot", type=int, required=True, help="slot 0..6 (DP 116..122)")
    p.add_argument("--days", default="all", help="e.g. mon,tue or all, weekdays, weekends")
    p.add_argument("--entry", action="append", default=[], metavar="START-END[,key=value...]",
                   help="schedule entry; repeat up to 4 times")
    p.add_argument("--id", type=int, help="header id byte (default: slot number)")
    p.add_argument("--disabled", action="store_true", help="store the slot disabled")
    p.add_argument("--json", action="store_true", help='print {"<dp>": "<base64>"}')
    p.set_defaults(func=cmd_encode)

    p = sub.add_parser("clear", help="DP string that disables a slot")
    p.add_argument("--slot", type=int, required=True, help="slot 0..6 (DP 116..122)")
    p.add_argument("--json", action="store_true", help='print {"<dp>": ""}')
    p.set_defaults(func=cmd_clear)

    p = sub.add_parser("decode", help="show what a DP string contains")
    p.add_argument("text", help="base64 DP value")
    p.set_defaults(func=cmd_decode)

    args = parser.parse_args()
    try:
        args.func(args)
    except ValueError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
