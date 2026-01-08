import wave
import math
import struct
import os

def create_beep(filename, duration, freq, volume=0.5):
    sample_rate = 44100
    n_samples = int(sample_rate * duration)
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        
        data = []
        for i in range(n_samples):
            t = float(i) / sample_rate
            # Simple Sine Wave
            val = math.sin(2.0 * math.pi * freq * t)
            # Apply envelope (fade in/out) to avoid clicks
            if i < 500: val *= (i/500.0)
            if i > n_samples - 500: val *= ((n_samples - i)/500.0)
            
            sample = int(val * volume * 32767.0)
            data.append(struct.pack('<h', sample))
            
        wav_file.writeframes(b''.join(data))

if not os.path.exists("assets"):
    os.makedirs("assets")

# Click: Short, higher pitch
create_beep("assets/click.wav", 0.1, 880.0)

# Hover: Very short, lower pitch, quiet
create_beep("assets/hover.wav", 0.05, 440.0, 0.3)

print("Created assets/click.wav and assets/hover.wav")
