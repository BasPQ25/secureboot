"""
Trimite aplicatia Safety compilata (Safety/Debug/Safety.elf) prin bootloader-ul
Bootloader_F4, pe USART2 (VCOM-ul debugger-ului de pe Nucleo).

Utilizare (din secureboot/):
    flash_safety.bat                      dublu-click, totul automat
    python flash_safety.py [--port COM3] [--elf cale.elf] [--no-reset]

Pasi:
  1. citeste Safety.elf si construieste imaginea binara de la __app_start__
     (adresele vin din Common/memory_map.ld, prin simbolurile din ELF),
     apoi o semneaza (ECDSA-P256, Tools/sb_sign.py): header 512 B + aplicatie
  2. gaseste portul serial al debugger-ului (J-Link OB sau ST-LINK)
  3. reseteaza placa prin J-Link Commander, daca exista; altfel cere RESET manual
  4. CONNECT in fereastra de 3 s a bootloader-ului, apoi ERASE, WRITE, VERIFY, JUMP

Protocolul este cel din Bootloader_F4/Tools/boot_host.py (refolosit, nu copiat).
"""
import argparse
import glob
import os
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "Bootloader_F4", "Tools"))

import serial                       # noqa: E402  (pyserial)
import serial.tools.list_ports      # noqa: E402
import boot_host                    # noqa: E402
import sb_sign                      # noqa: E402  (ecdsa)

DEFAULT_ELF = os.path.join(ROOT, "Safety", "Debug", "Safety.elf")
JLINK_DEVICE = "STM32F446RE"
DEBUG_VIDS = {0x1366: "J-Link", 0x0483: "ST-LINK"}
CONNECT_WINDOW_S = 30.0             # cat asteptam RESET-ul manual


# ------------------------------------------------------------------ ELF

def elf_symbols(data: bytes, names: set) -> dict:
    """Valorile simbolurilor cerute, din .symtab (ELF32 little-endian)."""
    e_shoff, = struct.unpack_from("<I", data, 0x20)
    e_shentsize, e_shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = [struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize)
                for i in range(e_shnum)]
    found = {}
    for sh in sections:
        if sh[1] != 2:                              # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]                    # sh_link -> .strtab
        for off in range(sh[4], sh[4] + sh[5], 16):
            st_name, st_value = struct.unpack_from("<II", data, off)
            start = strtab[4] + st_name
            name = data[start:data.index(b"\0", start)].decode()
            if name in names:
                found[name] = st_value
    return found


