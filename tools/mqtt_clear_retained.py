#!/usr/bin/env python3
"""
Bersihkan pesan MQTT retained yang nyangkut di broker HiveMQ.

Kapan dipakai:
  - Saat client (web/mobile) menampilkan status basi (mis. IMU "jalan" terus)
    padahal perangkat sudah diam/mati. Penyebabnya pesan retained lama.
  - Menghapus retained = publish payload KOSONG dengan retain=true ke topik itu.

Contoh:
  python3 tools/mqtt_clear_retained.py            # bersihkan imu, gas, sos
  python3 tools/mqtt_clear_retained.py imu        # hanya imu
  python3 tools/mqtt_clear_retained.py imu gas sos status
"""
import ssl
import sys
import time

import paho.mqtt.client as mqtt

HOST = "746434cf770c4a868dc6863ed27b6d0a.s1.eu.hivemq.cloud"
PORT = 8883
USERNAME = "robotrollator"
PASSWORD = "Roro12345"
ROLLATOR_ID = "471grmOw38iBx5v5m9uC"
BASE = f"rollators/{ROLLATOR_ID}"

# Topik yang dibersihkan kalau tidak diberi argumen
DEFAULT = ["imu", "gas", "sos"]


def main():
    topics = sys.argv[1:] or DEFAULT

    c = mqtt.Client(client_id="roro-cleaner",
                    callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    c.username_pw_set(USERNAME, PASSWORD)
    c.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    c.connect(HOST, PORT, keepalive=20)
    c.loop_start()

    for key in topics:
        t = f"{BASE}/{key}"
        # payload kosong + retain=true = perintah broker hapus retained
        c.publish(t, payload=b"", qos=0, retain=True).wait_for_publish(timeout=5)
        print(f"[CLEAR] retained dihapus: {t}")

    time.sleep(1)
    c.loop_stop()
    c.disconnect()
    print("selesai. subscriber baru tidak akan dapat pesan basi lagi untuk topik di atas.")


if __name__ == "__main__":
    main()
