#include <iostream>
#include <vector>
#include <iomanip>
#include <fstream>
#include <string>
#include <ostream>

#define RAM_SIZE 65536 // in bytes definiert

#define HEADER_SIZE 12 // Magic (4 Bytes) + Load Addresse (4 Bytes) + Programmgröße ohne Header (4 Bytes)
#define MAGIC 0x4D415849

#define REGISTER_COUNT 16

#define CARRY_IDX 0
#define ZERO_IDX 1
#define SIGN_IDX 2
#define OVERFLOW_IDX 3

class ByteImage {
    private:
        std::vector<unsigned char> memory;

    public:
        ByteImage(int size_bytes) : memory(size_bytes, 0) {
        }

        size_t get_size() const {
            return memory.size();
        }

        unsigned char read(int address) {
            return memory[address];
        }

        void write(int address, int value) {
            memory[address] = (unsigned char)value;
        }

        unsigned int read_word(int address) {
            unsigned int byte_1 = memory[address];
            unsigned int byte_2 = memory[address+1];
            unsigned int byte_3 = memory[address+2];
            unsigned int byte_4 = memory[address+3];

            unsigned int result = (byte_1 << 24) |
                                  (byte_2 << 16) |
                                  (byte_3 << 8)  |
                                  (byte_4 << 0);

            return result;
        }

        void dump(std::ostream& os, int max_address) {
            // Begrenzt den Dump auf RAM_SIZE, falls max_address zu groß ist
            if (max_address >= (int)memory.size()) {
                max_address = (int)memory.size() - 1;
            }

            // Auf Hex umstellen, Hex in Großbuchstaben anzeigen und 0 als Füllzeichen verwenden
            os << std::hex << std::uppercase << std::setfill('0');
            os << "\n--- RAM DUMP (0x0000 bis 0x" << std::setw(4) << max_address << ") ---\n";

            for (int i = 0; i <= max_address; i += 16) { // 16 ist, wie viele Bytes in einer Zeile sind+
                // Zeigt die Addresse des ersten Bytes der Zeile an
                os << std::setw(4) << i << ": ";

                for (int j = 0; j < 16; j++) { // Hier genau so
                    int current_adress = i + j;

                    if (current_adress <= max_address) {
                        // Zeigt den Byte an
                        os << std::setw(2) << (int)read(current_adress) << " ";
                    } else {
                        os << "   ";
                    }
                }

                // Trennzeichen zur ASCII Spalte
                os << " | ";

                // Auf Dezimal umstellen
                os << std::dec;

                for (int j = 0; j < 16; j++) { // 16 ist wieder Bytes pro Zeile
                    int current_adress = i + j;

                    if (current_adress <= max_address) {
                        unsigned char byte = read(current_adress);

                        if (byte >= 32 && byte <= 126) { // nur druckbare ASCII Zeichen abbilden
                            os << byte; // byte ist unsigned char, also automatisch ein ASCII
                        } else {
                            os << "."; // für nicht-druckbares ein .
                        }
                    }
                }

                // Zeilenumbruch nach jeder Zeile
                os << std::endl;
                // Und auf Hex umstellen
                os << std::hex;
            }
            // Nach dem dump wieder auf dezimal umstellen
            os << std::dec << std::noshowbase << std::endl; 
        }
};

class Core {
    private:
        ByteImage& ram;

        unsigned int registers[REGISTER_COUNT];
        unsigned int pc;
        unsigned char flags[4];
        bool state = false;

        void _execute_extended(unsigned int dest_in, unsigned int src1_in, unsigned int src2_in) {
            unsigned int opcode = dest_in;
            unsigned int dest = src1_in;
            unsigned int src = src2_in;

            switch (opcode) {
                case 0x1:
                    halt();
                    break;
                case 0x2:
                    jump(dest);
                    break;
                case 0x3:
                    jeq(dest);
                    break;
                case 0x4:
                    jne(dest);
                    break;
                case 0x5:
                    jc(dest);
                    break;
                case 0x6:
                    jnc(dest);
                    break;
                case 0x7:
                    js(dest);
                    break;
                case 0x8:
                    jns(dest);
                    break;
                case 0x9:
                    jo(dest);
                    break;
                case 0xA:
                    jno(dest);
                    break;
                case 0xB:
                    jlt(dest);
                    break;
                case 0xC:
                    jgt(dest);
                    break;
                case 0xD:
                    ldr(dest, src);
                    break;
                case 0xE:
                    str(dest, src);
                    break;

                default:
                    if (opcode == 0) {} else {
                        std::cout << "Unbekannter Opcode " << opcode << " bei PC " << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << pc << std::dec << ". Fahre herunter." << std::endl;
                        state = false;
                        break;
                    }
            }
        }

