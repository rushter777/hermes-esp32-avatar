# Wire protocol

**Version: 1.** As implemented by the current firmware (`firmware/hermes_esp32_avatar/`) and the
current host plugin (`hermes-plugin/jin-esp32-bridge/`). This documents what the code does, not
what a future revision should do.

> **Current working magic value: `JIN1`.** It is four ASCII bytes, and it is the literal value both
> sides use today. It is a protocol identifier only; nothing else about the avatar's identity
> travels on the wire.

---

## Transport

Plain **TCP**, IPv4, no TLS. The device is the server; the host is the client.

Not UDP, not HTTP, not WebSocket, not MQTT. After an 8-byte preamble the connection carries an
unframed PCM byte stream.

The port is a fixed constant on both sides. The device also advertises itself over mDNS, which is
how the host normally finds it.

### `TCP_NODELAY`

Set by both sides, immediately after the connection is established.

The host writes roughly 62.5 small payloads per second, which is the pattern Nagle's algorithm
coalesces. Without this option the device sees bursts instead of a steady stream, and mouth timing
degrades.

---

## Message format

One message per utterance, consisting of a fixed 8-byte header followed by PCM.

### Header

| Offset | Size | Content |
|---:|---:|---|
| 0 | 4 | ASCII magic, **`JIN1`** |
| 4 | 4 | Sample rate, unsigned 32-bit, **little-endian** |

Equivalent to `struct.Struct("<4sI")`. **No length field, no version field, no flags, no
sequence number, no payload size.**

### Payload

**Signed 16-bit little-endian mono PCM.**

The current sample rate is **16000 Hz**.

The header's rate field exists so the receiver could adapt, but the current receiver validates it
rather than adapting: it requires exactly 16000 and closes the connection on any other value. So
while the field is architecturally a parameter, in version 1 the rate is effectively fixed, and a
host that sends another rate simply gets rejected.

---

## Blocking and pacing

Audio is delivered in **exactly 512-byte blocks**.

| Property | Value |
|---|---|
| Block size | 512 bytes |
| Samples per block | 256 |
| Duration per block | **exactly 16.000 ms** at 16 kHz |
| Block rate | 62.5 per second |

512 bytes = 256 samples x 2 bytes per sample. This is a hard contract, not a preference: the
receiver reconstructs its sample timeline from the block size and its own clock, and there is no
per-block framing to fall back on.

### Final-block padding

The last block of an utterance is padded with silence (zero bytes) so that **every block on the
wire is a full 512 bytes**. A partial trailing block is never sent.

Practical consequence: a clip whose audio length is not a multiple of 16 ms sends slightly more
than its audio contains. A 0.500 s clip sends 32 blocks, which is 0.512 s.

### Real-time pacing

The host transmits at **1x real time**: block *n* is sent no earlier than `t0 + n x 16 ms`, against
a monotonic clock and measured from the first audio block.

Two details matter:

- The deadline is **absolute**, computed from the origin, not accumulated per block, so error
  cannot build up over a long utterance.
- The pacing clock starts with the **first audio block**, not with the connection or the header.

Because delivery is real time, transmission occupies the same wall-clock span as the speech. That
is what lets the host treat "the send is still running" as equivalent to "audio is playing", which
the fallback and cancellation rules below depend on.

---

## Connection lifecycle

**One connection and one header per utterance.** This is the central rule of the protocol.

```
host                                    device
  |  connect  --------------------------->  accept
  |  header (JIN1 + rate)  -------------->  validate, or close and continue listening
  |  block, block, block ...  ----------->  decode, play, drive mouth
  |  close  ---------------------------->   end of utterance
  |                                         write a short silence tail
  |                                         mouth returns to its resting frame
  |                                         sample timeline reset
  |                                         back to accept
```

The connection carries a single utterance. It is **not** reused and it is **not** held open
between utterances. An idle persistent connection would leave the device unable to tell "paused"
from "finished", so it is not part of the protocol.

Writes are not synchronised to anything else. There is no handshake beyond the header, no
readiness signal, and no negotiation of any kind.

---

## Connection closure is the end-of-utterance signal

There is no end marker, no terminator, and no length prefix. **Closing the connection is how the
utterance ends.** It is the only end-of-utterance signal in version 1.

On closure the device:

