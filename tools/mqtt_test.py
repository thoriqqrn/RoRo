#!/usr/bin/env python3
"""
Tester MQTT untuk RoRo (HiveMQ) — ukur kecepatan & simulasi event perangkat.

Tujuan:
  - Lihat event masuk realtime (seperti yang akan diterima mobile app).
  - Kirim event simulasi gas/sos/imu tanpa perlu hardware ESP32.
  - Ukur latency broker (round-trip publish -> broker -> subscribe).

Default kredensial diambil dari src/config.h. Bisa di-override via argumen.

Contoh pakai:
  # 1. Pantau semua event (jalankan di satu terminal)
  python3 tools/mqtt_test.py sub

  # 2. Kirim event simulasi (terminal lain) — mobile app harusnya update seketika
  python3 tools/mqtt_test.py pub gas on
  python3 tools/mqtt_test.py pub sos on
  python3 tools/mqtt_test.py pub imu jalan

  # 2b. Simulasi tombol ditekan SEBENTAR: kirim ON lalu otomatis OFF.
  #     SOS asli kirim OFF ~3 detik setelah tombol dilepas -> pakai --pulse (default 3s).
  python3 tools/mqtt_test.py pub sos on --pulse        # ON, tunggu 3s, OFF
  python3 tools/mqtt_test.py pub gas on --pulse 0.3    # tap gas singkat

  # 3. Ukur latency broker (round-trip, 20 sampel)
  python3 tools/mqtt_test.py ping -n 20
"""
import argparse
import json
import ssl
import statistics
import sys
import time

import paho.mqtt.client as mqtt

# ── Default dari src/config.h ──────────────────────────────────────────────
HOST = "746434cf770c4a868dc6863ed27b6d0a.s1.eu.hivemq.cloud"
PORT = 8883
USERNAME = "robotrollator"
PASSWORD = "Roro12345"
ROLLATOR_ID = "471grmOw38iBx5v5m9uC"

BASE = f"rollators/{ROLLATOR_ID}"


def make_client(client_id):
    c = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311,
                    callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    c.username_pw_set(USERNAME, PASSWORD)
    c.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    return c


def now_ms():
    return time.time() * 1000.0


# ── Mode: sub ──────────────────────────────────────────────────────────────
def cmd_sub(args):
    def on_connect(c, u, flags, rc, props=None):
        if rc == 0:
            print(f"[OK] Terhubung ke {HOST}:{PORT}. Subscribe {BASE}/#")
            c.subscribe(f"{BASE}/#", qos=0)
        else:
            print(f"[GAGAL] connect rc={rc}")

    def on_message(c, u, msg):
        recv = time.strftime("%H:%M:%S")
        try:
            payload = json.loads(msg.payload.decode())
        except Exception:
            payload = msg.payload.decode(errors="replace")
        topic = msg.topic.split("/")[-1]
        retain = " (retained)" if msg.retain else ""
        print(f"{recv}  [{topic:<6}]{retain}  {payload}")

    c = make_client("roro-tester-sub")
    c.on_connect = on_connect
    c.on_message = on_message
    c.connect(HOST, PORT, keepalive=30)
    print("Menunggu event... (Ctrl+C untuk berhenti)")
    c.loop_forever()


# ── Mode: pub ──────────────────────────────────────────────────────────────
def is_active(value):
    return value in ("on", "true", "1", "jalan", "walk")


def build_payload(event, active):
    """Bangun (topic, payload) untuk satu event pada state aktif/non-aktif."""
    if event == "gas":
        return f"{BASE}/gas", {"gas": active, "deviceMillis": int(now_ms())}
    if event == "sos":
        return f"{BASE}/sos", {"sos": active, "deviceMillis": int(now_ms())}
    if event == "imu":
        return f"{BASE}/imu", {"connected": True,
                               "status": "jalan" if active else "diam",
                               "walking": active, "pitch": 2.5, "roll": -1.1,
                               "motionScore": 1.8 if active else 0.3,
                               "deviceMillis": int(now_ms())}
    if event == "status":
        return f"{BASE}/status", {"online": active, "deviceMillis": int(now_ms())}
    raise ValueError(event)


