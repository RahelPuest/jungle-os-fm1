# Sample bank JOB1 and its transfer protocol

## Where

Break Surgeon's flash bytes 0xA1000..0xDBFFF (241,664 B), in fm1-base's data area, apart from the firmware, the
store objects, the panel calibration and the unit key. Read in place by the render (through the XIP cache).

## Format (little-endian)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `JOB1` |
| 4 | 2 | version = 1 |
| 6 | 2 | count of breaks (1..8) |
| 8 | 4 | total length of the bank in bytes |
| 12 | 4 | CRC-32 (zlib / IEEE) of bytes [32, total) |
| 16 | 16 | reserved (0) |
| 32 + 256 i | 256 | entry i (below) |
| … | | PCM, each break's at its entry's offset |

Entry:

| Offset | Size | Field |
|---|---|---|
| 0 | 16 | name, ASCII, 0-padded |
| 16 | 4 | PCM offset from the bank's start (a multiple of 4, after the entries) |
| 20 | 4 | frames |
| 24 | 4 | sample rate, 8,000..48,000 Hz (the tool uses 22,059 Hz: half the FM-1's 44,117.6) |
| 28 | 2 | tempo × 100 (40..300 BPM) |
| 30 | 1 | bars |
| 31 | 1 | slices (1..32) |
| 32 | 1 | flags (0) |
| 33 | 3 | reserved |
| 36 | 128 | slice start frames ×32 (u32; strictly increasing, < frames) |
| 164 | 32 | slice class ×32: 0 none, 1 kick, 2 snare, 3 hat, 4 other (heuristic) |
| 196 | 32 | slice gain ×32 (128 = unity) |
| 228 | 28 | reserved |

PCM: signed 16-bit mono. The firmware (`jb_bank_parse`) refuses a bank whose magic, version, length, CRC, any
entry's bounds, rate, tempo or slice table does not check; then Break Surgeon plays its built-in test break.

## Transfer: SysEx `F0 7D 4A 42 …` (0x7D: non-commercial; "JB")

| Command | Bytes after the header | Device does |
|---|---|---|
| 01 BEGIN | length (5 × 7 bits, LSB first) | refuses while playing (status 1) or if too big (2); detaches the bank (the test break plays) |
| 02 DATA | offset (5 × 7 bits), payload packed 7-bit: per group of 7 bytes one byte of their high bits (bit i = byte i), then the 7 bytes' low 7 bits | offsets must follow on exactly (3); erases each 4 KiB sector before its first write (4 on a flash error) |
| 03 END | — | all bytes there (3 if not), parses the bank in flash (5 + the parser's code if it does not check), attaches it |
| 04 INFO | — | status 0 with a bank (value: its count), 5 without |

Every command is answered `F0 7D 4A 42 7F <cmd> <status> <value: 5 × 7 bits> F7`. A DATA answer's value is the
next offset expected. `tools/sample-prep/jo_send_bank.py` waits for each answer (3 s) and stops at the first
non-zero status. 512 payload bytes per DATA frame (586 bytes of SysEx, under fm1-base's 640).

Flash erases stall the FM-1's audio, which is why the transport must be stopped. A transfer cut short leaves no
valid bank (the CRC fails) and the firmware plays the test break; send it again.
