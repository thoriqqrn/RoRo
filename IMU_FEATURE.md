# Fitur IMU Rollator

Dokumen ini menjelaskan perintah dan kontrak data untuk fitur IMU agar mobile dev bisa membaca status jalan, diam, timestamp, dan riwayat pemakaian per hari.

## Tujuan Fitur

- Membaca status IMU sebagai `jalan`, `diam`, atau `offline`.
- Menyimpan timestamp saat status berubah.
- Menyediakan history agar mobile app bisa menghitung durasi jalan per hari.
- Memberikan data yang cukup untuk analisis di sisi aplikasi mobile, bukan di ESP32.

## Definisi Status

- `jalan`: IMU mendeteksi pola gerak berkelanjutan.
- `diam`: IMU aktif tetapi tidak terdeteksi gerakan yang masuk ambang.
- `offline`: sensor IMU tidak terbaca atau koneksi I2C gagal.

## Filter Noise Yang Dipakai Di ESP32

Pembacaan IMU sudah diberi filter ringan di firmware:

- Pitch dan roll di-smooth dengan exponential moving average.
- Status `jalan` / `diam` tidak diputus dari satu sampel saja.
- Ada beberapa sampel beruntun sebelum status dianggap berubah.

Catatan:
- Filter ini cukup untuk mengurangi jitter kecil.
- Jika mobile dev mau rumus yang lebih kuat, logika final tetap sebaiknya dibuat di aplikasi mobile dari history yang dikirim ESP32.

## Endpoint Untuk Mobile Dev

Base URL lokal saat perangkat aktif di jaringan yang sama:

- `GET /api/status`
- `GET /api/imu/status`

Contoh:

```text
http://<IP_ESP32>/api/status
http://<IP_ESP32>/api/imu/status
```

Jika device ada di mode AP provisioning, gunakan:

```text
http://192.168.4.1/api/status
http://192.168.4.1/api/imu/status
```

## Response Yang Penting

### GET /api/status

Field yang relevan untuk mobile app:

```json
{
  "imu_connected": true,
  "imu_status": "jalan",
  "imu_walking": true,
  "imu_pitch": 3.21,
  "imu_roll": -1.4,
  "imu_motion_score": 1.87,
  "imu_status_changed_ms": 1234567,
  "imu_last_update_ms": 1234678,
  "firebase_pending_imu": false
}
```

### GET /api/imu/status

Endpoint ini bisa dipakai jika mobile dev hanya butuh data IMU.

Field utama:

- `imu_connected`
- `imu_status`
- `imu_walking`
- `imu_pitch`
- `imu_roll`
- `imu_motion_score`
- `imu_status_changed_ms`
- `imu_last_update_ms`

## Firebase Schema Untuk IMU

ESP32 mengirim data ke dokumen rollator utama dan history subcollection.

### Dokumen utama rollator

Field yang dipakai:

- `imuStatus`: string, nilai `jalan` / `diam` / `offline`
- `imuBerjalan`: boolean
- `imuConnected`: boolean
- `imuPitch`: number
- `imuRoll`: number
- `imuMotionScore`: number
- `imuUpdatedAtMs`: number

### History IMU

Subcollection:

```text
rollators/<rollatorId>/imuHistory
```

Setiap event history menyimpan:

- `status`
- `connected`
- `walking`
- `pitch`
- `roll`
- `motionScore`
- `timestamp` jika NTP sudah tersedia
- `deviceMillis` sebagai fallback waktu internal

## Perintah Untuk Mobile Dev

### 1. Ambil status realtime

- Poll endpoint `GET /api/status` atau `GET /api/imu/status` saat debugging.
- Untuk produksi, lebih baik dengarkan Firestore dokumen rollator.

### 2. Hitung menit jalan per hari

Logika yang disarankan di mobile app:

- Saat `imuStatus` berubah dari `diam` ke `jalan`, simpan waktu mulai.
- Saat berubah dari `jalan` ke `diam`, hitung selisih durasi.
- Akumulasi durasi hanya dari interval saat status `jalan`.
- Abaikan perubahan cepat yang sangat singkat jika perlu, sesuai aturan app.

### 3. Gunakan history untuk audit

- Gunakan `imuHistory` untuk melihat urutan perubahan status.
- History cocok untuk grafik, timeline, dan validasi data harian.
- Jangan hitung durasi hanya dari satu event history tanpa pasangan status lawan.

## Contoh Alur Hitung Durasi

Misal urutannya:

1. `diam` pada 08:00
2. `jalan` pada 08:12
3. `diam` pada 08:28
4. `jalan` pada 09:05
5. `diam` pada 09:10

Maka mobile app bisa hitung:

- Jalan 1: 16 menit
- Jalan 2: 5 menit
- Total harian: 21 menit

## Aturan Integrasi

- Jangan hitung durasi di ESP32.
- ESP32 hanya bertugas mendeteksi status dan mengirim event.
- Mobile app yang menghitung menit, sesi jalan, dan statistik harian.
- Jika sensor `offline`, tampilkan status error atau data belum tersedia.

## Catatan Teknis

- Status IMU yang dipakai saat ini berasal dari MPU6050 di pin I2C default ESP32.
- Pembacaan diproses non-blocking di task terpisah.
- Firebase hanya dikirim saat ada perubahan status atau saat data perlu disinkronkan.
