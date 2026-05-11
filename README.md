# SSDV - simple command line app for encoding / decoding SSDV image data

Originally created by Philip Heron <phil@sanslogic.co.uk>
http://www.sanslogic.co.uk/ssdv/
Now available at his Codeberg repository: https://codeberg.org/fsphil/ssdv

A robust packetised version of the JPEG image format.

Uses the Reed-Solomon codec written by Phil Karn, KA9Q.

This version has been modified to support higher number of image IDs, up to 65535, and always produce fixed-length packets, by default 256 bytes long, with the option to change this. The packet header has been modified to allow more packets per image and allow higher amount of MCU blocks per image.

Also experimental optimized Huffman tables have been added, they're enabled by default and should improve compression efficiency for typical SSDV images, but the standard tables can be used with the `-u 0` option.
Decoder will automatically detect which Huffman profile was used and decode accordingly.

#### VERSION

`ssdv -v`

Prints the current binary version and exits.

#### ENCODING

`ssdv -e -c TEST01 -i ID input.jpeg output.bin`

This encodes the `input.jpeg` image file into SSDV packets stored in the `output.bin` file. TEST01 (the callsign, an alphanumeric string up to 6 characters) and ID (a number from 0-65535) are encoded into the header of each packet. The ID should be changed for each new image transmitted to allow the decoder to identify when a new image begins.

The output file contains a series of fixed-length SSDV packets (default 256 bytes). Additional data may be transmitted between each packet, the decoder will ignore this.

Use `-u 0` to force standard Huffman tables, or `-u 1` (default) for the optimized profile.

You can specify a custom packet length with `-l <length>`. Note that packet sizes larger than 256 bytes are only supported when Forward Error Correction is disabled (using the `-n` option).

#### PACKET STRUCTURE

Original packet structure can be found here: https://ukhas.org.uk/doku.php?id=guides:ssdv#packet_format

Current packet header and trailer layout:

| Byte offset              | Size (bytes) | Field               | Encoding / notes                                                                                                                                              |
| ------------------------ | -----------: | ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0                        |            1 | Sync                | 0xD3 (11010011)                                                                                                                                               |
| 1-4                      |            4 | Callsign            | Base-40 encoded callsign. Up to 6 digits                                                                                                                      |
| 5-6                      |            2 | Image ID            | Big-endian (MSB, LSB)                                                                                                                                         |
| 7-9                      |            3 | Packet ID           | Big-endian (MSB, MID, LSB)                                                                                                                                    |
| 10                       |            1 | Width               | width / 16                                                                                                                                                    |
| 11                       |            1 | Height              | height / 16                                                                                                                                                   |
| 12                       |            1 | Flags               | thqqqexx: t = type (0 = Normal/FEC, 1 = No-FEC), h = Huffman profile (0 = std, 1 = opt), qqq = JPEG quality level (0-7 XOR 4), e = EOI flag, xx = subsampling |
| 13-14                    |            2 | MCU offset          | Offset in bytes to the beginning of the first MCU block in the payload, or 0xFFFF if none present                                                             |
| 15-17                    |            3 | MCU index (MCU ID)  | The number of the MCU pointed to by the offset above (big endian), or 0xFFFFFF if none present                                                                |
| 18...                    |     variable | Payload             | Depends on total packet size and type                                                                                                                         |
| after payload            |            4 | CRC32               | 32-bit CRC                                                                                                                                                    |
| final (normal mode only) |           32 | Reed-Solomon parity | Present only for normal/FEC packets                                                                                                                           |

Packet length constraints:

- Normal/FEC mode is strictly limited to a maximum packet size of 256 bytes due to the Reed-Solomon (GF(256)) implementation.
    - Normal/FEC: header(18) + payload + crc(4) + rs(32) = pkt_size
- No-FEC mode (`-n`) supports larger packet sizes (e.g. `-l 512`).
    - No-FEC: header(18) + payload + crc(4) = pkt_size

#### DECODING

`ssdv -d input.bin output.jpeg`

This decodes a file `input.bin` containing a series of SSDV packets into the JPEG file `output.jpeg`.

#### LIMITATIONS

Only JPEG files are supported, with the following limitations:

- Greyscale or YUV/YCbCr colour formats
- Width and height must be a multiple of 16 (up to a resolution of 4080 x 4080)
- Baseline DCT only
- The total number of MCU blocks must not exceed 16777215

The encoder now uses Huffman profile `1` by default (optimized symbol ordering for SSDV payload statistics). The decoder reads the `h` flag bit and selects the matching profile automatically.

#### INSTALLING

The project uses CMake and builds both the command-line tool (`ssdv`) and a lightweight HTML/Webview-based GUI (`ssdv-gui`).

**Prerequisites:**

- CMake (3.16+)
- Git
- C/C++ Compiler (GCC, Clang, or MSVC)
- **Linux only:** GTK3 and WebKit2GTK development headers.
  _(e.g., on Ubuntu/Debian: `sudo apt install build-essential cmake git pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev`)_

**Building:**

You can build the project using standard CMake commands:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Alternatively, a wrapper `Makefile` is provided for convenience on Unix-like systems:

```bash
make
sudo make install
```

The compiled binaries (`ssdv` and `ssdv-gui` or `.exe` on Windows) will be located in the `build/bin/` directory.
