import json
import sqlite3
import os
import random
import struct
import zlib

import paho.mqtt.client as mqtt

BROKER = "127.0.0.1"  # bez localhost
RECORD_NO_CRC = struct.Struct("<IIHHBB2s")  # pierwsze 16 B rekordu

# Test odpornosci: ulamek ACK celowo pomijanych PO zapisie w bazie
# (symulacja awarii serwera miedzy COMMIT a ACK). 0 = normalna praca.
DROP_ACK = float(os.environ.get("DROP_ACK", "0"))

DB = sqlite3.connect("server/events.db")
DB.execute("""
    CREATE TABLE IF NOT EXISTS events (
        device  TEXT    NOT NULL,
        seq     INTEGER NOT NULL,
        ts      INTEGER NOT NULL,
        user    INTEGER NOT NULL,
        act     INTEGER NOT NULL,
        action  INTEGER NOT NULL,
        flags   INTEGER NOT NULL,
        crc     INTEGER NOT NULL,
        received_at TEXT DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (device, seq)
    )""")
DB.execute("""
    CREATE TABLE IF NOT EXISTS conflicts (
        device TEXT, seq INTEGER, ts INTEGER, user INTEGER, act INTEGER,
        action INTEGER, flags INTEGER, crc INTEGER,
        received_at TEXT DEFAULT CURRENT_TIMESTAMP
    )""")


def crc_ok(ev):
    raw = RECORD_NO_CRC.pack(ev["seq"], ev["ts"], ev["user"], ev["act"],
                             ev["action"], ev["flags"], b"\x00\x00")
    return zlib.crc32(raw) == ev["crc"]


def on_connect(client, userdata, flags, reason_code, properties):
    print("Polaczono z brokerem:", reason_code)
    client.subscribe("terminal/+/events", qos=1)  # + = dowolny terminal


def on_message(client, userdata, msg):
    device = msg.topic.split("/")[1]
    ev = json.loads(msg.payload)

    if not crc_ok(ev):
        print(f"{device} #{ev['seq']}: ZLE CRC - odrzucone, bez ACK")
        return  # terminal wysle ponownie

    cur = DB.execute(
        "INSERT OR IGNORE INTO events (device, seq, ts, user, act, action, flags, crc) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (device, ev["seq"], ev["ts"], ev["user"], ev["act"], ev["action"], ev["flags"], ev["crc"]))
    DB.commit()  # najpierw trwaly zapis...

    if cur.rowcount == 1:
        kind = "nowe"
    else:
        (stored_crc,) = DB.execute("SELECT crc FROM events WHERE device = ? AND seq = ?",
                                   (device, ev["seq"])).fetchone()
        if stored_crc == ev["crc"]:
            kind = "duplikat"
        else:
            DB.execute(
                "INSERT INTO conflicts (device, seq, ts, user, act, action, flags, crc) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (device, ev["seq"], ev["ts"], ev["user"], ev["act"], ev["action"],
                 ev["flags"], ev["crc"]))
            DB.commit()
            kind = "!!! KONFLIKT: inne zdarzenie z tym samym numerem - zapisane w 'conflicts'"
    print(f"{device} #{ev['seq']:<4} user {ev['user']} akt {ev['act']} "
          f"akcja {ev['action']} -> {kind}, ACK")
    
    if random.random() < DROP_ACK:
        print("    [TEST] ACK celowo pominiety")
        return
    client.publish(f"terminal/{device}/ack", str(ev["seq"]), qos=1)  # potem ACK - nawet dla duplikatu


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, 1883)
client.loop_forever()