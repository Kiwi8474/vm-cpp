import sys
import struct

MAGIC_NUMBER = 0x4D415849
HEADER_SIZE = 12 # MAGIC (4) + LOAD_ADDR (4) + SIZE (4)

# Basis-Instruktionen (16 Bit)
instruction_set = {
    "mov": (0x1, 2),  
    "ldi": (0x2, 2),  # 6 Bytes lang (2-Byte Header + 4-Byte Wert)
    "add": (0x3, 3),
    "sub": (0x4, 3),
    "mult": (0x5, 3),
    "div": (0x6, 3)
}

# Erweiterte Instruktionen (16 Bit, Opcode 0x0). Korrigierte Opcodes.
extended_instruction_set = {
    "hlt": (0x1, 0),
    "jmp": (0x2, 1), "jeq": (0x3, 1), "jne": (0x4, 1), "jc": (0x5, 1), 
    "jnc": (0x6, 1), "js": (0x7, 1), "jns": (0x8, 1), "jo": (0x9, 1), 
    "jno": (0xA, 1), 
    "jlt": (0xB, 1), 
    "jgt": (0xC, 1), 
    "ldr": (0xD, 2), 
    "str": (0xE, 2)  
}

def register_to_int(reg_token):
    if reg_token.lower().startswith("r"):
        try:
            reg_index = int(reg_token[1:])
            if 0 <= reg_index <= 15:
                return reg_index
            else:
                raise ValueError(f"Registerindex muss zwischen R0 und R15 liegen.")
        except ValueError:
            raise ValueError(f"Ungültiges Registerformat.")
    raise ValueError(f"Ungültiger Operand, erwarte Register (R0-R15).")

def parse_numeric_arg(token):
    # Unterstützt 0x... für Hex und Dezimalzahlen
    try:
        if token.lower().startswith("0x"):
            return int(token, 16)
        return int(token)
    except ValueError:
        raise ValueError(f"Ungültige numerische Adresse/Konstante: {token}")

def parse_file(asm_lines):
    """Reinigt die Zeilen und splittet sie in Tokens und Labels."""
    cleaned_lines = []
    for line_num, line in enumerate(asm_lines):
        # Entferne Kommentare
        if ';' in line:
            line = line.split(';')[0]
        
        cleaned_line = line.strip().replace(",", " ")
        if not cleaned_line:
            continue
            
        # Prüfe auf Label (endet mit :)
        if cleaned_line.endswith(':'):
            label = cleaned_line[:-1].strip()
            # Labels werden als [Label, None] gespeichert
            cleaned_lines.append((line_num + 1, label, None))
        else:
            tokens = cleaned_line.split()
            if tokens:
                instruction_str = tokens[0].lower()
                # Instruktionen werden als [None, Tokens] gespeichert
                cleaned_lines.append((line_num + 1, None, tokens))
    return cleaned_lines


def pass_1_find_labels(cleaned_lines):
    """Erster Durchgang: Findet alle Labels und ihre Adressen."""
    labels = {}
    current_address = 0x00000000
    load_address = None

    for line_num, label, tokens in cleaned_lines:
        # Pseudo-Instruktion .ORG oder LOAD setzen die Startadresse
        if tokens and tokens[0].lower() in [".org", "load"]:
            if len(tokens) != 2:
                print(f"Fehler in Zeile {line_num}: '{tokens[0]}' erwartet genau eine Adresse.")
                return None, None
            try:
                addr = parse_numeric_arg(tokens[1])
                current_address = addr
                load_address = addr
            except ValueError as e:
                print(f"Fehler in Zeile {line_num}: Ungültige Adresse: {e}")
                return None, None
            continue
        
        # Label gefunden: Speichere seine aktuelle Adresse
        elif label:
            if label in labels:
                print(f"Fehler in Zeile {line_num}: Label '{label}' bereits definiert.")
                return None, None
            labels[label] = current_address
            continue

        # Instruktionslänge zur Adresse addieren
        elif tokens:
            instr = tokens[0].lower()
            
            if instr in instruction_set:
                opcode_val, required_args = instruction_set[instr]
                
                # LDI ist 6 Bytes (2B Header + 4B Wert)
                if instr == "ldi":
                    current_address += 6
                # MOV, ADD, SUB, MULT, DIV sind 2 Bytes
                else:
                    current_address += 2
                    
            elif instr in extended_instruction_set:
                # Alle erweiterten Instruktionen sind 2 Bytes
                current_address += 2
            
            else:
                print(f"Fehler in Zeile {line_num}: Unbekannte Instruktion '{instr}'.")
                return None, None

    if load_address is None:
        print("Fehler: Keine Ladeadresse mit 'LOAD' oder '.ORG' gefunden.")
        return None, None
        
    return labels, load_address


