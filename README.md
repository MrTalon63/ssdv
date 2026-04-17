# SSDV - simple command line app for encoding / decoding SSDV image data

Originally created by Philip Heron <phil@sanslogic.co.uk>
http://www.sanslogic.co.uk/ssdv/
Now available at his Codeberg repository: https://codeberg.org/fsphil/ssdv

A robust packetised version of the JPEG image format.

Uses the Reed-Solomon codec written by Phil Karn, KA9Q.

This version has been modified to support higher number of image IDs, up to 65535, and always produce fixed-length packets, by default 256 bytes.

#### ENCODING

$ ssdv -e -c TEST01 -i ID input.jpeg output.bin

This encodes the 'input.jpeg' image file into SSDV packets stored in the 'output.bin' file. TEST01 (the callsign, an alphanumeric string up to 6 characters) and ID (a number from 0-65535) are encoded into the header of each packet. The ID should be changed for each new image transmitted to allow the decoder to identify when a new image begins.

The output file contains a series of fixed-length SSDV packets (default 256 bytes). Additional data may be transmitted between each packet, the decoder will ignore this.

#### PACKET STRUCTURE

Original packet structure can be found here: https://ukhas.org.uk/doku.php?id=guides:ssdv#packet_format
Current packet header and trailer layout:

| Byte offset              | Size (bytes) | Field               | Encoding / notes                                                                                                                                          |
| ------------------------ | -----------: | ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0                        |            1 | Sync                | 0x55                                                                                                                                                      |
| 1                        |            1 | Packet type         | 0x66 + type                                                                                                                                               |
| 2-5                      |            4 | Callsign            | Base-40 encoded callsign. Up to 6 digits                                                                                                                  |
| 6-7                      |            2 | Image ID            | Big-endian (MSB, LSB)                                                                                                                                     |
| 8-9                      |            2 | Packet ID           | Big-endian (MSB, LSB)                                                                                                                                     |
| 10                       |            1 | Width               | width / 16                                                                                                                                                |
| 11                       |            1 | Height              | height / 16                                                                                                                                               |
| 12                       |            1 | Flags               | 00qqqexx: 00 = Reserved, qqq = JPEG quality level (0-7 XOR 4), e = EOI flag (1 = Last Packet), xx = Subsampling Mode (0 = 2×2, 1 = 1×2, 2 = 2×1, 3 = 1×1) |
| 13                       |            1 | MCU offset          | Offset in bytes to the beginning of the first MCU block in the payload, or 0xFF if none present                                                           |
| 14-15                    |            2 | MCU index (MCU ID)  | The number of the MCU pointed to by the offset above (big endian), or 0xFFFF if none present                                                              |
| 16...                    |     variable | Payload             | Depends on total packet size and type                                                                                                                     |
| after payload            |            4 | CRC32               | 32-bit CRC                                                                                                                                                |
| final (normal mode only) |           32 | Reed-Solomon parity | Present only for normal/FEC packets                                                                                                                       |

For total packet length up to 256 bytes:

- No-FEC: header(16) + payload + crc(4) = pkt_size
- Normal/FEC: header(16) + payload + crc(4) + rs(32) = pkt_size

DECODING

$ ssdv -d input.bin output.jpeg

This decodes a file 'input.bin' containing a series of SSDV packets into the JPEG file 'output.jpeg'.

LIMITATIONS

Only JPEG files are supported, with the following limitations:

- Greyscale or YUV/YCbCr colour formats
- Width and height must be a multiple of 16 (up to a resolution of 4080 x 4080)
- Baseline DCT only
- The total number of MCU blocks must not exceed 65535

INSTALLING

make

TODO

- Allow the decoder to handle multiple images in the input stream.
- Experiment with adaptive or multiple huffman tables.
