#!/usr/bin.env python3
"""Podglad dziennika zdarzen terminala (events.bin z karty SD)."""

import struct
import sys
import zlib
from datetime import datetime, timezone

# Uklad musi odpowiadac event_record_t w event_store.h
RECORD = struct.Struct("<IIHHBB2sI")  # 20 B, little endian
CRC_LEN = 16                          # offsetof(event_record_t, crc)
ACTIONS = {1: "START", 2: "STOP"}
FLAG_TIME_SYNCED = 0x01

def main(path):
    with open(path, "rb") as f:
        data = f.read()

    count, tail = divmod(len(data), RECORD.size)
    print(f"{path}: {len(data)} B, {count} rekordow", end="")
    print(f", {tail} B ucietej koncowki!" if tail else "")
    print(f"{'seq':>6}  {'czas lokalny':19}  {'user':>4}  {'akt':>4}  {'akcja':5}  synch  CRC")

    for i in range(count):
        chunk = data[i * RECORD.size:(i + 1) * RECORD.size]
        seq, ts, user, act, action, flags, _reserved, crc = RECORD.unpack(chunk)

        crc_ok = zlib.crc32(chunk[:CRC_LEN]) == crc
        when = datetime.fromtimestamp(ts, timezone.utc).astimezone()
        synced = "tak" if flags & FLAG_TIME_SYNCED else "NIE"

        print(f"{seq:>6}  {when:%Y-%m-%d %H:%M:%S}  {user:>4}  {act:>4}  "
              f"{ACTIONS.get(action, '?'):5}  {synced:5}  {'OK' if crc_ok else 'ZLY!'}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Uzycie: python events_dump.py <sciezka do events.bin>")
    main(sys.argv[1])