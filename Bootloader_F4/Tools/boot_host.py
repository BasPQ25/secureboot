"""
Host pentru bootloader-ul Bootloader_F4 (protocol descris in Core/Inc/module/boot.h).

Utilizare:
    pip install pyserial ecdsa
    python sb_sign.py sign aplicatie.bin      # -> aplicatie_signed.bin
    python boot_host.py COM5 aplicatie_signed.bin

Aplicatia trebuie linkata la 0x0800C200 (dupa headerul de 512 B de la 0x0800C000;
S0-S1 = bootloader, S2 = date). Bootloader-ul accepta doar imagini semnate.
Placa: reset (sau tine B1 apasat la reset) si porneste scriptul in primele 3 s.
"""
import struct
import sys
import time

import serial

SOF, ACK, NACK = 0xA5, 0x79, 0x1F
CMD_CONNECT, CMD_ERASE, CMD_WRITE, CMD_VERIFY, CMD_JUMP = 0x10, 0x20, 0x30, 0x40, 0x50
MAX_DATA = 256
RETRIES = 3
BOOT_VERSION = 0x02                 # bootloader cu imagini semnate (boot.h)

ERRORS = ["OK", "TIMEOUT", "SOF", "FRAME", "LEN", "CRC", "CMD", "ADDR",
          "NOT_ERASED", "FLASH", "VERIFY", "NO_APP", "SIGNATURE"]


def stm32_crc(data: bytes) -> int:
    """CRC32 identic cu perifericul CRC al STM32F4 (cuvinte LE, poly 0x04C11DB7)."""
    assert len(data) % 4 == 0
    crc = 0xFFFFFFFF
    for (word,) in struct.iter_unpack("<I", data):
        crc ^= word
        for _ in range(32):
            crc = ((crc << 1) ^ 0x04C11DB7) if crc & 0x80000000 else (crc << 1)
            crc &= 0xFFFFFFFF
    return crc


def read_response(ser: serial.Serial, timeout: float) -> bytes:
    """Asteapta cei 2 octeti de raspuns prin polling pe in_waiting.

    Un ser.read() blocant pe driverul J-Link CDC (VCOM J-Link OB) pierde sau
    corupe octetii care sosesc cat timp citirea e in asteptare; citirea doar a
    ce e deja in bufferul driverului e sigura (masurat: ~40% vs 100% reusite).
    """
    deadline = time.time() + timeout
    while ser.in_waiting < 2 and time.time() < deadline:
        time.sleep(0.001)
    return ser.read(min(ser.in_waiting, 2))


def send(ser: serial.Serial, cmd: int, payload: bytes = b"", timeout: float = 1.0) -> int:
    frame = struct.pack("<BBH", SOF, cmd, len(payload)) + payload
    frame += struct.pack("<I", stm32_crc(frame))
    for attempt in range(RETRIES):
        ser.reset_input_buffer()
        ser.write(frame)
        resp = read_response(ser, timeout)
        if len(resp) == 2 and resp[0] == ACK:
            return resp[1]
        if len(resp) == 2 and resp[0] == NACK:
            err = ERRORS[resp[1]] if resp[1] < len(ERRORS) else hex(resp[1])
            # doar erorile de transmisie merita retrimise
            if err not in ("CRC", "FRAME", "LEN"):
                raise RuntimeError(f"cmd 0x{cmd:02X}: NACK {err}")
            print(f"  NACK {err}, retrimit ({attempt + 1}/{RETRIES})")
        else:
            print(f"  fara raspuns, retrimit ({attempt + 1}/{RETRIES})")
    raise RuntimeError(f"cmd 0x{cmd:02X}: esuat dupa {RETRIES} incercari")


def main() -> None:
    port, path = sys.argv[1], sys.argv[2]
    image = open(path, "rb").read()
    image += b"\xFF" * (-len(image) % 4)          # aliniere la 4 octeti

    ser = serial.Serial(port, 115200, bytesize=8, parity=serial.PARITY_EVEN, stopbits=1)

    print("Conectare...")
    deadline = time.time() + 5
    while True:
        try:
            ver = send(ser, CMD_CONNECT, timeout=0.2)
            break
        except RuntimeError:
            if time.time() > deadline:
                raise
    print(f"Bootloader v{ver}")
    if ver != BOOT_VERSION:
        raise RuntimeError(f"bootloader v{ver}, astept v{BOOT_VERSION}: reflasheaza Bootloader_F4")

    print(f"Stergere {len(image)} octeti...")
    send(ser, CMD_ERASE, struct.pack("<I", len(image)), timeout=15.0)

    for off in range(0, len(image), MAX_DATA):
        chunk = image[off:off + MAX_DATA]
        send(ser, CMD_WRITE, struct.pack("<I", off) + chunk)
        print(f"\rScriere {off + len(chunk)}/{len(image)}", end="")
    print()

    print("Verificare CRC + semnatura...")
    send(ser, CMD_VERIFY, struct.pack("<II", len(image), stm32_crc(image)), timeout=5.0)

    print("Pornire aplicatie")
    send(ser, CMD_JUMP)
    print("Gata.")


if __name__ == "__main__":
    main()
