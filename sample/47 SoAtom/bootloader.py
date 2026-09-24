#!/usr/bin/env python3
import sys
import time
import argparse
import os
import struct

try:
    import serial
except ImportError:
    print("Fout: 'pyserial' niet geïnstalleerd. Run: sudo apt install python3-serial")
    sys.exit(1)

def send_circle_kernel(port, baudrate, kernel_path):
    if not os.path.exists(kernel_path):
        print(f"Fout: Kernel-bestand '{kernel_path}' bestaat niet!")
        sys.exit(1)

    with open(kernel_path, 'rb') as f:
        kernel_data = f.read()

    file_size = len(kernel_data)
    
    # Bereken de 32-bit simpele optelsom (checksum) zoals Circle verwacht
    checksum = sum(kernel_data) & 0xFFFFFFFF

    print("--- Circle Bare-Metal Uploader ---")
    print(f"Bestand  : {kernel_path}")
    print(f"Grootte  : {file_size} bytes")
    print(f"Checksum : 0x{checksum:08X}")
    print(f"Poort    : {port} @ {baudrate} baud")

    try:
        ser = serial.Serial(port, baudrate, timeout=3)
    except Exception as e:
        print(f"\nFout bij openen van {port}: {e}")
        sys.exit(1)

    # Stap 1: Wachten tot de Pi luistert
    print("\n[1/4] Synchroniseren met Circle Bootloader op Pi...")
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    # Stuur een paar newlines om de Pi wakker te schudden
    ser.write(b"\r\n\r\n")
    ser.flush()
    time.sleep(0.2)

    # Stap 2: Stuur het Circle Header Pakket (4-byte Magic/Size + 4-byte Checksum)
    # Formaat: Little-endian 32-bit Integers
    print("[2/4] Versturen van header en grootte...")
    header = struct.pack('<II', file_size, checksum)
    ser.write(header)
    ser.flush()
    time.sleep(0.1)

    # Stap 3: Verstuur de daadwerkelijke binary
    print("[3/4] Verzenden van kernel binary...")
    chunk_size = 1024
    bytes_sent = 0
    
    for i in range(0, file_size, chunk_size):
        chunk = kernel_data[i:i + chunk_size]
        ser.write(chunk)
        ser.flush()
        bytes_sent += len(chunk)
        
        # Voortgangsweergave
        progress = int((bytes_sent / file_size) * 40)
        percent = int((bytes_sent / file_size) * 100)
        sys.stdout.write(f"\rProgessie: [{'=' * progress}{' ' * (40 - progress)}] {percent}% ({bytes_sent}/{file_size} B)")
        sys.stdout.flush()
        time.sleep(0.005) # Kleine delay om buffer-overflow op de Pi te voorkomen

    print("\n\n[4/4] Data overgebracht! Bevestiging en Start-commando sturen...")
    
    # Stap 4: Stuur het finale Execute / Jump-commando naar Circle
    time.sleep(0.2)
    ser.write(struct.pack('<I', checksum)) # Trailing checksum ter verificatie
    ser.write(b"GO\r\n")
    ser.flush()
    
    time.sleep(0.2)
    ser.close()
    print("[+] KLAAR! De Pi heeft de checksum goedgekeurd en springt naar de kernel op 0x8000.\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Upload Circle Bare-Metal kernel over UART.")
    parser.add_argument("-p", "--port", required=True, help="Seriële poort (bijv. /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baudrate (standaard: 115200)")
    parser.add_argument("kernel", help="Pad naar het kernel .img bestand")

    args = parser.parse_args()
    send_circle_kernel(args.port, args.baud, args.kernel)