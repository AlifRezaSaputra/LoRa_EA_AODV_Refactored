# LoRa EA-AODV ESP32 — Refactored Hardware / Routing

Versi ini memisahkan akses perangkat keras dari logika EA-AODV untuk memudahkan kerja paralel antara pengembang hardware dan routing. Kedua node tetap merupakan dua sketch Arduino yang terpisah.

## Struktur proyek

```text
LoRa_EA_AODV_Refactored/
├── Node_TX_EA_AODV/
│   ├── Node_TX_EA_AODV.ino   # entry point setup() / loop()
│   ├── Config.h              # ID node, pin, parameter radio, timeout, limit
│   ├── Hardware.h            # API hardware TX
│   ├── Hardware.cpp          # SX1276, GPS, INA219, tombol SOS, LED
│   ├── Router.h              # API router TX
│   ├── Router.cpp            # SOURCE + RELAY, EA-AODV, queue, ACK, RERR
│   ├── PacketFormat.h        # deklarasi parser / pembentuk path
│   ├── PacketFormat.cpp      # split/join field dan helper path
│   └── SeenEntry.h           # struktur deduplikasi
│
└── Node_RX_EA_AODV/
    ├── Node_RX_EA_AODV.ino   # entry point setup() / loop()
    ├── Config.h              # ID BASE, pin, parameter radio, timeout, limit
    ├── Hardware.h            # API hardware Basecamp
    ├── Hardware.cpp          # SX1276, LED Basecamp, buzzer
    ├── Router.h              # API router Basecamp
    ├── Router.cpp            # Top-K RREQ, pemilihan jalur, RREP, DATA, ACK, RERR
    ├── PacketFormat.h
    ├── PacketFormat.cpp
    └── SeenEntry.h
```

Semua file `.cpp` ditempatkan langsung di folder sketch agar dapat ditemukan oleh Arduino IDE tanpa konfigurasi build tambahan.

## Tanggung jawab modul

### Hardware

`Hardware.cpp` adalah satu-satunya modul yang mengakses objek RadioLib, GPIO, GPS, INA219, LED, dan buzzer. Routing menggunakan API di `Hardware.h`. Status sukses/error/CRC radio juga diterjemahkan menjadi enum netral (`HW_RADIO_OK`, `HW_RADIO_ERROR`, `HW_RADIO_CRC_MISMATCH`), sehingga `Router.cpp` tidak perlu menyertakan RadioLib.

**Node TX — API hardware:**
- `txHardwareBegin()` — inisialisasi tombol, LED, I2C/INA219, GPS, SPI, dan SX1276.
- `txTransmitFrame()` / `txPollRadio()` — kirim/ambil frame radio.
- `txGetGpsLocation()` / `txGpsUpdate()` — akses koordinat GPS.
- `txGetBatteryVoltage()` / `txGetResidualEnergyPercent()` — akses data baterai.
- `txSosPressed()` — membaca tombol SOS.
- `txLedRedOn()` / `txLedGreenOn()` / `txLedUpdate()` — indikator LED.

**Node RX — API hardware:**
- `rxHardwareBegin()` — inisialisasi GPIO, SPI, dan SX1276.
- `rxTransmitFrame()` / `rxPollRadio()` — kirim/ambil frame radio.
- `rxLedPulse()` / `rxLedUpdate()` — indikator DATA.
- `rxActivateBuzzer()` / `rxDeactivateBuzzer()` — alarm SOS.

### Routing

`Router.cpp` menampung logika protokol dan state jaringan. Modul ini tidak mengakses `digitalWrite()`, `digitalRead()`, objek radio, objek GPS, atau objek INA219 secara langsung.

- TX: SOURCE + RELAY, RREQ forwarding, RREP processing, routing table, neighbor maintenance, deduplikasi, prioritas antrean TX, telemetry, SOS, ACK timeout/retry, dan RERR.
- RX: pengumpulan kandidat RREQ, Top-K, pemilihan rute EA-AODV/AODV, RREP, penerimaan DATA, deduplikasi DATA, ACK, RERR, dan metrik Basecamp.

### PacketFormat

`PacketFormat.cpp` berisi helper generik untuk memecah dan menggabungkan field `|` serta mengolah path `>`; format payload tidak diubah.

## Cara membuka dan mengunggah

