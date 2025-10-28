import subprocess
import os
import sys
import time

# --- Konfiguration ---
# Der Pfad zum kompilierten C++-Emulator
VM_EXECUTABLE = "./seraph_vm" 
# Der Pfad zum Assembler-Programm, das die VM ausführen soll
PROGRAM_TO_LOAD = "programm.bin" 
# Der Pfad zum Python-Renderer-Skript
RENDERER_SCRIPT = "renderer.py"
# ---------------------

def run_seraph_system():
    """
    Startet den C++ VM-Prozess und den Python Renderer-Prozess und verbindet sie.
    """
    vm_process = None
    renderer_process = None

    print(f"[LAUNCHER] Starte das Seraph-System...")

    try:
        # 1. Starte den C++ VM-Prozess (seraph_vm)
        # Wir verwenden Popen, um den Prozess im Hintergrund zu starten
        print(f"[VM] Starte VM mit Programm: {PROGRAM_TO_LOAD}")
        vm_process = subprocess.Popen([VM_EXECUTABLE, PROGRAM_TO_LOAD], 
                                      stdout=subprocess.PIPE, # Ausgabe der VM in eine Pipe
                                      stderr=subprocess.STDOUT, 
                                      text=True)

        # Gib der VM 0.5 Sekunden, um den RAM zu initialisieren und den ersten Dump zu machen
        time.sleep(0.5)

        # 2. Starte den Python Renderer-Prozess (Monitor)
        # Wir starten den Renderer mit 'python3' (Standard auf den meisten Linux-Systemen)
        print(f"[RENDERER] Starte den virtuellen Monitor ({RENDERER_SCRIPT})")
        renderer_process = subprocess.Popen([sys.executable, RENDERER_SCRIPT])

        # 3. Warte, bis der Renderer (das GUI-Fenster) geschlossen wird
        print("[LAUNCHER] System laeuft. Schliesse das Tkinter-Fenster, um die VM zu beenden.")
        
        # Blockiere, bis das Tkinter-Fenster geschlossen wird
        renderer_process.wait() 
        
        print("\n[LAUNCHER] Monitor geschlossen. Beende nun die VM...")

    except FileNotFoundError as e:
        print(f"\n[FEHLER] Konnte eine Datei nicht finden: {e.filename}")
        print("Stelle sicher, dass 'seraph_vm', 'renderer.py' und 'programm.bin' in diesem Verzeichnis sind und 'seraph_vm' ausführbar ist.")
    except Exception as e:
        print(f"\n[FEHLER] Ein unerwarteter Fehler ist aufgetreten: {e}")
        
    finally:
        # Saubere Beendigung aller Prozesse
        if renderer_process and renderer_process.poll() is None:
            renderer_process.terminate()
            print("[LAUNCHER] Renderer-Prozess beendet.")

        if vm_process and vm_process.poll() is None:
            # Sende SIGTERM an die VM
            vm_process.terminate()
            print("[LAUNCHER] VM-Prozess beendet.")
            
            # Warte kurz, um sicherzustellen, dass er wirklich tot ist
            try:
                vm_process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                # Wenn er immer noch läuft, schiesse ihn ab (SIGKILL)
                vm_process.kill()
                print("[LAUNCHER] VM-Prozess wurde zwangsweise beendet (kill).")

        # Gib die Konsolenausgabe der VM aus, falls vorhanden
        if vm_process and vm_process.stdout:
            vm_output = vm_process.stdout.read()
            if vm_output:
                print("\n--- VM KONSOLENAUSGABE ---\n")
                print(vm_output)
                print("\n--------------------------\n")
            
        print("[LAUNCHER] Seraph-System heruntergefahren. Tschüss Maxi! :wave:")


if __name__ == "__main__":
    # Erzeuge eine leere programm.bin, wenn sie nicht existiert, um Fehler zu vermeiden
    if not os.path.exists(PROGRAM_TO_LOAD):
        print(f"[ACHTUNG] Datei '{PROGRAM_TO_LOAD}' nicht gefunden. Erstelle eine leere Datei.")
        # Schreibe einen minimalen Header und einen HALT-Befehl (0x0001) in die Lade-Adresse 0x1000
        # MAGIC(4D415849) | LOAD_ADDR(00001000) | SIZE(00000002) | PROGRAM(0001)
        empty_program = bytes.fromhex('4D41584900001000000000020001')
        with open(PROGRAM_TO_LOAD, 'wb') as f:
            f.write(empty_program)
            
    run_seraph_system()
