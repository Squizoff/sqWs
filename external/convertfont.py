def parse_bdf(bdf_path):
    font = {}
    with open(bdf_path) as f:
        lines = f
        while True:
            try:
                line = next(lines)
            except StopIteration:
                break
            if line.startswith("STARTCHAR"):
                encoding = 0; bitmap = []; bbx_w=bbx_h=0
                for line in lines:
                    if line.startswith("ENCODING"):
                        encoding = int(line.split()[1])
                    elif line.startswith("BBX"):
                        _, w, h, *_ = line.split()
                        bbx_w, bbx_h = int(w), int(h)
                    elif line.strip()=="BITMAP":
                        for _ in range(bbx_h):
                            bitmap.append(int(next(lines).strip(),16))
                    elif line.startswith("ENDCHAR"):
                        break
                if 0 <= encoding < 128:
                    arr = [0]*16
                    for i in range(min(16, len(bitmap))):
                        b = bitmap[i]
                        if bbx_w > 8:
                            b >>= (bbx_w - 8)
                        arr[i] = b & 0xFF
                    font[encoding] = arr
    return font

def gen_c(font, name="font8x16_basic"):
    print(f"const uint8_t {name}[128][16] = {{")
    for code in range(128):
        if code in font:
            glyph = ','.join(f"0x{b:02X}" for b in font[code])
            print(f"  [{code}] = {{ {glyph} }},")
    print("};")

if __name__=="__main__":
    f = parse_bdf("ter-u16n.bdf")
    gen_c(f)
