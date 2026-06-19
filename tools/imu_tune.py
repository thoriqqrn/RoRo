#!/usr/bin/env python3
"""
Tuning ambang IMU: pantau motionScore LIVE dari topik rollators/<id>/imu_debug.

Tujuan: lihat angka motionScore asli saat DIAM vs DORONG vs ANGKAT, supaya kita
tahu ambang WALKING_ON / WALKING_OFF yang benar (atau apakah perlu ganti algoritma).

Butuh firmware dengan kImuDebugStream=true (default sudah true).

Mode:
  # Pantau mentah (angka per sampel)
  python3 tools/imu_tune.py watch

  # Mode fase: tekan ENTER untuk pindah fase (diam -> dorong -> angkat),
  # tiap fase dihitung min/avg/maks-nya untuk rekomendasi ambang.
  python3 tools/imu_tune.py phases
"""
import ssl
import statistics
import sys
import threading
import time

import paho.mqtt.client as mqtt

HOST = "746434cf770c4a868dc6863ed27b6d0a.s1.eu.hivemq.cloud"
PORT = 8883
USERNAME = "robotrollator"
PASSWORD = "Roro12345"
ROLLATOR_ID = "471grmOw38iBx5v5m9uC"
TOPIC = f"rollators/{ROLLATOR_ID}/imu_debug"

import json

latest = {"lock": threading.Lock(), "rows": []}


def make_client(cid):
    c = mqtt.Client(client_id=cid, callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    c.username_pw_set(USERNAME, PASSWORD)
    c.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    return c


def start(on_row):
    c = make_client("roro-imu-tune")
    def on_msg(cl, u, m):
        try:
            d = json.loads(m.payload.decode())
        except Exception:
            return
        on_row(d)
    c.on_connect = lambda cl, u, f, rc, p=None: cl.subscribe(TOPIC)
    c.on_message = on_msg
    c.connect(HOST, PORT, keepalive=20)
    c.loop_start()
    return c


def cmd_watch(_):
    print(f"Pantau {TOPIC} (Ctrl+C berhenti)\n")
    print(f"{'t':>6} {'motionScore':>12} {'accelDelta':>11} {'gyroMag':>9}  status")
    def on_row(d):
        st = "JALAN" if d.get("walking") else "diam"
        print(f"{time.strftime('%H:%M:%S')} {d.get('motionScore',0):12.3f} "
              f"{d.get('accelDelta',0):11.3f} {d.get('gyroMag',0):9.2f}  {st}", flush=True)
    start(on_row)
    while True:
        time.sleep(1)


def cmd_phases(_):
    scores = []
    collecting = {"on": True}
    def on_row(d):
        if collecting["on"]:
            scores.append(d.get("motionScore", 0.0))
    start(on_row)

    def summarize(name):
        if not scores:
            print(f"  [{name}] tidak ada data"); return None
        mn, mx = min(scores), max(scores)
        av = statistics.mean(scores)
        p90 = sorted(scores)[int(len(scores)*0.9)-1] if len(scores) > 1 else mx
        print(f"  [{name}] n={len(scores):4d}  min={mn:6.3f}  avg={av:6.3f}  p90={p90:6.3f}  max={mx:6.3f}")
        return (mn, av, p90, mx)

    results = {}
    for name, instruksi in [
        ("DIAM",   "Diamkan rollator, jangan disentuh."),
        ("DORONG", "Dorong/jalankan rollator normal terus-menerus."),
        ("ANGKAT", "Angkat-turun / goyang rollator."),
    ]:
        input(f"\n>>> Fase {name}: {instruksi}\n    Tekan ENTER untuk MULAI ukur (~6 detik)...")
        scores.clear()
        collecting["on"] = True
        time.sleep(6)
        collecting["on"] = False
        results[name] = summarize(name)

    print("\n=== REKOMENDASI AMBANG ===")
    diam = results.get("DIAM"); dorong = results.get("DORONG")
    if diam and dorong:
        diam_max = diam[3]; dorong_avg = dorong[1]; dorong_p90 = dorong[2]
        if dorong_avg > diam_max * 1.5:
            on = round((diam_max + dorong_avg) / 2, 2)
            off = round(diam_max + (on - diam_max) * 0.4, 2)
            print(f"  Dorong jelas > diam. Saran:")
            print(f"    kImuWalkingOnThreshold  = {on}")
            print(f"    kImuWalkingOffThreshold = {off}")
        else:
            print("  !! motionScore DORONG tidak jauh beda dari DIAM.")
            print("     Artinya rumus accel-magnitude TIDAK bisa mendeteksi dorong.")
            print("     Perlu GANTI algoritma (deteksi getaran/variance), bukan sekadar ubah ambang.")
    print("\nKirim hasil ini ke chat untuk di-set / diperbaiki algoritmanya.")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "watch"
    try:
        {"watch": cmd_watch, "phases": cmd_phases}.get(cmd, cmd_watch)(None)
    except KeyboardInterrupt:
        print("\nberhenti.")


if __name__ == "__main__":
    main()
