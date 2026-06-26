# Frigidaire NIU ↔ AC UART protocol

Reverse-engineered from direct captures of the NIU. Everything here is observed from bus captures; confidence is marked.

## Physical layer

- Two UART wires (full-duplex pair), **9600 8E1** (8 data bits, even parity, 1 stop bit). 8N1 frames are rejected by the AC before protocol parsing.
- **WIRING NOTE:** the ESP TX must drive ONLY the NIU wire (the AC's input). Driving the AC-output wire puts two push-pull drivers in contention — it corrupts the bus and hangs the AC's comms. Recover by power-cycling the AC at the wall.

## Frame format

```
C6  LEN  TYPE  SEQ  AD  ADDR1[3]  ADDR2[3]  PAYLOAD[..]  CHK
```

- `C6` — start sentinel.
- `LEN` — number of bytes between LEN and CHK (so total frame = LEN + 3).
- `TYPE` — high bit set on the **NIU's** frames (`0x8X`), clear on the AC's (`0x0X`). Low nibble is the high byte of a rolling 16-bit counter: it increments when `SEQ` wraps `FF->00`.
- `SEQ` — per-frame sequence; a request and its reply share a `SEQ`.
- `AD` — constant `0xAD` marker.
- `ADDR1`,`ADDR2` — two 3-char ASCII node IDs from {`AC1`,`NIU`,`ALL`,`SC1`}. **The source is the second triple** (ADDR2); ADDR1 is the destination.
- `CHK` — **XOR of every byte except the leading `C6`**. Verified.

### Payload shapes

- **Poll / idle:** `02 01 00` (NIU poll) -> `02 01 01` (AC "nothing to report").
- **Generic ack / end-of-transaction:** `02 02 07`.
- **Register op:** a nested message `02 0X 0X AD ADDR1 ADDR2 <reg-hi> <reg-lo> <marker> [value...]`.

### Marker grammar (after a register number)

| Marker | Meaning |
|---|---|
| `00` | full read request, answered with `02`/`04` + data |
| `02`,`04` | string/value response to a read |
| `04 <value>` | **SET request from NIU to AC**, answered with `05` |
| `05` | AC accepted a `04` SET request |
| `06 <value>` | state/report record, including AC change events |
| `07` | ack of a `06` report |
| `0E`,`0C` | seen in handshake, unknown right now |

## Transaction model

The NIU polls ~1 Hz. The AC answers in its slot — `02 01 01` if nothing changed, or a `<reg> 06 <value>` report if something did. The NIU acks with `<reg> 07`; the AC closes with `02 02 07`. State values are **event-driven**: they are sent once at connect (full dump) and again only when they change.

### Confirmed control transaction

Operational control is a direct register SET, not a nested event frame:

1. NIU sends `dst=AC1 src=NIU <reg-hi> <reg-lo> 04 <value...>` in place of a poll.
2. AC replies on the same `TYPE/SEQ` with `dst=NIU src=AC1 <reg-hi> <reg-lo> 05`.
3. On a real state change, the AC subsequently reports the new value through the normal nested
   `02 01 02 ... <reg> 06 <value>` event path; the NIU acknowledges that report normally.

For ex, fan Low: `10 02 04 01` -> `10 02 05`, followed by an event containing `10 02 06 01`.

Power is controlled through the mode SET register, not the reported power-state register:

- `10 00 04 00` powers the unit off.
- `10 00 04 01` powers on in Cool mode.
- `10 00 04 03` powers on in Fan-only mode.
- `10 00 04 05` powers on in Dry mode.

Each is answered with `10 00 05`, followed by normal state events. Register `04 01` is read-only power state (`00` off, `02` on).

## Boot handshake (NIU connects)

The real NIU performs the sequence below. The AC remains engaged when the master sends only the discovery opener followed by 1 Hz idle polls, so the identity exchange is **not required for basic bus engagement**. However, that reduced sequence only exposes the special power-off preamble; ordinary power, mode, fan, temperature, and eco reports remained suppressed. The post-discovery initialization is required to register the NIU for the normal event stream and trigger the initial full state dump.

0. **Discovery opener** The NIU's first frame is addressed to `dst = 00 00 00` (not `AC1`), payload `00 01 00` (read reg `0001`). Full frame: `C6 0C 80 <seq> AD 00 00 00 4E 49 55 00 01 00 <chk>`. The AC replies revealing its address — `… AD 4E 49 55 00 00 00 00 01 02 00 41 43 31`, only after that does the master address it as `AC1`. **Opening with `dst=AC1` never engages the AC** The AC's own source stays `00 00 00` until assigned too.
1. **Identity exchange** — NIU reads the AC's identity and writes its own config in:
   - `02 00` -> firmware
   - `00 02` -> eight-digit serial cached by the NIU
   - `00 07` -> nine-digit PNC cached by the NIU
   - `00 23` -> `52 50 31 FF` (`"RP1"` plus an unknown terminator/subtype byte)
   - `00 20` -> NIU status/identity blob containing its MAC and an IPv4 field
2. **Full state dump** — AC reports every register's current value in one burst.
3. **Steady state** — the 1 Hz poll loop.

To read current state at any time, the ESP can either trigger the dump (handshake) or issue a `<reg> 00` read request for a specific register.

## Register map

Format note: temperatures use `06 <unit> 00 <value> 00`, where **unit `00`=°C, `01`=°F**, and `<value>` is the temperature in that unit.

### Confirmed

| Register | Function | Values |
|---|---|---|
| `04 01` | Power state (read-only) | `00` off, `02` on (power-off is preceded by `06 72`) |
| `10 00` | Mode / power command | SET `00`=off; `01`=Cool, `03`=Fan-only, `05`=Dry also power on |
| `10 02` | Fan speed (requested) | `01`=1, `02`=2, `04`=3, `07`=Auto |
| `10 03` | Fan speed (actual) | mirrors `10 02`, but Auto resolves to `01` |
| `04 32` | Setpoint | `06 <unit> 00 <temp> 00` (e.g. C: `…00 00 18 00`=24; F: `…01 00 4B 00`=75) |
| `04 30` | Measured temp, probe 1 (room) | same unit format |
| `04 35` | Measured temp, probe 2 (coil?) | same format |
| `04 20` | Temperature display unit | `00`=°C, `01`=°F |
| `04 26` | Eco mode | `00` off, `01` on |
| `04 28` | Sleep mode | `00` off, `01` on |
| `10 09` | Swing | `00` off, `01` on |
| `04 63` | Child lock | `00` off, `01` on |
| `06 51` | PureAir filter installed | `00` not installed, `01` installed |
| `06 22` | Air quality index (raw) | two-byte big-endian raw value; units/range unknown |
| `06 21` | Air quality category | `00` green, `01` yellow, `02` red (with hysteresis) |
| `04 7C` | Filter status | `03` needs cleaning -> `00` after reset |
| `10 21` | Filter-related | `03` -> `00` on filter reset |
| `06 40` | Filter runtime counter | cleared to ~`0` on filter reset |
| `00 24` | Wi-Fi setup request | nested marker `04`, value `01`; locally acknowledged and otherwise ignored |

### Tentative

| Register | Note |
|---|---|
| `04 A1` | `01` in Dry mode, `00` otherwise — likely a dehumidify flag |
| `10 30`, `04 B5` | multi-byte monotonic counters (runtime/usage); units unknown |
| `10 31`, `10 32`, `06 41` | counters seen at `0` |
| `00 2E`,`00 28`,`04 70`,`04 7E`,`00 EA`,`04 29`,`10 04`,`10 73`,`06 80`,`01 2A`,`00 60`,`00 13` | appear in the state dump; unknown right now |

### Unconfirmed mapping candidates

Potential mappings based on location; not confirmed yet for implementation

| Possible function | Candidate registers | Reason for suspicion |
|---|---|---|
| PureAir/HEPA filter needs replacement | `04 70` (strongest), possibly a state derived from `06 41` | `04 70` is a one-byte value reported beside confirmed filter registers `04 7C` and `10 21`; four-byte `06 41` follows confirmed filter runtime `06 40` and may be a second filter lifetime counter |
| Schedule set/session | `10 73` or `00 EA`; `10 04` remains a conflicting weaker candidate | these are one-byte state-dump values; `10 73` was `00` in an apparently unscheduled capture, while the app capability model contains separate `schedulerSet` and `schedulerSession` booleans |

The app's AC data model also contains separate `filterState`, `hepaFilterState`, `hepaFilterLifeTime`, and `hepaFilterInsertedState` properties, which supports the expectation that the currently unknown dump includes multiple PureAir-related registers.

### Identity (read during handshake)

| Register | Value |
|---|---|
| `02 00` | firmware|
| `00 02` | NIU-cached eight-digit serial |
| `00 07` | NIU-cached nine-digit PNC |
| `00 23` | `"RP1" FF` NIU model/type (exact meaning unknown) |
| `00 20` | NIU config/identity blob |

## Notes

- The input source doesn't matter: front-panel buttons and the IR remote produce **identical**
  bus traffic — the AC just reports its resulting state, never how it was changed.
- Reports follow the display unit, so `04 30`/`04 32` switch between °C and °F values with `04 20`.
- NIU→AC control is confirmed as direct `<reg> 04 <value>` → `<reg> 05`; successful changes are
  then echoed as ordinary `<reg> 06 <value>` events.
