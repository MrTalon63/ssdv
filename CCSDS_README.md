# SSDV CCSDS Mode (Stripped Version)

This is a stripped-down SSDV implementation optimized for embedding in CCSDS space packets. It removes sync bytes, callsign, CRC, and FEC/RS codes.

## Quick Facts

- **Packet Size**: 246 bytes (vs 256 standard)
- **Header Size**: 13 bytes (vs 18 standard)
- **Payload**: 233 bytes pure JPEG data (vs 202-224 standard)
- **Removed**: Sync (0xD3), Callsign (4 bytes), CRC (4 bytes), FEC/RS codes (32 bytes)

## Usage

### Encoding

```c
#include "ssdv.h"

ssdv_t ssdv;
uint8_t packet[246];
uint8_t jpeg_data[JPEG_SIZE];
size_t jpeg_size;

// Initialize encoder with CCSDS type
// Pass NULL for callsign (not used in CCSDS mode)
ssdv_enc_init(&ssdv, SSDV_TYPE_CCSDS, NULL,
              image_id,    // 0-65535
              quality,     // 0-7
              246);        // packet size

// Set output buffer
ssdv_enc_set_buffer(&ssdv, packet);

// Feed JPEG data
ssdv_enc_feed(&ssdv, jpeg_data, jpeg_size);

// Get packets
while(ssdv_enc_get_packet(&ssdv) == SSDV_OK) {
    // packet now contains 246 bytes ready to send
    // Can embed directly in CCSDS packet
    ccsds_send(packet, 246);
}
```

### Decoding

```c
#include "ssdv.h"

ssdv_t ssdv;
uint8_t packet[246];
uint8_t *jpeg_data;
size_t jpeg_size;

// Allocate JPEG output buffer (several MB recommended)
jpeg_data = malloc(5 * 1024 * 1024);

// Initialize decoder with CCSDS packet size
ssdv_dec_init(&ssdv, 246);
ssdv_dec_set_buffer(&ssdv, jpeg_data, 5 * 1024 * 1024);

// Process packets
while(get_next_packet(packet, 246) == OK) {
    // Validate packet
    if(ssdv_dec_is_packet(packet, 246, NULL) == 0) {
        // Feed to decoder
        ssdv_dec_feed(&ssdv, packet);
    }
}

// Get final JPEG
ssdv_dec_get_jpeg(&ssdv, &jpeg_data, &jpeg_size);
// jpeg_data now points to complete JPEG image
```

## Command Line

The modified `main.c` automatically detects packet size and handles both standard SSDV and CCSDS modes:

```bash
# Encode JPEG to CCSDS packets (246 bytes each)
./ssdv -e -C image.jpg image.ccsds

# Decode CCSDS packets back to JPEG
./ssdv -d image.ccsds image_out.jpg

# Decode with verbose output
./ssdv -d -v image.ccsds > image_out.jpg
```

## Packet Header Layout

```
Offset  Size  Field
------  ----  -----
0       2     Image ID
2       3     Packet ID
5       1     Width (in 16-pixel units)
6       1     Height (in 16-pixel units)
7       1     Flags (huff_profile[1], quality[3], eoi[1], mcu_mode[2])
8       2     MCU Offset (where this MCU starts in packet payload)
10      3     MCU ID
13      233   JPEG Payload Data
```

## Differences from Standard SSDV

| Feature     | SSDV Normal    | SSDV NoFEC    | SSDV CCSDS |
| ----------- | -------------- | ------------- | ---------- |
| Packet Size | 256            | 256           | 246        |
| Sync Byte   | Yes (0xD3)     | Yes (0xD3)    | No         |
| Callsign    | Yes (4 bytes)  | Yes (4 bytes) | No         |
| CRC-32      | Yes (4 bytes)  | Yes (4 bytes) | No         |
| RS Codes    | Yes (32 bytes) | No            | No         |
| Header Size | 18 bytes       | 18 bytes      | 13 bytes   |
| Payload     | 202 bytes      | 234 bytes     | 233 bytes  |
| FEC Capable | Yes            | No            | No\*       |

\*CCSDS mode relies on CCSDS packet-level error correction instead

## Notes

- CCSDS packets have no error correction - rely on CCSDS transport layer
- Image dimensions must be multiples of 16 pixels
- Maximum image size: 4080×4080 pixels
- Quality levels 0-7 (higher = better)
- All packet data must be in order (no reordering/loss tolerance like standard SSDV)
