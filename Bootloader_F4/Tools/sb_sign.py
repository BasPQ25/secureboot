"""
Semnare imagini pentru secure boot-ul din Bootloader_F4 (ECDSA-P256 + SHA-256).

Utilizare:
    pip install ecdsa
    python sb_sign.py keygen            # o singura data: chei + boot_pubkey.h
    python sb_sign.py sign app.bin      # -> app_signed.bin (header + aplicatie)

Formatul slotului de la __slot_start__ (0x0800C000), little-endian:
    +0x000  magic  'SBH1'   (4 B)   scris ultimul de bootloader, dupa verificare
    +0x004  size            (4 B)   lungimea aplicatiei, multiplu de 4
    +0x008  semnatura r||s  (64 B)  ECDSA-P256 peste SHA-256(aplicatie[0:size])
    +0x048  0xFF ...                pana la 512 B (aliniere VTOR)
    +0x200  aplicatia (tabela de vectori la __app_start__)

Cheia privata sta DOAR in SafetyProject/keys (in afara repo-ului): cine o are
poate semna firmware acceptat de placa.
"""
import hashlib
import os
import struct
import sys

from ecdsa import NIST256p, SigningKey
from ecdsa.util import sigencode_string

HEADER_SIZE = 0x200
HEADER_MAGIC = 0x31484253               # 'SBH1' in memorie (little-endian)

HERE = os.path.dirname(os.path.abspath(__file__))
KEY_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "..", "SafetyProject", "keys"))
PRIV_PEM = os.path.join(KEY_DIR, "sb_private.pem")
PUB_PEM = os.path.join(KEY_DIR, "sb_public.pem")
PUBKEY_H = os.path.normpath(os.path.join(HERE, "..", "Core", "Inc", "module", "boot_pubkey.h"))


def load_private_key() -> SigningKey:
    if not os.path.exists(PRIV_PEM):
        raise RuntimeError(f"nu exista {PRIV_PEM}: ruleaza intai 'python sb_sign.py keygen'")
    with open(PRIV_PEM, "rb") as f:
        return SigningKey.from_pem(f.read())


def build_signed_image(image: bytes) -> bytes:
    """Header semnat + aplicatie, gata de scris de la __slot_start__."""
    if len(image) == 0 or len(image) % 4 != 0:
        raise RuntimeError("imaginea trebuie sa aiba lungime nenula, multiplu de 4")

    digest = hashlib.sha256(image).digest()
    # RFC 6979: nonce derivat din cheie + mesaj, nu depinde de un RNG bun
    sig = load_private_key().sign_digest_deterministic(
        digest, hashfunc=hashlib.sha256, sigencode=sigencode_string)

    header = struct.pack("<II", HEADER_MAGIC, len(image)) + sig
    header += b"\xFF" * (HEADER_SIZE - len(header))
    return header + image


def keygen() -> None:
    if os.path.exists(PRIV_PEM):
        raise RuntimeError(f"{PRIV_PEM} exista deja: sterge-l manual daca vrei chei noi "
                           "(imaginile semnate cu cheia veche nu vor mai porni)")
    os.makedirs(KEY_DIR, exist_ok=True)

    sk = SigningKey.generate(curve=NIST256p)
    pub = sk.get_verifying_key().to_string()        # X||Y, 64 B, big-endian
    with open(PRIV_PEM, "wb") as f:
        f.write(sk.to_pem())
    with open(PUB_PEM, "wb") as f:
        f.write(sk.get_verifying_key().to_pem())

    rows = ",\n".join("    " + ", ".join(f"0x{b:02X}" for b in pub[i:i + 8])
                      for i in range(0, len(pub), 8))
    with open(PUBKEY_H, "w", newline="\n") as f:
        f.write(f"""/*
 * boot_pubkey.h
 *
 *  GENERAT de Tools/sb_sign.py keygen - nu edita de mana.
 *
 *  Cheia publica ECDSA-P256 (X||Y, big-endian) cu care bootloader-ul
 *  verifica imaginile. Perechea privata: SafetyProject/keys/sb_private.pem
 */

#ifndef INC_MODULE_BOOT_PUBKEY_H_
#define INC_MODULE_BOOT_PUBKEY_H_

#include <stdint.h>

static const uint8_t BOOT_PUBKEY[64] =
{{
{rows}
}};

#endif /* INC_MODULE_BOOT_PUBKEY_H_ */
""")
    print(f"cheie privata : {PRIV_PEM}")
    print(f"cheie publica : {PUB_PEM}")
    print(f"header C      : {PUBKEY_H}")


def main() -> None:
    if len(sys.argv) == 2 and sys.argv[1] == "keygen":
        keygen()
    elif len(sys.argv) == 3 and sys.argv[1] == "sign":
        with open(sys.argv[2], "rb") as f:
            image = f.read()
        image += b"\xFF" * (-len(image) % 4)
        out = os.path.splitext(sys.argv[2])[0] + "_signed.bin"
        with open(out, "wb") as f:
            f.write(build_signed_image(image))
        print(f"{out}: {len(image)} B aplicatie + {HEADER_SIZE} B header")
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
