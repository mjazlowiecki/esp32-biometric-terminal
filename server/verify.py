"""Skrypt dla celów weryfikacyjnych. Celem jest sprawdzenie, czy rekordy w bazie - a te na karcie SD są 1:1 co do bitu identyczne."""
import sqlite3
import struct
import sys
import zlib

RECORD = struct.Struct("<IIHHBB2sI")  # 20 B jak event_record_t w event_store.h
CRC_LEN = 16                          # CRC liczone z pierwszych 16 B


def read_card(path):
    with open(path, "rb") as f:
        data = f.read()

    records, bad, repeated = {}, [], []
    for pos in range(len(data) // RECORD.size):
        chunk = data[pos * RECORD.size:(pos + 1) * RECORD.size]
        seq, ts, user, act, action, flags, _res, crc = RECORD.unpack(chunk)
        if zlib.crc32(chunk[:CRC_LEN]) != crc:
            bad.append(pos)
            continue
        if seq in records:
            repeated.append(seq)  # ponowienie numeru drugi raz na karcie (BAD)
        records[seq] = (ts, user, act, action, flags, crc)

    tail = len(data) % RECORD.size  # uciety (niepelny) rekord (BAD)
    return records, bad, repeated, tail

def read_db(path, device):
    db = sqlite3.connect(path)
    rows = db.execute("SELECT seq, ts, user, act, action, flags, crc FROM events "
                      "WHERE device = ?", (device,)).fetchall()
    return {r[0]: tuple(r[1:]) for r in rows}

def main():
    if len(sys.argv) != 4:
        print("Uzycie: verify.py <EVENTS.BIN> <events.db> <device_id>")
        sys.exit(2)

    card, bad, repeated, tail = read_card(sys.argv[1])
    db = read_db(sys.argv[2], sys.argv[3])

    missing = sorted(s for s in card if s not in db)
    extra = sorted(s for s in db if s not in card)
    differ = sorted(s for s in card if s in db and card[s] != db[s])

    print(f"Karta: {len(card)} poprawnych rekordow; uszkodzone: {len(bad)}; "
          f"powtorzone numery: {len(repeated)}; niepelny ogon: {tail} B")
    print(f"Baza:  {len(db)} wierszy dla terminala {sys.argv[3]}")
    print(f"Brakujace w bazie:  {len(missing)}  {missing[:20]}")
    print(f"Nadmiarowe w bazie: {len(extra)}  {extra[:20]}")
    print(f"Rozne pola:         {len(differ)}  {differ[:20]}")

    ok = not (missing or extra or differ or bad or repeated)
    print("WYNIK:", "ZGODNE co do bitu" if ok else "NIEZGODNOSCI!")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()