1. Ekstrak ZIP.
2. Buka folder `Node_TX_EA_AODV`, lalu buka `Node_TX_EA_AODV.ino` dengan Arduino IDE untuk firmware node pendaki.
3. Untuk setiap node pendaki, ubah `NODE_ID` di `Node_TX_EA_AODV/Config.h` menjadi ID unik, misalnya `NODE01`, `NODE02`, `NODE03`. Jangan memakai `|` atau `>` pada ID.
4. Buka folder `Node_RX_EA_AODV`, lalu buka `Node_RX_EA_AODV.ino` untuk firmware Basecamp.
5. Pastikan `USE_EA_AODV` sama pada semua perangkat dan parameter radio TX/RX identik.
6. Pilih board ESP32 dan port yang benar, lalu compile dan upload setiap sketch ke perangkat masing-masing.

Jangan menggabungkan dua folder node menjadi satu sketch. TX dan RX adalah firmware terpisah.

## Library dan platform yang diperlukan

- ESP32 Arduino Core
- RadioLib
- TinyGPSPlus (TX)
- Adafruit INA219 (TX)

`Arduino.h`, `SPI.h`, dan `Wire.h` disediakan oleh platform Arduino ESP32.

## Parameter protokol/radio yang dipertahankan

| Parameter | Nilai |
|---|---:|
| Frekuensi | 923.0 MHz |
| Bandwidth | 125 kHz |
| Spreading Factor | SF7 |
| Coding Rate | 4/5 (`LORA_CR = 5` pada RadioLib) |
| Sync Word | `0x12` |
| TX power | 10 dBm |
| Preamble | 8 |
| CRC | Aktif |
| RREQ TTL awal/maksimum | 6 |
| Route lifetime | 60.000 ms |
| Neighbor timeout | 180.000 ms |
| TX telemetry interval | 5.000 ms |
| Base discovery window | 900 ms |
| TX ACK timeout | 1.800 ms + 750 ms per hop, maksimum 7.000 ms |

## Format frame yang dipertahankan

- `HELLO|nodeId`
- `RREQ|origin|destination|requestId|previousHop|hops|minEnergy|path|ttl`
- `RREP|origin|destination|requestId|selectedPath|index|energy|ttl`
- `RERR|reporter|destination|brokenPath|seq|ttl`
- `DATA`: `src|dst|seq|lat|lng|battV|battPct|sos|hops|path|nextHop|ttl`
- `ACK`: `src|dst|ACK|seq|nextHop|ttl`; handler TX tetap menerima bentuk ACK lama 4-field sebagaimana sketch asal.

## Pembagian kerja yang disarankan

- **Partner hardware:** fokus pada `Hardware.cpp`, `Hardware.h`, dan pin di `Config.h`. Hindari mengubah isi atau urutan field paket.
- **Pengembang routing:** fokus pada `Router.cpp`, `Router.h`, dan `PacketFormat.cpp/.h`. Untuk perubahan parameter bersama, koordinasikan perubahan pada `Config.h`.
- **Integrasi:** setiap perubahan nama API di `Hardware.h` harus disepakati bersama dan diselaraskan dengan pemanggilnya di `Router.cpp`.

## Catatan kompatibilitas dan pengujian

Refactoring ini memindahkan fungsi perangkat keras ke adapter dan mempertahankan alur routing, aturan pemilihan jalur, string frame, nilai radio, timer, dan kondisi ACK/RERR dari sketch yang diberikan. Tidak ada perubahan pada algoritma pemilihan kandidat Top-K atau metrik EA-AODV.

Pemeriksaan sintaks C++ dilakukan secara lokal menggunakan stub API Arduino/RadioLib untuk memeriksa struktur dan referensi antarmodul. Pemeriksaan tersebut **bukan** pengganti compile dengan Arduino IDE/ESP32 Core atau uji perangkat fisik. Arduino CLI dan board ESP32/SX1276 tidak tersedia di lingkungan pembuatan ini, sehingga status compile target dan perilaku radio aktual belum dapat dikonfirmasi.

Sebelum penggunaan lapangan, compile kedua sketch di Arduino IDE dan uji minimal: 1-hop, 3-hop, 6-hop, ACK timeout, RERR, kehilangan paket, GPS invalid, INA219 tidak terdeteksi, dan SOS pada kondisi rute tersedia maupun saat discovery. Sistem SOS perlu pengujian fisik menyeluruh sebelum dipakai untuk keselamatan.
