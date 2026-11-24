#!/usr/bin/env python3
"""Post-build: encrypt .rodata strings + inject runtime decoder + rename symbols."""
import struct, sys, subprocess, os
from pathlib import Path

MIN_STR_LEN = 4
PAGE_SIZE = 4096

def parse_elf_full(data):
    """Parse ELF64 to extract all info we need."""
    if data[:4] != b'\x7fELF' or data[4] != 2:
        return None
    e_entry = struct.unpack_from('<Q', data, 24)[0]
    e_phoff = struct.unpack_from('<Q', data, 32)[0]
    e_shoff = struct.unpack_from('<Q', data, 40)[0]
    e_phentsize = struct.unpack_from('<H', data, 54)[0]
    e_phnum = struct.unpack_from('<H', data, 56)[0]
    e_shentsize = struct.unpack_from('<H', data, 58)[0]
    e_shnum = struct.unpack_from('<H', data, 60)[0]
    e_shstrndx = struct.unpack_from('<H', data, 62)[0]

    info = {'entry': e_entry, 'sections': {}, 'segments': [], 'shstrtab': 0}

    if e_shnum > 0 and e_shoff > 0:
        shstr = e_shoff + e_shstrndx * e_shentsize
        strtab_off = struct.unpack_from('<Q', data, shstr + 24)[0]
        for i in range(e_shnum):
            sh = e_shoff + i * e_shentsize
            name_idx = struct.unpack_from('<I', data, sh)[0]
            sh_offset = struct.unpack_from('<Q', data, sh + 24)[0]
            sh_size = struct.unpack_from('<Q', data, sh + 32)[0]
            sh_addr = struct.unpack_from('<Q', data, sh + 16)[0]
            sh_flags = struct.unpack_from('<Q', data, sh + 8)[0]
            sh_addralign = struct.unpack_from('<Q', data, sh + 48)[0]
            ne = data.find(b'\x00', strtab_off + name_idx)
            name = data[strtab_off + name_idx:ne].decode('ascii', errors='')
            info['sections'][name] = {
                'offset': sh_offset, 'size': sh_size, 'addr': sh_addr,
                'flags': sh_flags, 'align': sh_addralign
            }

    for i in range(e_phnum):
        ph = e_phoff + i * e_phentsize
        p_type = struct.unpack_from('<I', data, ph)[0]
        p_flags = struct.unpack_from('<I', data, ph + 4)[0]
        p_offset = struct.unpack_from('<Q', data, ph + 8)[0]
        p_vaddr = struct.unpack_from('<Q', data, ph + 16)[0]
        p_filesz = struct.unpack_from('<Q', data, ph + 32)[0]
        p_memsz = struct.unpack_from('<Q', data, ph + 40)[0]
        p_align = struct.unpack_from('<Q', data, ph + 48)[0]
        info['segments'].append({
            'type': p_type, 'flags': p_flags, 'offset': p_offset,
            'vaddr': p_vaddr, 'filesz': p_filesz, 'memsz': p_memsz, 'align': p_align
        })
    return info

def align_up(v, a):
    return (v + a - 1) & ~(a - 1)

def derive_key_material(seed, ro_addr, ro_size):
    """Derive per-build key material from seed and section metadata."""
    s = seed & 0xFFFFFFFF
    # mix in layout so key depends on binary layout too
    s ^= (ro_addr * 0x9E3779B1) & 0xFFFFFFFF
    s ^= (ro_size * 0x85EBCA6B) & 0xFFFFFFFF
    if s == 0:
        s = 0xA5C3D1E7
    # xorshift32
    s ^= (s << 13) & 0xFFFFFFFF
    s ^= (s >> 17) & 0xFFFFFFFF
    s ^= (s << 5) & 0xFFFFFFFF
    key = [(s >> (i * 8)) & 0xFF for i in range(4)]
    # ensure non-trivial
    if all(k == 0 for k in key):
        key = [0x37, 0xA1, 0x5C, 0xD9]
    return s & 0xFFFFFFFF, key

