# KONTEKS PROYEK ROBOT ROLLATOR ESP32

## Deskripsi Proyek

Proyek ini adalah Robot Rollator Pintar berbasis ESP32 yang digunakan untuk membantu mobilitas pengguna serta melakukan monitoring kondisi robot secara real-time.

Fitur yang dimiliki saat ini:

* Kendali motor DC menggunakan BTS7960
* Pembacaan encoder roda
* Pembacaan sensor IMU MPU6050
* Monitoring baterai (saat ini belum)
* Koneksi WiFi
* Mode Access Point (AP) untuk konfigurasi awal
* Mode Station (STA) untuk koneksi ke WiFi rumah
* Firebase Realtime Database
* Dashboard web lokal
* Tombol kontrol manual
* Sistem emergency stop (belum ada )

Fitur yang dikembangakan oleh mobile dev:

* Deteksi jatuh
* Monitoring jarak jauh melalui internet
* Aplikasi Flutter
* Logging data perjalanan

---

# TUJUAN PENGEMBANGAN

Kode harus:

* Stabil 24 jam tanpa restart
* Mudah dikembangkan
* Tidak menggunakan blocking code
* Tidak menyebabkan ESP32 hang
* Mudah dibaca dan dipelihara

---

# STRUKTUR PROYEK WAJIB

Jangan menaruh semua kode di main.cpp.

Gunakan struktur modular seperti berikut:

src/
├── main.cpp
├── config.h
├── wifi_manager.cpp
├── wifi_manager.h
├── encoder.cpp
├── encoder.h
├── imu.cpp
├── imu.h
├── motor.cpp
├── motor.h
├── firebase_manager.cpp
├── firebase_manager.h
├── webserver.cpp
├── webserver.h
├── battery.cpp
├── battery.h

Setiap fitur harus berada pada file terpisah.

---

# ATURAN PEMROGRAMAN

WAJIB:

* Menggunakan millis() untuk timer
* Menggunakan FreeRTOS jika diperlukan
* Menggunakan fungsi modular
* Menggunakan struct untuk data bersama
* Menambahkan komentar yang jelas

DILARANG:

* Menggunakan delay()
* Menaruh seluruh program di loop()
* Membuat fungsi blocking yang lama
* Mengirim data Firebase setiap iterasi loop
* Menggunakan String secara berlebihan

---

# ARSITEKTUR FREE RTOS

Core 0:

* WiFi
* Firebase
* Web Server

Core 1:

* Motor Control
* Encoder
* IMU
* Safety System

Contoh Task:

* TaskMotor
* TaskEncoder
* TaskIMU
* TaskFirebase
* TaskWebServer
* TaskBattery

---

# DATA GLOBAL

Gunakan struct bersama:

struct RobotData
{
float kecepatanKiri;
float kecepatanKanan;

```
float pitch;
float roll;

float teganganBaterai;

bool wifiTerhubung;
bool emergencyStop;
```

};

Semua modul membaca dan menulis data dari struct ini.

---

# ALUR KONFIGURASI WIFI

Saat pertama kali menyala:

1. ESP32 membuat Access Point.

SSID:

Rollator_Setup

2. Pengguna menghubungkan HP ke AP tersebut.

3. Membuka browser.

Alamat:

192.168.4.1

4. Mengisi:

* Nama WiFi
* Password WiFi

5. Data disimpan menggunakan Preferences.

6. ESP32 restart otomatis.

7. ESP32 terhubung ke WiFi rumah.

8. Firebase aktif.

9. Dashboard aktif.

---

# ATURAN FIREBASE

Jangan mengirim data terus menerus.

Salah:

upload setiap loop.

Benar:

* Upload setiap 1000 ms
* Upload saat data berubah
* Gabungkan beberapa data dalam satu pengiriman

Schema monitoring Firebase yang dipakai mobile dev:

* `sos`: status tombol SOS aktif / nonaktif
* `gas`: status tombol gas di-hold / lepas
* `manual`: status motor manual test aktif / berhenti
* `sosHistory`, `gasHistory`, `manualHistory`: array histori event dengan timestamp

Mobile dev harus pakai snapshot listener / realtime listener ke dokumen rollator, bukan polling terus-menerus.

Prioritas Firebase lebih rendah dibanding sistem motor.

---

# PRIORITAS SISTEM

Prioritas tertinggi:

1. Emergency Stop
2. Motor Control
3. Encoder
4. IMU
5. WiFi
6. Firebase
7. Dashboard

Keselamatan robot harus selalu diutamakan.

---

# SISTEM KEAMANAN

Jika:

* Kemiringan robot terlalu besar
* Sensor mendeteksi kondisi berbahaya
* Tombol emergency ditekan

Maka:

* Motor harus berhenti
* Firebase mengirim status bahaya
* Dashboard menampilkan peringatan

---

# TARGET PERFORMA

Motor:
10 ms

Encoder:
10 ms

IMU:
20 ms

Firebase:
1000 ms

Monitoring baterai:
5000 ms

Dashboard:
Event Driven

---

# INSTRUKSI UNTUK AI

Saat menghasilkan kode:

* Jangan mengubah arsitektur modular.
* Jangan menggunakan delay().
* Jangan membuat kode yang blocking.
* Prioritaskan stabilitas daripada fitur tambahan.
* Gunakan kode yang siap digunakan pada PlatformIO.
* Berikan penjelasan file yang perlu dibuat.
* Jika menambahkan fitur baru, pisahkan ke file modul baru.
* Selalu pertimbangkan penggunaan memori ESP32.

---

# PERINTAH UNTUK TIM MOBILE DEV

Gunakan Firestore document berikut untuk monitoring realtime:

`projects/roro-90c6b/databases/(default)/documents/rollators/471grmOw38iBx5v5m9uC`

Listener wajib membaca field berikut:

* `gas == true` berarti tombol gas sedang ditekan.
* `gas == false` berarti tombol gas dilepas.
* `manual == true` berarti motor sedang jalan mode manual test.
* `manual == false` berarti mode manual test sudah berhenti.
* `sos == true` berarti kondisi SOS aktif.

Perilaku yang harus ditampilkan aplikasi:

* Saat `gas` berubah ke `true`, tampilkan status “Gas pressed” dan mulai indikator aktif.
* Saat `gas` kembali `false`, matikan indikator gas.
* Saat `manual` berubah ke `true`, tampilkan status “Manual running”.
* Saat `manual` kembali `false`, tampilkan status “Manual stopped”.
* Gunakan history `gasHistory` dan `manualHistory` kalau perlu untuk log aktivitas.

Jangan polling REST endpoint untuk event ini. Pakai realtime listener Firestore agar perubahan hold/release terbaca cepat.

Proyek ini merupakan Robot Rollator untuk kebutuhan akademik dan pengembangan produk nyata sehingga kualitas kode harus setara proyek produksi, bukan sekadar contoh sederhana.
