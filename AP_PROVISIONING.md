# Spesifikasi Awal: AP Provisioning via QR

Dokumen ini mendefinisikan alur awal konfigurasi WiFi melalui mode Access Point (AP) dengan bantuan QR di perangkat. Fokus pada kebutuhan minimum agar aplikasi mobile dapat melakukan pairing dan mengirim SSID serta password WiFi rumah.

## Tujuan

- Mempermudah konfigurasi awal tanpa browser manual.
- Menjaga arsitektur modular dan non-blocking.
- Menggunakan AP lokal untuk mengirim kredensial WiFi.

## Ringkasan Alur

1. Perangkat menyalakan AP untuk provisioning.
2. Pengguna membuka aplikasi dan scan QR di alat (QR berisi id perangkat).
3. Aplikasi menampilkan AP perangkat (mis. RoRo R1) dan menghubungkan HP ke AP.
4. Aplikasi menampilkan form konfigurasi seperti kamera CCTV (SSID dan password WiFi rumah).
5. Aplikasi mengirim SSID dan password WiFi rumah ke perangkat via HTTP lokal.
6. Perangkat menyimpan kredensial (Preferences) dan restart.
7. Perangkat masuk mode STA dan mulai layanan normal.

## Data QR

QR hanya berisi id perangkat. Format bebas asalkan konsisten dan singkat.

Contoh payload QR:

RR-ESP32-0001

Catatan:
- Aplikasi menggunakan id ini untuk menampilkan nama perangkat dan membantu UX.
- Nama AP perangkat tetap mengikuti format tetap, mis. "RoRo R1".

## Endpoint Lokal

Base URL: http://192.168.4.1

### POST /api/wifi

Request JSON:

{
  "ssid": "NamaWiFiRumah",
  "password": "PasswordWiFiRumah"
}

Response JSON:

{
  "ok": true,
  "message": "saved",
  "restart_in_ms": 2000
}

Aturan:
- Jika input invalid, kembalikan ok=false dan message singkat.
- Setelah ok=true, perangkat melakukan restart terjadwal non-blocking.

### GET /api/status

Response JSON:

{
  "mode": "AP",
  "device_id": "RR-ESP32-0001",
  "uptime_ms": 123456
}

Tujuan:
- Aplikasi memastikan koneksi ke perangkat sebelum POST /api/wifi.

### POST /api/provisioning/start

Request JSON:

{
  "reason": "change_wifi"
}

Response JSON:

{
  "ok": true,
  "message": "switching_to_ap",
  "restart_in_ms": 2000
}

Aturan:
- Endpoint ini dipanggil dari mode STA saat user memilih "Ubah WiFi" di aplikasi.
- Perangkat menjadwalkan restart non-blocking lalu masuk mode AP.

## Perilaku Perangkat

- AP aktif saat belum ada kredensial WiFi tersimpan atau tombol reset provisioning ditekan.
- AP juga bisa diaktifkan ulang lewat perintah aplikasi (POST /api/provisioning/start).
- Jangan ada loop blocking selama provisioning.
- Jika kredensial tersimpan dan STA berhasil, AP dimatikan.
- Jika STA gagal dalam batas waktu (mis. 30s), kembali ke AP.

## Tugas Mobile Dev

1. Scan QR dan parse payload.
2. Hubungkan device WiFi HP ke SSID AP perangkat.
3. Setelah konek, akses GET /api/status untuk verifikasi.
4. Tampilkan form konfigurasi WiFi rumah dan kirim via POST /api/wifi.
5. Tampilkan status sukses dan instruksikan pengguna menunggu restart.
6. Setelah restart, aplikasi dapat menunggu koneksi internet normal.
7. Sediakan menu "Ubah WiFi" yang memanggil POST /api/provisioning/start dari mode STA.

## Catatan Keamanan

- Password WiFi rumah hanya dikirim sekali saat provisioning.
- Jika memungkinkan, gunakan AP dengan password default yang dicetak di perangkat.
- Hindari menyimpan password di aplikasi setelah provisioning.

## Pengujian Minimum

- Scan QR di kondisi cahaya normal.
- Pairing dari HP Android dan iOS.
- Kesalahan SSID/password menghasilkan feedback jelas.
- Restart sukses dan perangkat masuk mode STA.

---

# Perintah untuk Mobile Dev (Langkah Lengkap)

## A. Provisioning pertama kali (AP)

1. Scan QR di perangkat dan simpan `device_id`.
2. Hubungkan HP ke SSID AP perangkat (format sesuai perangkat, mis. "RoRo R1").
3. Verifikasi koneksi dengan GET `http://192.168.4.1/api/status`.
4. Tampilkan form input SSID dan password WiFi rumah.
5. Kirim POST `http://192.168.4.1/api/wifi` dengan JSON kredensial:

```json
{
  "ssid": "NamaWiFiRumah",
  "password": "PasswordWiFiRumah"
}
```

6. Jika respon `ok=true`, tampilkan pesan sukses dan tunggu restart sesuai `restart_in_ms`.
7. Setelah restart, perangkat masuk mode STA dan AP mati.
8. IP STA bisa didapat dari log serial (`[WiFi] Connected. IP: x.x.x.x`) atau dari router.

## B. Ubah WiFi saat sudah mode STA

1. Pastikan HP berada di jaringan WiFi yang sama dengan ESP32 (mode STA).
2. Dapatkan IP ESP32 dari router atau dari log serial (`[WiFi] Connected. IP: x.x.x.x`).
3. Jika mDNS tersedia, akses `http://rorro.local/api/status` untuk verifikasi.
4. Kirim POST ke IP STA (bukan 192.168.4.1):
m).
Cek BTS7960 kanan: pastikan R_EN dan L_EN sama-sama HIGH (jumper/terhubung benar).
Cek kabel sinyal dari ESP32 ke BTS7960 kanan: RPWM dan LPWM dua-duanya benar-benar terhubung.
```
http://<IP_ESP32>/api/provisioning/start
```

Body JSON:

```json
{
  "reason": "change_wifi"
}
```

5. ESP32 akan restart dan kembali ke mode AP.
6. Setelah AP aktif, ulangi langkah A.2 sampai A.6.

## C. Troubleshooting singkat

- Jika tidak kembali ke AP, cek log serial untuk status mode, timeout STA, dan alasan restart.
- Jika muncul `request handler not found`, berarti ada request ke path yang tidak terdaftar (mis. `/`).
 - Jika serial monitor tampil `�����`, pastikan baud rate 115200 (atau 74880 untuk boot log).