def pass_2_generate_code(cleaned_lines, labels, load_address):
    """Zweiter Durchgang: Generiert den Maschinencode."""
    program_bytes = bytearray()
    
    for line_num, label, tokens in cleaned_lines:
        if not tokens:
            continue # Ist ein Label oder eine leere Zeile

        instruction_str = tokens[0].lower()

        # Pseudo-Instruktionen wie LOAD oder .ORG überspringen
        if instruction_str in [".org", "load"]:
            continue 

        # ----------------------
        # Erweiterte Instruktionen (Opcode 0x0)
        # ----------------------
        elif instruction_str in extended_instruction_set:
            ext_opcode_val, required_args = extended_instruction_set[instruction_str]
            
            r_dest_addr, r_src1_val = 0, 0
            
            # Sprungbefehle (JMP, JEQ, etc.) verwenden R_dest/Addr (tokens[1])
            if required_args == 1:
                try:
                    # Der Operand muss ein Register sein, das die Sprungadresse enthält
                    r_dest_addr = register_to_int(tokens[1]) 
                except ValueError as e:
                    print(f"Fehler in Zeile {line_num} (Argument 1, Sprung): Erwarte Register (R0-R15). {e}")
                    return None
            
            # LDR/STR verwenden R_dest/Addr (tokens[1]) und R_src/Val (tokens[2])
            elif required_args == 2:
                try:
                    r_dest_addr = register_to_int(tokens[1]) 
                    r_src1_val = register_to_int(tokens[2]) 
                except ValueError as e:
                    print(f"Fehler in Zeile {line_num} (LDR/STR): Erwarte Register (R0-R15). {e}")
                    return None

            binary_line = (0x0 << 12) | (ext_opcode_val << 8) | (r_dest_addr << 4) | r_src1_val
            program_bytes.extend(binary_line.to_bytes(2, 'big'))

        # ----------------------
        # Basis-Instruktionen
        # ----------------------
        elif instruction_str in instruction_set:
            opcode_val, required_args = instruction_set[instruction_str] 
            
            # MOV (Load 8-Bit Immediate)
            if instruction_str == "mov":
                try:
                    dest_reg = register_to_int(tokens[1])
                    imm8 = parse_numeric_arg(tokens[2])
                except ValueError as e:
                    print(f"Fehler in Zeile {line_num}: {e}")
                    return None
                
                if imm8 < 0 or imm8 > 255:
                    print(f"Fehler in Zeile {line_num}: MOV kann nur Immediate-Werte von 0 bis 255 (0xFF) laden. Nutze LDI.")
                    return None

                src1_imm = (imm8 >> 4) & 0xF
                src2_imm = imm8 & 0xF
                binary_line = (opcode_val << 12) | (dest_reg << 8) | (src1_imm << 4) | src2_imm
                program_bytes.extend(binary_line.to_bytes(2, 'big'))

            # LDI (Load 32-Bit Immediate) -> 6 Bytes
            elif instruction_str == "ldi":
                try:
                    dest_reg = register_to_int(tokens[1])
                    
                    # Hier ist der kritische Teil: Wert kann Zahl oder Label sein!
                    value_token = tokens[2]
                    imm32 = 0
                    
                    # Wenn der Wert ein Label ist (z.B. @KERNEL_LOOP)
                    if value_token.startswith('@'):
                        label_name = value_token[1:] # Entferne das @
                        if label_name in labels:
                            imm32 = labels[label_name]
                        else:
                            print(f"Fehler in Zeile {line_num}: Unbekanntes Label '{label_name}'")
                            return None
                    # Wenn der Wert eine Zahl ist (z.B. 0x00FF00)
                    else:
                        imm32 = parse_numeric_arg(value_token)
                        
                except ValueError as e:
                    print(f"Fehler in Zeile {line_num}: {e}")
                    return None

                if imm32 < 0 or imm32 > 0xFFFFFFFF:
                    print(f"Fehler in Zeile {line_num}: LDI-Wert liegt außerhalb des 32-Bit Bereichs.")
                    return None

                # 1. Instruktionswort (2 Bytes)
                instr_word = (opcode_val << 12) | (dest_reg << 8)
                program_bytes.extend(instr_word.to_bytes(2, 'big'))
                
                # 2. Immediate Value (4 Bytes)
                program_bytes.extend(imm32.to_bytes(4, 'big'))

            # ADD, SUB, MULT, DIV (3 Register) -> 2 Bytes
            else: 
                try:
                    dest_reg = register_to_int(tokens[1])
                    src1_reg = register_to_int(tokens[2])
                    src2_reg = register_to_int(tokens[3])
                except ValueError as e:
                    print(f"Fehler in Zeile {line_num}: {e}")
                    return None

                binary_line = (opcode_val << 12) | (dest_reg << 8) | (src1_reg << 4) | src2_reg
                program_bytes.extend(binary_line.to_bytes(2, 'big'))

        else:
            print(f"Fehler in Zeile {line_num}: Unbekannte Instruktion '{instruction_str}'.")
            return None

    return program_bytes


