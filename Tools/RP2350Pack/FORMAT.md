# QRP2350 asset image, version 1

All integers are little-endian uint32. On-disk pointers are never host pointers.
Offsets are from the beginning of the image. No runtime allocation is needed.

Header (64 bytes): 8-byte `QRP2350\0`, then 14 uint32 fields:
version=1, block_bytes=4096, total_bytes, file_count, block_count,
directory_offset=64, block_table_offset, payload_offset,
metadata_crc32, followed by five zero reserved fields.
Metadata CRC uses standard IEEE CRC32 over bytes `[64,payload_offset)`.

Each directory entry (80 bytes): 56-byte NUL-terminated ASCII name,
logical_size, first_block, block_count, logical_content_crc32, zero flags,
zero reserved. Entries own consecutive, nonoverlapping runs of block records.

Each block record (16 bytes): payload_offset, stored_bytes, raw_bytes, flags.
flags=0 means raw; flags=1 means a standard raw LZ4 block (no frame wrapper).
All blocks except each file's last decode to 4096 bytes. Payloads are tightly
packed with zero padding to four-byte alignment. Raw full blocks are thus
physically contiguous when no compressed block intervenes.

`qpak_read` provides bounds-checked random reads. A caller-owned 4 KiB cache
decodes one compressed block at a time; callers on different cores use different
caches. Initialize each cache to zero, keep qpak handles immutable during use,
and reset caches when reopening/replacing an image.

`qpak_map` returns a direct const pointer only when the entire requested range is
raw and physically contiguous. Callers must handle NULL with the read API;
never dereference a logical file offset as if every file were mapped whole.
WAV files are entirely raw. For BSPs, blocks wholly inside texture/lightmap
lumps may compress; all other blocks stay raw, making every other lump map-able.

Compression is lossless. No files are removed and no sound is resampled by the
packer. The target mixer handles the two original 16-bit WAVs as well as the
8-bit WAVs; all sound contents remain intact in Flash.
