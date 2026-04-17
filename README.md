# SSDV - simple command line app for encoding / decoding SSDV image data

Originally created by Philip Heron <phil@sanslogic.co.uk>
http://www.sanslogic.co.uk/ssdv/
Now available at his Codeberg repository: https://codeberg.org/fsphil/ssdv

A robust packetised version of the JPEG image format.

Uses the Reed-Solomon codec written by Phil Karn, KA9Q.

This version has been modified to support higher number of image IDs, up to 65535, and always produce fixed-length packets, by default 256 bytes long, with the option to change this. The packet header has been modified to allow more packets per image and allow higher amount of MCU blocks per image.

Also experimental optimized Huffman tables have been added, they're enabled by default and should improve compression efficiency for typical SSDV images, but the standard tables can be used with the `-u 0` option.
Decoder will automatically detect which Huffman profile was used and decode accordingly.

#### ENCODING

`ssdv -e -c TEST01 -i ID input.jpeg output.bin`

This encodes the `input.jpeg` image file into SSDV packets stored in the `output.bin` file. TEST01 (the callsign, an alphanumeric string up to 6 characters) and ID (a number from 0-65535) are encoded into the header of each packet. The ID should be changed for each new image transmitted to allow the decoder to identify when a new image begins.

The output file contains a series of fixed-length SSDV packets (default 256 bytes). Additional data may be transmitted between each packet, the decoder will ignore this.

Use `-u 0` to force standard Huffman tables, or `-u 1` (default) for the optimized profile.

#### PACKET STRUCTURE

Original packet structure can be found here: https://ukhas.org.uk/doku.php?id=guides:ssdv#packet_format

Current packet header and trailer layout:

| Byte offset              | Size (bytes) | Field               | Encoding / notes                                                                                                                                  |
| ------------------------ | -----------: | ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0                        |            1 | Sync                | 0x55                                                                                                                                              |
| 1                        |            1 | Packet type         | 0x66 + type                                                                                                                                       |
| 2-5                      |            4 | Callsign            | Base-40 encoded callsign. Up to 6 digits                                                                                                          |
| 6-7                      |            2 | Image ID            | Big-endian (MSB, LSB)                                                                                                                             |
| 8-10                     |            3 | Packet ID           | Big-endian (MSB, MID, LSB)                                                                                                                        |
| 11                       |            1 | Width               | width / 16                                                                                                                                        |
| 12                       |            1 | Height              | height / 16                                                                                                                                       |
| 13                       |            1 | Flags               | hqqqexx: h = Huffman profile (0 = standard, 1 = low-entropy optimized), qqq = JPEG quality level (0-7 XOR 4), e = EOI flag, xx = subsampling mode |
| 14                       |            1 | MCU offset          | Offset in bytes to the beginning of the first MCU block in the payload, or 0xFF if none present                                                   |
| 15-17                    |            3 | MCU index (MCU ID)  | The number of the MCU pointed to by the offset above (big endian), or 0xFFFFFF if none present                                                    |
| 18...                    |     variable | Payload             | Depends on total packet size and type                                                                                                             |
| after payload            |            4 | CRC32               | 32-bit CRC                                                                                                                                        |
| final (normal mode only) |           32 | Reed-Solomon parity | Present only for normal/FEC packets                                                                                                               |

For total packet length up to 256 bytes:

- No-FEC: header(18) + payload + crc(4) = pkt_size
- Normal/FEC: header(18) + payload + crc(4) + rs(32) = pkt_size

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

make

#### TODO

- Allow the decoder to handle multiple images in the input stream.
- Experiment with adaptive or multiple huffman tables.
