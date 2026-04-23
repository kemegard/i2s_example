# I2S Audio Example — nRF54L15

A minimal Zephyr application that demonstrates continuous I2S audio streaming on the nRF54L15, including a hardware loopback test to verify data integrity end-to-end.

## What it does

1. **Generates** two sine waves in memory:
   - Left channel: 440 Hz
   - Right channel: 880 Hz
   - 8 kHz sample rate, 16-bit stereo, 32 samples per block

2. **Streams continuously** using the I2S peripheral in master mode, feeding blocks from a TX memory slab into the DMA pipeline.

3. **Verifies loopback** by reading back each transmitted block on the RX path and comparing samples against the expected sine wave (accounting for the 2-sample hardware pipeline delay). The running error count is printed every 100 blocks:
   ```
   Blocks: 100  RX errors: 0
   Blocks: 200  RX errors: 0
   ```

## Platform

| Item | Value |
|---|---|
| Board | `nrf54l15dk/nrf54l15/cpuapp` |
| SDK | nRF Connect SDK v3.3.0-rc2 |
| Zephyr | v4.3.99-22eb89901c98 |
| Toolchain | Zephyr SDK 0.17.0 (ARM GCC 12.2.0) |

## nRF54L15 I2S notes

- The nRF54L15 I2S peripheral **cannot run TX-only**. Both TX and RX must be active simultaneously (`I2S_DIR_BOTH`). This is controlled by `CONFIG_I2S_TEST_USE_I2S_DIR_BOTH`, which defaults to `y` for this SoC.
- The hardware introduces a **2-frame (4-sample) pipeline delay**: the first two stereo frames of each received block belong to the end of the previous transmitted block. The first RX block is skipped during verification to absorb this startup transient.

## Pin assignment

Configured in [boards/nrf54l15dk_nrf54l15_cpuapp.overlay](boards/nrf54l15dk_nrf54l15_cpuapp.overlay). Matches the official Zephyr I2S test suite overlay for this board.

| Signal | Pin |
|---|---|
| I2S_SCK_M | P1.11 |
| I2S_LRCK_M | P1.12 |
| I2S_SDOUT | P1.8 |
| I2S_SDIN | P1.9 |

## Loopback test

Wire **P1.8 (SDOUT) → P1.9 (SDIN)** on the expansion header. With the wire connected the error count should remain at 0. Without the wire all samples will mismatch (floating input).

## Building and flashing

```bash
cd <west-workspace>   # e.g. D:\work\ncs\v3.3.0-rc2
west build -b nrf54l15dk/nrf54l15/cpuapp \
    -s <path-to-this-folder> \
    -d <path-to-this-folder>/build \
    --pristine
west flash -d <path-to-this-folder>/build
```

## Serial output

Connect to VCOM1 (e.g. COM90) at **115200 baud**. Expected output after reset:

```
=== I2S Audio Example (nRF54L15) ===
I2S device ready
I2S configured: 8000 Hz, 16-bit stereo
I2S streaming started
Blocks: 100  RX errors: 0
Blocks: 200  RX errors: 0
...
```

- Transmission progress (every 100 blocks)
- Any errors encountered

Serial settings:
- Baud rate: 115200
- Data bits: 8
- Stop bits: 1
- Parity: None

## Project Structure

```
i2s_audio_example/
├── CMakeLists.txt              # Build configuration
├── prj.conf                     # Project configuration
├── README.md                    # This file
├── boards/
│   └── nrf54l15dk_nrf54l15_cpuapp.overlay  # Device tree overlay
└── src/
    └── main.c                   # Main application
```

## How It Works

1. **Initialization**:
   - Gets the I2S device from device tree
   - Generates sine wave lookup tables
   - Configures I2S as master with stereo output

2. **Pre-filling**:
   - Fills the I2S TX queue with initial audio blocks
   - This prevents underruns when transmission starts

3. **Continuous Streaming**:
   - Starts I2S transmission with `I2S_TRIGGER_START`
   - Continuously writes audio blocks in a loop
   - I2S driver manages buffer queuing automatically

4. **Buffer Management**:
   - Uses Zephyr memory slabs for efficient buffer allocation
   - Allocates → fills → writes → driver manages → repeats

## Troubleshooting

### I2S device not ready
- Check that the I2S peripheral is enabled in the device tree overlay
- Verify pin assignments don't conflict with other peripherals

### No audio output
- Check physical connections to DAC/audio device
- Verify the DAC is configured for I2S format (not left/right justified)
- Use an oscilloscope to verify clock signals on SCK and LRCK pins
- Check that SDOUT has data transitions

### Buffer allocation failures
- Increase `BLOCK_COUNT` in main.c
- Check system memory availability

## Notes

- The I2S peripheral operates as master, generating both bit and frame clocks
- Audio data is in standard I2S format (MSB first, data delayed by 1 bit clock)
- The example uses floating-point math for sine wave generation (requires FPU)
- Memory slabs provide deterministic buffer allocation for real-time audio

## License

SPDX-License-Identifier: Apache-2.0
