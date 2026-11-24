import json
import sqlite3
import struct
import zlib

import paho.mqtt.client as mqtt

BROKER = "127.0.0.1"  # bez localhost
RECORD_NO_CRC = struct.Struct("<IIHHBB2s")  # pierwsze 16 B rekordu

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

    kind = "nowe" if cur.rowcount == 1 else "duplikat"
    print(f"{device} #{ev['seq']:<4} user {ev['user']} akt {ev['act']} "
          f"akcja {ev['action']} -> {kind}, ACK")
    client.publish(f"terminal/{device}/ack", str(ev["seq"]), qos=1)  # potem ACK - nawet dla duplikatu


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, 1883)
client.loop_forever()