def assemble(asm_lines):
    cleaned_lines = parse_file(asm_lines)
    if not cleaned_lines:
        return None, None

    # PASS 1: Labels finden und Ladeadresse bestimmen
    labels, load_address = pass_1_find_labels(cleaned_lines)
    if labels is None:
        return None, None

    # PASS 2: Code generieren und Labels auflösen
    program_bytes = pass_2_generate_code(cleaned_lines, labels, load_address)
    
    return program_bytes, load_address

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Fehler: Nicht genügend Argumente erhalten.")
        print("Verwendung: python3 assembler.py <input.asm> <output.bin>")
        sys.exit(1)
    
    asm_file = sys.argv[1]
    binary_file = sys.argv[2]

    try:
        with open(asm_file, "r") as f:
            asm_content = f.readlines()
    except FileNotFoundError:
        print(f"Fehler: Eingabedatei '{asm_file}' nicht gefunden.")
        sys.exit(1)

    program_bytes, load_address = assemble(asm_content)
    
    if program_bytes is None:
        sys.exit(1)
        
    program_size_bytes = len(program_bytes)

    # ----------------------
    # Header erstellen (Immer Big-Endian)
    # ----------------------
    header_magic = MAGIC_NUMBER.to_bytes(4, 'big')
    header_load_addr = load_address.to_bytes(4, 'big')
    header_size = program_size_bytes.to_bytes(4, 'big')
    
    header_bytes = header_magic + header_load_addr + header_size

    final_binary_content = header_bytes + program_bytes

    try:
        with open(binary_file, "wb") as f:
            f.write(final_binary_content)
    except IOError:
        print(f"Fehler: Konnte die Ausgabedatei '{binary_file}' nicht schreiben.")
        sys.exit(1)


    print(f"Erfolgreich kompiliert!")
    print(f"Programmgröße: {program_size_bytes} Bytes")
    print(f"Ladeadresse: 0x{load_address:08X}")