def cmd_pub(args):
    active = is_active(args.value)
    retain = not args.no_retain

    c = make_client("roro-tester-pub")
    c.connect(HOST, PORT, keepalive=30)
    c.loop_start()

    # Kirim state utama (mis. ON)
    topic, payload = build_payload(args.event, active)
    c.publish(topic, json.dumps(payload), qos=args.qos, retain=retain).wait_for_publish(timeout=5)
    print(f"[KIRIM] {topic}  qos={args.qos}  retain={retain}")
    print(f"        {payload}")

    # --pulse: simulasi tombol ditekan sebentar lalu dilepas -> kirim state lawan.
    if args.pulse is not None:
        print(f"        ...tahan {args.pulse:.1f}s lalu kirim lawannya (tombol dilepas)")
        time.sleep(args.pulse)
        topic2, payload2 = build_payload(args.event, not active)
        c.publish(topic2, json.dumps(payload2), qos=args.qos, retain=retain).wait_for_publish(timeout=5)
        print(f"[KIRIM] {topic2}  (auto-balik)")
        print(f"        {payload2}")

    c.loop_stop()
    c.disconnect()


# ── Mode: ping (ukur latency round-trip) ───────────────────────────────────
def cmd_ping(args):
    topic = f"{BASE}/_latency_test"
    samples = []
    pending = {}

    def on_connect(c, u, flags, rc, props=None):
        c.subscribe(topic, qos=args.qos)

    def on_message(c, u, msg):
        try:
            seq = json.loads(msg.payload.decode())["seq"]
        except Exception:
            return
        if seq in pending:
            rtt = (now_ms() - pending.pop(seq))
            samples.append(rtt)
            print(f"  seq={seq:<3} rtt={rtt:6.1f} ms")

    c = make_client("roro-tester-ping")
    c.on_connect = on_connect
    c.on_message = on_message
    c.connect(HOST, PORT, keepalive=30)
    c.loop_start()
    time.sleep(1.0)  # tunggu subscribe aktif

    print(f"Ping {topic} ({args.n} sampel, qos={args.qos})...")
    for seq in range(args.n):
        pending[seq] = now_ms()
        c.publish(topic, json.dumps({"seq": seq}), qos=args.qos, retain=False)
        time.sleep(args.interval)

    time.sleep(1.0)  # tunggu sisa balasan
    c.loop_stop()
    c.disconnect()

    if samples:
        print("\n── Hasil latency broker (round-trip) ──")
        print(f"  sampel : {len(samples)}/{args.n}")
        print(f"  min    : {min(samples):6.1f} ms")
        print(f"  rata2  : {statistics.mean(samples):6.1f} ms")
        print(f"  median : {statistics.median(samples):6.1f} ms")
        print(f"  maks   : {max(samples):6.1f} ms")
        print("\nCatatan: ini latency publisher->broker->subscriber dari mesin ini.")
        print("Latency ke HP = angka ini + jaringan HP. Bandingkan dgn delay Firebase lama.")
    else:
        print("Tidak ada balasan diterima — cek koneksi/kredensial/izin topik.")


def main():
    p = argparse.ArgumentParser(description="Tester MQTT RoRo (HiveMQ)")
    sub = p.add_subparsers(dest="cmd", required=True)

    s_sub = sub.add_parser("sub", help="pantau semua event realtime")
    s_sub.set_defaults(func=cmd_sub)

    s_pub = sub.add_parser("pub", help="kirim event simulasi")
    s_pub.add_argument("event", choices=["gas", "sos", "imu", "status"])
    s_pub.add_argument("value", help="on/off | jalan/diam | true/false")
    s_pub.add_argument("--qos", type=int, default=0, choices=[0, 1])
    s_pub.add_argument("--no-retain", action="store_true", help="kirim tanpa retained")
    s_pub.add_argument("--pulse", type=float, nargs="?", const=3.0, default=None,
                       metavar="DETIK",
                       help="kirim state utama lalu otomatis kirim lawannya setelah DETIK "
                            "(default 3s = meniru tombol ditekan sebentar; SOS asli OFF ~3s "
                            "setelah dilepas)")
    s_pub.set_defaults(func=cmd_pub)

    s_ping = sub.add_parser("ping", help="ukur latency broker round-trip")
    s_ping.add_argument("-n", type=int, default=20, help="jumlah sampel")
    s_ping.add_argument("--interval", type=float, default=0.3, help="jeda antar ping (detik)")
    s_ping.add_argument("--qos", type=int, default=0, choices=[0, 1])
    s_ping.set_defaults(func=cmd_ping)

    args = p.parse_args()
    try:
        args.func(args)
    except KeyboardInterrupt:
        print("\nberhenti.")


if __name__ == "__main__":
    main()