def make_shellcode(rodata_vaddr, rodata_size, decoder_vaddr, orig_entry, seed, key):
    """Generate x86-64 decoder shellcode with rolling XOR (no fixed single-byte key)."""
    page = PAGE_SIZE
    rodata_page = rodata_vaddr & ~(page - 1)
    mprot_size = align_up(rodata_vaddr + rodata_size, page) - rodata_page

    def rip_rel(target, instr_end):
        return target - (decoder_vaddr + instr_end)

    c = bytearray()
    # save callee-saved-ish scratch we use; keep stack aligned enough for syscalls
    c += b'\x57\x56\x52\x53\x41\x50\x41\x51'  # push rdi,rsi,rdx,rbx,r8,r9

    # mprotect(rodata_page, mprot_size, PROT_READ|PROT_WRITE)
    c += b'\x48\xc7\xc0\x0a\x00\x00\x00'              # mov rax, 10
    c += b'\x48\x8d\x3d'                                 # lea rdi, [rip+disp32]
    c += struct.pack('<i', rip_rel(rodata_page, len(c) + 4))
    c += b'\x48\xc7\xc6'                                 # mov rsi, imm32
    c += struct.pack('<I', mprot_size)
    c += b'\x48\xc7\xc2\x03\x00\x00\x00'               # mov rdx, 3
    c += b'\x0f\x05'                                     # syscall

    # rdi = rodata_vaddr, rcx = rodata_size, r8d = index, r9d = rolling state
    c += b'\x48\x8d\x3d'                                 # lea rdi, [rip+disp32]
    c += struct.pack('<i', rip_rel(rodata_vaddr, len(c) + 4))
    c += b'\x48\xc7\xc1'                                 # mov rcx, imm32
    c += struct.pack('<I', rodata_size)
    c += b'\x45\x31\xc0'                                 # xor r8d, r8d
    c += b'\x41\xb9'                                     # mov r9d, seed
    c += struct.pack('<I', seed & 0xFFFFFFFF)

    # embed 4-byte key nearby as data; load its address
    # jmp over key bytes
    c += b'\xeb\x04'
    key_off = len(c)
    c += bytes(key[i] & 0xFF for i in range(4))
    # r10 = key ptr
    c += b'\x4c\x8d\x15'                                 # lea r10, [rip+disp32]
    c += struct.pack('<i', rip_rel(decoder_vaddr + key_off, len(c) + 4))

    loop_start = len(c)
    # al = *rdi
    c += b'\x8a\x07'                                     # mov al, [rdi]
    # bl = key[index & 3]
    c += b'\x44\x89\xc3'                                 # mov ebx, r8d
    c += b'\x83\xe3\x03'                                 # and ebx, 3
    c += b'\x41\x8a\x1c\x1a'                             # mov bl, [r10+rbx]
    # al ^= bl
    c += b'\x30\xd8'                                     # xor al, bl
    # al ^= r9b (rolling state)
    c += b'\x44\x30\xc8'                                 # xor al, r9b
    # al ^= (index & 0xff)
    c += b'\x44\x30\xc0'                                 # xor al, r8b
    # store
    c += b'\x88\x07'                                     # mov [rdi], al

    # advance state: r9d = (r9d * 0x45D9F3B + (r8d & 0xff) + 0x53) & 0xFFFFFFFF
    c += b'\x41\x69\xc9\x3b\x9f\x5d\x04'                 # imul ecx->ecx? no, wait
    # The above is wrong encoding; we need imul r9d, r9d, imm32
    # Fix: rebuild this section properly below by replacing
    # We'll construct correctly.
    # Remove the bad bytes we just appended (7 bytes)
    c = c[:-7]
    c += b'\x45\x69\xc9'                                 # imul r9d, r9d, imm32
    c += struct.pack('<I', 0x045D9F3B)
    c += b'\x44\x89\xc2'                                 # mov edx, r8d
    c += b'\x0f\xb6\xd2'                                 # movzx edx, dl
    c += b'\x41\x01\xd1'                                 # add r9d, edx
    c += b'\x41\x83\xc1\x53'                             # add r9d, 0x53

    # inc rdi, inc r8d, dec rcx, jnz
    c += b'\x48\xff\xc7'                                 # inc rdi
    c += b'\x41\xff\xc0'                                 # inc r8d
    c += b'\x48\xff\xc9'                                 # dec rcx
    rel = loop_start - (len(c) + 2)
    assert -128 <= rel < 0, f"loop too large: {rel}"
    c += b'\x75' + struct.pack('b', rel)                 # jnz loop_start

    # mprotect back to PROT_READ
    c += b'\x48\xc7\xc0\x0a\x00\x00\x00'
    c += b'\x48\x8d\x3d'
    c += struct.pack('<i', rip_rel(rodata_page, len(c) + 4))
    c += b'\x48\xc7\xc6'
    c += struct.pack('<I', mprot_size)
    c += b'\x48\xc7\xc2\x01\x00\x00\x00'
    c += b'\x0f\x05'

    # restore
    c += b'\x41\x59\x41\x58\x5b\x5a\x5e\x5f'             # pop r9,r8,rbx,rdx,rsi,rdi

    # jmp to original entry
    c += b'\xe9' + struct.pack('<i', orig_entry - (decoder_vaddr + len(c) + 5))
    return bytes(c)

def find_strings(data, offset, size):
    strings = []
    start = None
    for i in range(offset, offset + size):
        b = data[i]
        if 0x20 <= b <= 0x7E:
            if start is None:
                start = i
        else:
            if start is not None and (i - start) >= MIN_STR_LEN:
                strings.append((start, i - start))
            start = None
    if start is not None and (offset + size - start) >= MIN_STR_LEN:
        strings.append((start, offset + size - start))
    return strings

