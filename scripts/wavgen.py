import argparse
import struct
import wave

import serial


def main(port, output, seconds):
    raw_sound = []

    with serial.Serial(port, 115200, timeout=1) as ser:
        ser.write(f'hwv mic capture {seconds}\r'.encode())
        ser.readline()

        finished = False
        while not finished:
            line = ser.readline().decode().strip()
            if line == "S":
                continue
            elif line == "E":
                finished = True
            else:
                raw_sound.append(int(line))

    with wave.open(output, "w") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)

        for value in raw_sound:
            data = struct.pack('<h', value)
            wav.writeframesraw(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--port", required=True, help="Serial port")
    parser.add_argument("-o", "--output", required=True, help="Output file")
    parser.add_argument("-s", "--seconds", default=1, help="Seconds")
    args = parser.parse_args()

    main(args.port, args.output, args.seconds)