        // Base
        void mov(unsigned int dest, unsigned int src1, unsigned int src2) {
            registers[dest] = (src1 << 4) | src2;
        }

        void ldi(unsigned int dest, unsigned int src1, unsigned int src2) {
            if (pc + 4 > ram.get_size()) {
                std::cout << "LDI: Nicht genügend Platz für 32-Bit Wert bei PC " << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << pc << std::dec << ". Fahre herunter." << std::endl;
                state = false;
                return;
            }

            unsigned int immediate_value = ram.read_word(pc); 
            registers[dest] = immediate_value;

            pc += 4; 
        }

        void add(unsigned int dest, unsigned int src1, unsigned int src2) {
            unsigned int val1 = registers[src1];
            unsigned int val2 = registers[src2];

            unsigned long long raw_result = (unsigned long long)val1 + (unsigned long long)val2;

            flags[CARRY_IDX] = (raw_result >> 32);
            flags[ZERO_IDX] = (registers[dest] == 0);
            flags[SIGN_IDX] = (registers[dest] >> 31) & 0x1;
            flags[OVERFLOW_IDX] = ((val1 ^ registers[dest]) & (val2 ^ registers[dest])) >> 31;

            registers[dest] = (unsigned int)raw_result; 
        }

        void sub(unsigned int dest, unsigned int src1, unsigned int src2) {
            unsigned int val1 = registers[src1];
            unsigned int val2 = registers[src2];

            unsigned long long raw_result = (unsigned long long)val1 - (unsigned long long)val2;

            flags[CARRY_IDX] = (val1 < val2);
            flags[ZERO_IDX] = (registers[dest] == 0);
            flags[SIGN_IDX] = (registers[dest] >> 31) & 0x1;
            flags[OVERFLOW_IDX] = ((val1 ^ registers[dest]) & (val2 ^ registers[dest])) >> 31;

            registers[dest] = (unsigned int)raw_result; 
        }

        void mult(unsigned int dest, unsigned int src1, unsigned int src2) {
            unsigned int val1 = registers[src1];
            unsigned int val2 = registers[src2];

            unsigned long long raw_result = (unsigned long long)val1 * (unsigned long long)val2;
            
            registers[dest] = (unsigned int)raw_result;

            flags[CARRY_IDX] = 0;
            flags[ZERO_IDX] = (registers[dest] == 0);
            flags[SIGN_IDX] = (registers[dest] >> 31) & 0x1;
            flags[OVERFLOW_IDX] = 0;
        }

        void div(unsigned int dest, unsigned int src1, unsigned int src2) {
            unsigned int val1 = registers[src1];
            unsigned int val2 = registers[src2];

            if (val2 == 0) {
                std::cout << "Division durch Null bei PC " << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << pc << std::dec << ". Fahre herunter." << std::endl;
                state = false;
                return;
            }

            unsigned long long raw_result = (unsigned long long)val1 / (unsigned long long)val2;

            flags[CARRY_IDX] = (raw_result >> 32);
            flags[ZERO_IDX] = (registers[dest] == 0);
            flags[SIGN_IDX] = (registers[dest] >> 31) & 0x1;
            flags[OVERFLOW_IDX] = flags[CARRY_IDX];

            registers[dest] = (unsigned int)raw_result; 
        }

        // Extended
        void halt() {
            state = false;
        }

        void jump(unsigned int dest) {
            pc = registers[dest];
        }

        void jeq(unsigned int dest) {
            if (flags[ZERO_IDX] == 1) {
                pc = registers[dest];
            }
        }

        void jne(unsigned int dest) {
            if (flags[ZERO_IDX] == 0) {
                pc = registers[dest];
            }
        }

