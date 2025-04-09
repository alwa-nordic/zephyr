#!/usr/bin/env python3

import sys
import argparse
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

def e(key, plaintextData):
    if not 0 <= key <= (2**128 - 1):
        raise ValueError(f"Key out of range: {key}")

    if not 0 <= plaintextData <= (2**128 - 1):
        raise ValueError(f"Plaintext data out of range: {plaintextData}")

    K = key.to_bytes(16, 'big')
    M = plaintextData.to_bytes(16, 'big')

    cipher = Cipher(algorithms.AES128(K), modes.ECB())
    encryptor = cipher.encryptor()
    
    ciphertext = encryptor.update(M) + encryptor.finalize()

    return int.from_bytes(ciphertext, 'big')

def ah(k, r):
    if not 0 <= r <= 0xffffff:
        raise ValueError(f"r out of range: {r}")

    if not 0 <= k <= (2**128 - 1):
        raise ValueError(f"k out of range: {k}")

    return e(k, r) % (2**24)

# Check if the IRK matches the RPA
def bt_rpa_irk_matches(irk, addr):
    prand = parse_rpa_prand(addr)
    found_hash = parse_rpa_hash(addr)
    
    print(f"irk            : {hex(irk)}")
    print(f"prand          : {hex(prand)}")

    # Calculate the hash using the IRK and random part
    calculated_hash = ah(irk, prand)

    print(f"expected_hash  : {hex(calculated_hash)}")
    print(f"found_hash     : {hex(found_hash)}")
    
    # Compare the calculated hash with the hash part from the address
    return found_hash == calculated_hash

def parse_address(addr_str):
    # Remove colons and check length
    addr = addr_str.replace(':', '')
    if len(addr) != 12:
        raise ValueError(f"Invalid Bluetooth address format: {addr_str}")
    
    return int.from_bytes(bytes.fromhex(addr), 'big')

def parse_rpa_prand(rpa):
    if not 0 <= rpa <= (2**48 - 1):
        raise ValueError(f"rpa out of range: {rpa}")

    if (rpa // (2**46)) != 1:
        raise ValueError(f"rpa magic bits not set: {rpa}")

    prand = rpa // (2**24)

    return prand

def parse_rpa_hash(rpa):
    if not 0 <= rpa <= (2**48 - 1):
        raise ValueError(f"rpa out of range: {rpa}")

    if (rpa // (2**46)) != 1:
        raise ValueError(f"rpa magic bits not set: {rpa}")

    hash = rpa % (2**24)

    return hash

def parse_irk(irk_str):
    # Check if the input contains commas
    if ',' in irk_str:
        # Parse as comma-separated integers
        # Split by commas, strip whitespace, and convert to integers
        values = [int(x.strip(), base=0) for x in irk_str.split(',')]
        
        # Ensure we have 16 bytes
        if len(values) != 16:
            raise ValueError(f"Expected 16 comma-separated values, got {len(values)}")
            
        # Ensure all values are valid bytes (0-255)
        for val in values:
            if val < 0 or val > 255:
                raise ValueError(f"Invalid byte value: {val} (must be 0-255)")
        
        # Convert integers to bytes
        irk_bytes = bytes(values)
    else:
        # Handle as hex string (remove spaces and colons)
        irk = irk_str.replace(':', '').replace(' ', '')
        if len(irk) != 32:  # 16 bytes = 32 hex characters
            raise ValueError(f"Invalid IRK format: {irk_str}")
        
        # Convert hex string to bytes
        irk_bytes = bytes.fromhex(irk)
        
        # Ensure the IRK is exactly 16 bytes
        if len(irk_bytes) != 16:
            raise ValueError(f"Invalid IRK format: {irk_str}")

    return int.from_bytes(irk_bytes, 'little')

def main():
    parser = argparse.ArgumentParser(description='Bluetooth IRK address resolution')
    parser.add_argument('irk', help='Identity Resolving Key (IRK) in little-endian, either hex or comma separated byte literals.')
    parser.add_argument('rpa', help='Resolvable Private Address (RPA) in cannonical format xx:xx:xx:xx:xx:xx')
    
    args = parser.parse_args()
    
    irk = parse_irk(args.irk)
    addr = parse_address(args.rpa)
    
    match = bt_rpa_irk_matches(irk, addr)
    
    if match:
        print(f"Match!")
    else:
        print(f"No match.")

if __name__ == "__main__":
    main()
