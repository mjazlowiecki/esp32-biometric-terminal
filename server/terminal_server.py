import json
import struct
import zlib

import paho.mqtt.client as mqtt

BROKER = "127.0.0.1"  # bez localhost
RECORD_NO_CRC = struct.Struct("<IIHHBB2s")  # pierwsze 16 B rekordu

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
    status = "OK" if crc_ok(ev) else "ZLE CRC!"
    print(f"{device} #{ev['seq']:<4} user {ev['user']} akt {ev['act']} "
          f"akcja {ev['action']} CRC {status}")


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, 1883)
client.loop_forever()