        void jc(unsigned int dest) {
            if (flags[CARRY_IDX] == 1) {
                pc = registers[dest];
            }
        }

        void jnc(unsigned int dest) {
            if (flags[CARRY_IDX] == 0) {
                pc = registers[dest];
            }
        }

        void js(unsigned int dest) {
            if (flags[SIGN_IDX] == 1) {
                pc = registers[dest];
            }
        }

        void jns(unsigned int dest) {
            if (flags[SIGN_IDX] == 0) {
                pc = registers[dest];
            }
        }
        
        void jo(unsigned int dest) {
            if (flags[OVERFLOW_IDX] == 1) {
                pc = registers[dest];
            }
        }

        void jno(unsigned int dest) {
            if (flags[OVERFLOW_IDX] == 0) {
                pc = registers[dest];
            }
        }

        void ldr(unsigned int dest, unsigned int src) {
            unsigned char byte_high = ram.read(registers[src]);
            unsigned char byte_low = ram.read(registers[src] + 1);

            unsigned int value = (byte_high << 8) | byte_low;
            registers[dest] = value;
        }

        void str(unsigned int dest, unsigned int src) {
            unsigned int value_to_store = registers[src]; 

            unsigned char byte_high = (value_to_store >> 8) & 0xFF;
            unsigned char byte_low = value_to_store & 0xFF;

            ram.write(registers[dest], byte_high);
            ram.write(registers[dest] + 1, byte_low);
        }

        void jlt(unsigned int dest) {
            if (flags[SIGN_IDX] != flags[OVERFLOW_IDX]) {
                pc = registers[dest];
            }
        }

        void jgt(unsigned int dest) {
            if (flags[ZERO_IDX] == 0 && flags[SIGN_IDX] == flags[OVERFLOW_IDX]) {
                pc = registers[dest];
            }
        }

    public:
        Core(ByteImage& memory) : ram(memory), pc(0) {
            for (int i = 0; i < REGISTER_COUNT; ++i) {
                registers[i] = 0;
            }
            for (int i = 0; i < 4; ++i) {
                flags[i] = 0;
            }
        }

        bool get_state() {
            return state;
        }

        void power() {
            if (state == false) {
                state = true;
            } else {
                state = false;
            }
        }

        void execute() {
            if (pc + 1 >= ram.get_size()) {
                std::cout << "PC außerhalb vom RAM bei Addresse " << pc << ". Fahre herunter." << std::endl;
                state = false;
                return;
            }

            if (state == false) {
                return;
            }

            unsigned char byte_high = ram.read(pc);
            pc += 1;
            unsigned char byte_low = ram.read(pc);
            pc += 1;

            unsigned int instruction = (byte_high << 8) | byte_low;

            unsigned int opcode = (instruction >> 12) & 0xF;
            unsigned int dest = (instruction >> 8) & 0xF;
            unsigned int src1 = (instruction >> 4) & 0xF;
            unsigned int src2 = instruction & 0xF;

            switch (opcode) {
                case 0x0:
                    _execute_extended(dest, src1, src2);
                    break;
                case 0x1:
                    mov(dest, src1, src2);
                    break;
                case 0x2:
                    ldi(dest, src1, src2);
                    break;
                case 0x3:
                    add(dest, src1, src2);
                    break;
                case 0x4:
                    sub(dest, src1, src2);
                    break;
                case 0x5:
                    mult(dest, src1, src2);
                    break;
                case 0x6:
                    div(dest, src1, src2);
                    break;

                default:
                    std::cout << "Unbekannter Opcode " << opcode << " bei PC " << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << pc << std::dec << ". Fahre herunter." << std::endl;
                    state = false;
                    break;
            }
        }

        void dump() {
            std::cout << std::hex << std::uppercase << std::setfill('0');
            std::cout << "PC: 0x" << std::setw(8) << pc << std::endl;

            std::cout << "Flags: ";
            std::cout << "C=" << (int)flags[CARRY_IDX] << " (Carry) ";
            std::cout << "Z=" << (int)flags[ZERO_IDX] << " (Zero) ";
            std::cout << "S=" << (int)flags[SIGN_IDX] << " (Sign) ";
            std::cout << "O=" << (int)flags[OVERFLOW_IDX] << " (Overflow)" << std::endl;

            std::cout << "\nRegister:" << std::endl;
            for (int i = 0; i < REGISTER_COUNT; ++i) {
                std::cout << "R" << std::setw(2) << std::dec << i << std::hex << ": 0x"
                        << std::setw(8) << registers[i] << "  ";

                if ((i + 1) % 4 == 0) {
                    std::cout << std::endl;
                }
            }

            std::cout << std::dec << std::noshowbase << std::endl;
        }
};