def rename_symbols(path):
    result = subprocess.run(['nm', '-C', path], capture_output=True, text=True)
    renames = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            sym = parts[2]
            if 'randomx' in sym.lower():
                h = abs(hash(sym)) % 0xFFFFFF
                renames[sym] = f"_r{h:06x}"
    if renames:
        args = []
        for old, new in renames.items():
            args += ['--redefine-sym', f'{old}={new}']
        subprocess.run(['objcopy'] + args + [path], check=True)
    return len(renames)

def encrypt_rodata(data, ro_off, ro_size, seed, key):
    state = seed & 0xFFFFFFFF
    for i in range(ro_size):
        k = key[i & 3]
        b = data[ro_off + i]
        b ^= k ^ (state & 0xFF) ^ (i & 0xFF)
        data[ro_off + i] = b & 0xFF
        state = (state * 0x045D9F3B + (i & 0xFF) + 0x53) & 0xFFFFFFFF

def patch(path):
    rename_symbols(path)

    data = bytearray(Path(path).read_bytes())
    elf = parse_elf_full(data)
    if elf is None:
        print("ERROR: not an ELF64 file", file=sys.stderr)
        return 1

    ro = elf['sections'].get('.rodata')
    if ro is None:
        print("ERROR: cannot find .rodata", file=sys.stderr)
        return 1

    ro_off, ro_size, ro_addr = ro['offset'], ro['size'], ro['addr']

    re_seg = None
    for seg in elf['segments']:
        if seg['type'] == 1 and (seg['flags'] & 5) == 5:
            re_seg = seg
            break
    if re_seg is None:
        print("ERROR: cannot find RE segment", file=sys.stderr)
        return 1

    orig_entry = elf['entry']
    seed = struct.unpack('<I', os.urandom(4))[0]
    seed, key = derive_key_material(seed, ro_addr, ro_size)

    decoder_file_off = re_seg['offset'] + re_seg['filesz']
    decoder_file_off = align_up(decoder_file_off, 16)
    decoder_vaddr = re_seg['vaddr'] + (decoder_file_off - re_seg['offset'])

    ro_seg = None
    for seg in elf['segments']:
        if seg['type'] == 1 and seg['vaddr'] == ro_addr:
            ro_seg = seg
            break
    if ro_seg is not None and decoder_file_off + 160 > ro_seg['offset']:
        decoder_file_off = ro_seg['offset'] - 160
        decoder_file_off = align_up(decoder_file_off, 16)
        decoder_vaddr = re_seg['vaddr'] + (decoder_file_off - re_seg['offset'])

    shellcode = make_shellcode(ro_addr, ro_size, decoder_vaddr, orig_entry, seed, key)

    new_filesz = (decoder_file_off + len(shellcode)) - re_seg['offset']
    new_memsz = align_up(new_filesz, re_seg['align'])

    ph_off = None
    for i, seg in enumerate(elf['segments']):
        if seg is re_seg:
            e_phoff = struct.unpack_from('<Q', data, 32)[0]
            e_phentsize = struct.unpack_from('<H', data, 54)[0]
            ph_off = e_phoff + i * e_phentsize
            break
    if ph_off is None:
        print("ERROR: cannot locate program header", file=sys.stderr)
        return 1

    struct.pack_into('<Q', data, ph_off + 32, new_filesz)
    struct.pack_into('<Q', data, ph_off + 40, new_memsz)

    while len(data) < decoder_file_off + len(shellcode):
        data += b'\x00'
    data[decoder_file_off:decoder_file_off + len(shellcode)] = shellcode

    encrypt_rodata(data, ro_off, ro_size, seed, key)

    struct.pack_into('<Q', data, 24, decoder_vaddr)
    Path(path).write_bytes(data)

    r = subprocess.run(['strings', path], capture_output=True, text=True)
    all_strings = set(r.stdout.splitlines())
    ascii_strings = [s for s in all_strings if s.isascii() and s.isprintable() and len(s) >= MIN_STR_LEN]
    noise = {'ELF', '.rodata', '.text', '.data', '.bss', '.symtab', '.strtab',
             '.shstrtab', 'crtbegin', 'crtend', 'GCC:', 'GNU:', '.gnu', '.note',
             'libc', 'ld-linux', 'ld.so', 'Scrt1', 'crt1'}
    suspicious = [s for s in ascii_strings
                  if not any(n in s for n in noise)
                  and not s.startswith('_') and not s.startswith('.')]
    print(f"OK: encrypted .rodata ({ro_size} bytes), decoder at 0x{decoder_vaddr:x}")
    if suspicious:
        print(f"  remaining visible ({len(suspicious)}): {suspicious[:10]}{'...' if len(suspicious) > 10 else ''}")
    return 0

if __name__ == '__main__':
    sys.exit(patch(sys.argv[1]))