1. writes a short run of silence to the amplifier, so the speaker does not click,
2. forces the mouth to its flat resting frame,
3. clears its streaming flag,
4. resets its sample timeline and mouth event queue,
5. closes its side and returns to accepting connections.

Because this is the only end signal, a host that fails to close the connection leaves the device
believing the utterance is still in progress.

---

## Receiver behaviour

Recorded here because the host contract depends on it.

### Header validation

The device reads 8 bytes and requires **both** the `JIN1` magic **and** a rate of 16000. If either
fails it closes the connection immediately and returns to accepting connections. A malformed header
is therefore a silent no-op from the host's point of view: the audio is discarded.

### Buffering

The device waits until at least 512 bytes are available before reading a block, and accepts any
even byte count up to 512. It tolerates short reads; it does not require that a network read
returns exactly one block.

### Mouth timeline

Mouth frames are timestamped from their **sample position in the stream**, not from packet arrival
time, and playback is offset by a calibrated output latency. This keeps the mouth locked to the
audio when network delivery is irregular, which it is: an absolute schedule plus TCP means packets
can arrive in bursts even though the host paces its writes.

The mouth is driven by a broadband amplitude envelope with asymmetric attack and release, not by
phoneme classification. It indicates speech activity and loudness, not specific sounds.

---

## Failure and cancellation

**Failure before any audio is sent.** The connection was never established, so nothing was
delivered. The host is free to use its own local playback instead. This is a host-side policy, not
a protocol feature.

**Failure after audio has begun.** A connection dropped mid-utterance leaves the device with a
partially played utterance. The device notices the disconnect, stops, writes its silence tail,
returns the mouth to rest, and resets. The host must **not** re-send the utterance from the
beginning to the same or a different sink; the opening portion already played aloud.

**Cancellation.** The host stops writing and closes the connection. The device does the same
end-of-utterance work. Cancellation is therefore indistinguishable, at the protocol level, from a
normal end of utterance that happens early: both are just a closed connection. Nothing in the
protocol distinguishes "finished speaking" from "was interrupted", so a host that needs that
distinction must keep it on its own side.

**Restart.** Because closure resets the timeline, a new utterance is always a new connection with a
new header. There is no resume and no partial re-send.

---

## Explicitly not in version 1

None of the following exist. They are listed because their absence is part of the interface:

| Absent | Consequence |
|---|---|
| **Authentication** | Any host that can reach the port can make the device speak |
| **Encryption** | Audio and headers are readable on the wire |
| **Expression or state messages** | The device's face cannot be driven from the host; only audio reaches it |
| **Acknowledgements** | The host never reads from the socket, so delivery is unconfirmed |
| **Negotiation** | Rate, block size, port and magic are all fixed by convention |
| **Protocol version field** | The magic is the only protocol identifier |
| **Length prefix or framing** | Block boundaries are inferred from size and the sample clock |
| **Flow control** | Beyond TCP's own window |
| **Multi-channel audio** | Mono only; the device duplicates each sample to both I2S slots |
| **Keepalive or heartbeat** | An idle connection has no meaning |

The practical security consequence is worth stating plainly: **on a trusted local network this is
acceptable, and beyond one it is not.** Anyone with access to the network segment can make the
avatar speak, and can read the audio being sent.

---

## Future revisions (recommendation only, not implemented)

Two changes are worth making before this protocol is published for general use, and neither is in
place today:

1. **A neutral magic.** `JIN1` ties a public protocol to one prototype's name. A neutral
   identifier would let the protocol outlive the avatar that produced it.

2. **An explicit version field**, ideally as a message-type or version byte after the magic. The
   magic currently does double duty as identity and version, which means any protocol change would
   be indistinguishable from a corrupt or spoofed header.

These belong together, and the **cost of both is lowest now**, while exactly one device exists. A
version field is also the natural carrier for the next capability the system needs: a message type
for **expression and operational state**. The wire carries audio only, so the face cannot follow
what the host is doing. That is a protocol change, not a configuration change.

Any revision should accept `JIN1` as a deprecated alias so already-flashed devices keep working,
and should treat the following as frozen in version 1 for compatibility:

| Frozen | Value |
|---|---|
| Magic | `JIN1` |
| Header size | 8 bytes |
| Rate field | uint32 little-endian, 16000 in practice |
| Block size | 512 bytes |
| Sample format | signed int16 little-endian mono |
| End of utterance | connection closure |
| Utterances per connection | exactly one |