class Clock {
    private:
        Core& core;

    public:
        Clock(Core& core) : core(core) {}

        void run(int max_steps = -1) { 
            int steps = 0;
            while (core.get_state()) {
                // Wenn ein Limit gesetzt ist und dieses erreicht ist, wird gestoppt
                if (max_steps != -1 && steps >= max_steps) {
                    std::cout << "\nACHTUNG: Maximales Schrittlimit (" << max_steps << ") erreicht. Fahre herunter.\n";
                    core.power(); // Setzt den Zustand auf false
                    break;
                }
                core.execute();
                steps++;
            }
            std::cout << "Core ist angehalten. Total: " << steps << " Schritte." << std::endl;
        }

};

bool load_binary_file(const std::string& filename, ByteImage& target_ram) {
    std::ifstream file(filename, std::ios::binary | std::ios::in);

    if (!file.is_open()) {
        std::cerr << "Konnte die Datei '" << filename << "' nicht öffnen." << std::endl;
        return false;
    }

    std::vector<char> header_buffer(HEADER_SIZE);
    if (!file.read(header_buffer.data(), HEADER_SIZE)) {
        std::cerr << "Konnte den Header der Datei nicht lesen." << std::endl;
        return false;
    }

    auto read_word_from_buffer = [](const std::vector<char>& buffer, int offset) -> unsigned int {
        return (unsigned int)(
            ((unsigned char)buffer[offset]     << 24) |
            ((unsigned char)buffer[offset + 1] << 16) |
            ((unsigned char)buffer[offset + 2] << 8)  |
            ((unsigned char)buffer[offset + 3] << 0)
        );
    };

    unsigned int magic = read_word_from_buffer(header_buffer, 0);
    unsigned int load_address = read_word_from_buffer(header_buffer, 4);
    unsigned int size = read_word_from_buffer(header_buffer, 8);


    if (magic != MAGIC) {
        std::cerr << "Magic Number im Programm stimmt nicht überein." << std::endl
                  << "Erwartet: " << std::hex << MAGIC << std::endl
                  << "Erhalten: " << std::hex << magic << std::endl;
        return false;
    }

    std::vector<unsigned char> program_buffer(size);
    if (!file.read((char*)program_buffer.data(), size)) {
        std::cerr << "Konnte das eigentliche Programm (" << size << " Bytes) nicht auslesen." << std::endl;
        return false;
    }

    for (unsigned int i = 0; i < size; ++i) {
        target_ram.write(load_address + i, program_buffer[i]);
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Verwendung: " << argv[0] << " <programm.bin>" << std::endl;
        return 1;
    }

    ByteImage ram(RAM_SIZE);
    Core core(ram);
    Clock clock(core);

    const std::string program_file = argv[1]; 

    if (!load_binary_file(program_file, ram)) {
        return 1;
    }
    
    core.power();
    clock.run(50000);

    std::ofstream dump_file("ram_dump.txt");

    if (dump_file.is_open()) {
        std::cout << "\n--- RAM DUMP wird in 'ram_dump.txt' geschrieben... ---\n";
        // RAM Dump wird in die Datei geschrieben
        ram.dump(dump_file, 0xFFFF); 
        dump_file.close();
        std::cout << "--- RAM DUMP fertig. Datei wurde geschlossen. ---\n";
    } else {
        std::cerr << "FEHLER: Konnte 'ram_dump.txt' nicht zum Schreiben oeffnen. RAM Dump wird in das Terminal ausgegeben.\n";
        // Fallback: Wenn Datei fehlschlaegt, in das Terminal schreiben (wird aber unuebersichtlich)
        ram.dump(std::cout, 0xFFFF);
    }
    core.dump();

    return 0;
}