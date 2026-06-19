# Ada Perubahan Realtime: Event via MQTT (HiveMQ)

Dokumen ini untuk **mobile dev**. Ada perubahan kecil tapi penting pada cara
event cepat dikirim dari perangkat RoRo.

## Ringkasan perubahan

Mulai sekarang event **realtime** dikirim lewat **MQTT (HiveMQ)**, bukan Firebase:

| Event            | Dulu (lambat) | Sekarang (realtime) |
| ---------------- | ------------- | ------------------- |
| Gas ditekan/lepas | Firestore      | **MQTT**            |
| SOS aktif/clear   | Firestore      | **MQTT**            |
| Status IMU        | Firestore      | **MQTT**            |

**Firebase TIDAK dihapus.** Perangkat tetap menulis ke Firestore untuk
**penyimpanan & riwayat** (`gasHistory`, `sosHistory`, `imuHistory`, dll).

- **MQTT** → untuk **notifikasi/indikator live** di UI. Cepat, koneksi persisten.
- **Firestore** → untuk **history, statistik harian, audit**. Tetap pakai listener
  seperti sebelumnya untuk data tersimpan.

Alasan: tiap kirim Firestore membuka koneksi HTTPS+TLS baru sehingga ada delay
beberapa ratus ms s/d detikan. MQTT memakai satu koneksi tetap → push hampir instan.

## Broker

HiveMQ Cloud, **TLS wajib**.

```
Host : 746434cf770c4a868dc6863ed27b6d0a.s1.eu.hivemq.cloud
Port : 8883 (TLS)
Auth : username: robotrollator + password: Roro12345
TLS  : ya (wss://.../mqtt untuk web, port 8884 jika pakai WebSocket)
```

> Catatan: untuk Flutter mobile gunakan MQTT over TLS port **8883**.
> Untuk web/dashboard berbasis browser gunakan **WebSocket Secure** port **8884**
> path `/mqtt`. Host, username, password sama.

## Topik

Semua topik memakai `rollatorId` yang sama dengan dokumen Firestore
(`471grmOw38iBx5v5m9uC`). Format:

```
rollators/<rollatorId>/gas      → event tombol gas
rollators/<rollatorId>/sos      → event tombol SOS
rollators/<rollatorId>/imu      → status IMU (jalan/diam/offline)
rollators/<rollatorId>/status   → status online/offline perangkat (Last Will)
```

Contoh konkret:

```
rollators/471grmOw38iBx5v5m9uC/gas
rollators/471grmOw38iBx5v5m9uC/sos
rollators/471grmOw38iBx5v5m9uC/imu
rollators/471grmOw38iBx5v5m9uC/status
```

Subscribe sekaligus boleh pakai wildcard:

```
rollators/471grmOw38iBx5v5m9uC/#
```

## Sifat pesan

- **Retained = true.** Saat app baru connect/subscribe, broker langsung mengirim
  **nilai terakhir** tiap topik. Jadi tidak perlu polling status awal — kondisi
  terkini langsung diterima.
- **QoS 0.** Cukup untuk indikator live. Untuk data yang harus akurat historis,
  tetap andalkan Firestore.
- Saat perangkat connect, ia mem-publish ulang state terkini gas/sos/imu sekali
  supaya app langsung sinkron.

## Format payload (JSON)

### Topik `.../gas`

```json
{ "gas": true, "deviceMillis": 123456 }
```

- `gas == true` → tombol gas sedang aktif (ditekan **atau** sedang mode kunci/hold).
- `gas == false` → gas tidak aktif.
- `deviceMillis` → waktu internal perangkat (uptime ms), untuk urutan/debug.

> Catatan fitur gas: gas bisa aktif karena tombol ditahan **atau** karena mode
> "kunci gas" (triple-tap). Dari sisi MQTT keduanya sama: `gas:true` / `gas:false`.

### Topik `.../sos`

```json
{ "sos": true, "deviceMillis": 123456 }
```

- `sos == true` → kondisi SOS aktif → tampilkan alarm/peringatan.
- `sos == false` → SOS selesai/clear.

### Topik `.../imu`

```json
{
  "connected": true,
  "status": "jalan",
  "walking": true,
  "pitch": 3.21,
  "roll": -1.4,
  "motionScore": 1.87,
  "deviceMillis": 123456
}
```

- `status`: `"jalan"`, `"diam"`, atau (jika `connected=false`) anggap **offline**.
- `walking`: boolean, sama dengan `status=="jalan"`.
- `connected`: `false` artinya sensor IMU tidak terbaca → tampilkan offline.

### Topik `.../status` (online/offline perangkat)

```json
{ "online": true, "deviceMillis": 123456 }
```

- `online == true` → perangkat terkoneksi ke broker.
- `online == false` → dikirim otomatis oleh broker (**Last Will**) jika perangkat
  putus mendadak (mati/hilang sinyal). Pakai ini untuk badge "Perangkat offline".

> **WAJIB: gabungkan status ini dengan IMU.** Pesan IMU bersifat *retained* (state
> terakhir tersimpan di broker). Kalau perangkat mati saat status terakhir "jalan",
> nilai retained "jalan" akan tetap dikirim ke app yang baru connect — **padahal
> perangkat sudah mati.** Jadi:
>
> **Jika `status.online == false`, ANGGAP IMU = offline.** Jangan tampilkan "jalan"
> dari pesan IMU retained selama perangkat offline. Ini penyebab umum bug "IMU jalan
> terus" — pasti karena app menampilkan retained lama tanpa cek presence.
>
> Firmware juga sudah dibantu: saat boot perangkat **langsung publish status IMU
> sebenarnya** (menimpa retained basi), jadi setelah perangkat hidup lagi nilainya
> otomatis benar. Tapi selama perangkat mati, andalkan `status.online` di atas.

## Tugas mobile dev (ringkas)

1. Tambah MQTT client (mis. `mqtt_client` untuk Flutter) dengan TLS port 8883.
2. Connect pakai host/username/password dari embedded dev.
3. Subscribe `rollators/<rollatorId>/#`.
4. Map tiap topik ke indikator UI:
   - `/gas` → indikator gas aktif/nonaktif.
   - `/sos` → alarm SOS.
   - `/imu` → status jalan/diam/offline.
   - `/status` → badge perangkat online/offline.
5. Karena retained, **jangan** polling status awal — pakai pesan retained pertama.
6. **History tetap dari Firestore** (`imuHistory`, `gasHistory`, dll) seperti dokumen
   `IMU_FEATURE.md` dan `PROJECT_CONTEXT.md`. MQTT hanya untuk live, bukan riwayat.

## Catatan transisi

- Endpoint HTTP lokal (`GET /api/status`, dll) tetap ada untuk debugging dan kini
  menyertakan `mqtt_enabled`, `mqtt_connected`, `mqtt_last_message`.
- Jika MQTT belum dikonfigurasi/putus, perangkat tetap berjalan normal; event live
  saja yang tidak terkirim. Firestore tetap menerima data history.
- Selama migrasi, kamu boleh tetap membaca event dari Firestore sebagai fallback,
  tapi targetnya event live pindah ke MQTT agar delay hilang.