def elf_to_image(path: str) -> tuple:
    """Imaginea de scris de la __app_start__, umpluta cu 0xFF intre segmente."""
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise RuntimeError(f"{path} nu este un ELF32 little-endian")

    syms = elf_symbols(data, {"__app_start__", "__app_end__"})
    if len(syms) != 2:
        raise RuntimeError("ELF-ul nu contine __app_start__/__app_end__: "
                           "nu a fost linkat cu Common/memory_map.ld")
    app_start, app_end = syms["__app_start__"], syms["__app_end__"]

    # Nicio sectiune reala nu are voie in flash inainte de __app_start__ (headerul semnat)
    e_shoff, = struct.unpack_from("<I", data, 0x20)
    e_shentsize, e_shnum = struct.unpack_from("<HH", data, 0x2E)
    for i in range(e_shnum):
        _name, _type, sh_flags, sh_addr, _off, sh_size = struct.unpack_from(
            "<IIIIII", data, e_shoff + i * e_shentsize)
        if (sh_flags & 0x2) and sh_size and 0x08000000 <= sh_addr < app_start:   # SHF_ALLOC
            raise RuntimeError(f"sectiune la 0x{sh_addr:08X}, inainte de __app_start__ "
                               f"0x{app_start:08X}")

    e_phoff, = struct.unpack_from("<I", data, 0x1C)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x2A)
    segments = []
    for i in range(e_phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from(
            "<IIIII", data, e_phoff + i * e_phentsize)
        if p_type != 1 or p_filesz == 0:            # doar PT_LOAD cu continut
            continue
        # Vectorii la 0x...C200 nu sunt aliniati la pagina ELF (4 KB): linker-ul
        # incepe segmentul mai jos, cu headerele ELF ca umplutura. Se taie.
        if p_paddr < app_start <= p_paddr + p_filesz:
            skip = app_start - p_paddr
            p_paddr, p_offset, p_filesz = app_start, p_offset + skip, p_filesz - skip
        if p_paddr < app_start or p_paddr + p_filesz > app_end:
            raise RuntimeError(f"segment la 0x{p_paddr:08X} (+{p_filesz}) in afara "
                               f"zonei aplicatiei 0x{app_start:08X}-0x{app_end:08X}")
        segments.append((p_paddr, data[p_offset:p_offset + p_filesz]))

    if not segments:
        raise RuntimeError("ELF-ul nu are segmente de incarcat")
    end = max(addr + len(chunk) for addr, chunk in segments)
    image = bytearray(b"\xFF" * (end - app_start))
    for addr, chunk in segments:
        image[addr - app_start:addr - app_start + len(chunk)] = chunk
    image += b"\xFF" * (-len(image) % 4)            # aliniere la 4 octeti

    # Aceleasi conditii ca Boot_IsAppValid(), verificate inainte de transfer
    sp, reset = struct.unpack_from("<II", image, 0)
    if not (0x20000000 < sp <= 0x20020000) or not (reset & 1) or \
            not (app_start <= reset < app_end):
        raise RuntimeError(f"tabela de vectori invalida la 0x{app_start:08X} "
                           f"(SP=0x{sp:08X}, Reset=0x{reset:08X})")
    return bytes(image), app_start


def warn_if_stale(elf: str) -> None:
    """Avertizeaza daca sursele Safety sunt mai noi decat ELF-ul."""
    elf_time = os.path.getmtime(elf)
    patterns = [os.path.join(ROOT, "Safety", "Core", "**", ext) for ext in ("*.c", "*.cpp", "*.h")]
    patterns.append(os.path.join(ROOT, "Common", "*.ld"))
    newer = [f for p in patterns for f in glob.glob(p, recursive=True)
             if os.path.getmtime(f) > elf_time]
    if newer:
        print(f"ATENTIE: {len(newer)} fisier(e) sursa mai noi decat Safety.elf "
              f"(ex. {os.path.relpath(newer[0], ROOT)}). Fa Build in CubeIDE.")


# --------------------------------------------------------------- placa

def find_port() -> str:
    ports = [p for p in serial.tools.list_ports.comports() if p.vid in DEBUG_VIDS]
    if not ports:
        all_ports = ", ".join(p.device for p in serial.tools.list_ports.comports()) or "niciunul"
        raise RuntimeError(f"nu gasesc portul debugger-ului (porturi: {all_ports}); "
                           "foloseste --port COMx")
    port = ports[0]
    print(f"Port: {port.device} ({DEBUG_VIDS[port.vid]}: {port.description})")
    return port.device


def find_jlink() -> str:
    candidates = sorted(glob.glob(r"C:\Program Files*\SEGGER\JLink*\JLink.exe"), reverse=True)
    candidates += glob.glob(r"C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins"
                            r"\com.st.stm32cube.ide.mcu.externaltools.jlink.win32_*\tools\bin\JLink.exe")
    return candidates[0] if candidates else ""


def reset_board() -> bool:
    """Reset prin SWD cu J-Link Commander; False daca nu se poate."""
    jlink = find_jlink()
    if not jlink:
        return False
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as f:
        f.write("r\ng\nqc\n")
        script = f.name
    try:
        res = subprocess.run([jlink, "-device", JLINK_DEVICE, "-if", "SWD", "-speed", "4000",
                              "-autoconnect", "1", "-ExitOnError", "1", "-NoGui", "1",
                              "-CommandFile", script],
                             capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        return False
    finally:
        os.remove(script)
    return res.returncode == 0


def connect(ser: serial.Serial, window_s: float) -> int:
    """CONNECT repetat in tacere pana raspunde bootloader-ul (send() ar afisa fiecare reincercare)."""
    frame = struct.pack("<BBH", boot_host.SOF, boot_host.CMD_CONNECT, 0)
    frame += struct.pack("<I", boot_host.stm32_crc(frame))
    deadline = time.time() + window_s
    while time.time() < deadline:
        ser.reset_input_buffer()
        ser.write(frame)
        resp = boot_host.read_response(ser, 0.2)
        if len(resp) == 2 and resp[0] == boot_host.ACK:
            return resp[1]
    raise RuntimeError("bootloader-ul nu raspunde la CONNECT "
                       "(placa resetata? Bootloader_F4 e incarcat la 0x08000000?)")


# ---------------------------------------------------------------- main

def main() -> None:
    ap = argparse.ArgumentParser(description="Incarca Safety.elf prin Bootloader_F4 (USART2).")
    ap.add_argument("--port", help="portul serial (implicit: detectat automat)")
    ap.add_argument("--elf", default=DEFAULT_ELF, help="ELF-ul aplicatiei")
    ap.add_argument("--no-reset", action="store_true", help="nu reseta placa prin J-Link")
    args = ap.parse_args()

    if not os.path.isfile(args.elf):
        raise RuntimeError(f"nu exista {args.elf}; fa Build la Safety in CubeIDE")
    app, base = elf_to_image(args.elf)
    print(f"ELF:  {os.path.relpath(args.elf, ROOT)} "
          f"({time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(os.path.getmtime(args.elf)))})")
    print(f"Aplicatie: {len(app)} octeti la 0x{base:08X}")
    warn_if_stale(args.elf)

    # Slotul incepe cu headerul semnat, inaintea vectorilor aplicatiei
    image = sb_sign.build_signed_image(app)
    print(f"Semnata: {len(image)} octeti la 0x{base - sb_sign.HEADER_SIZE:08X} (header + aplicatie)")

    port = args.port or find_port()
    try:
        ser = serial.Serial(port, 115200, bytesize=8, parity=serial.PARITY_EVEN, stopbits=1)
    except serial.SerialException as exc:
        raise RuntimeError(f"nu pot deschide {port} (e deschis in alt terminal?): {exc}")

    with ser:
        # Portul e deschis inainte de reset, ca sa prindem fereastra de 3 s
        if not args.no_reset and reset_board():
            print("Placa resetata prin J-Link.")
            window = 5.0
        else:
            print(f"Apasa butonul RESET (negru) de pe placa, ai {CONNECT_WINDOW_S:.0f} s...")
            window = CONNECT_WINDOW_S

        ver = connect(ser, window)
        print(f"Bootloader v{ver}")
        if ver != boot_host.BOOT_VERSION:
            raise RuntimeError(f"bootloader v{ver}, astept v{boot_host.BOOT_VERSION} "
                               "(fara semnaturi): reflasheaza Bootloader_F4 prin debugger")

        print(f"Stergere {len(image)} octeti...")
        boot_host.send(ser, boot_host.CMD_ERASE, struct.pack("<I", len(image)), timeout=15.0)

        for off in range(0, len(image), boot_host.MAX_DATA):
            chunk = image[off:off + boot_host.MAX_DATA]
            boot_host.send(ser, boot_host.CMD_WRITE, struct.pack("<I", off) + chunk)
            print(f"\rScriere {off + len(chunk)}/{len(image)}", end="", flush=True)
        print()

        print("Verificare CRC + semnatura...")
        boot_host.send(ser, boot_host.CMD_VERIFY,
                       struct.pack("<II", len(image), boot_host.stm32_crc(image)), timeout=5.0)

        print("Pornire aplicatie")
        boot_host.send(ser, boot_host.CMD_JUMP)
    print("Gata: Safety ruleaza.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError) as exc:
        print(f"\nEROARE: {exc}")
        sys.exit(1)
