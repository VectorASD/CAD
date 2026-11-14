import socket
import struct
import threading
import time
import queue

import av
import cv2
import numpy as np
import pywinctl
from pynput import keyboard
from pynput.keyboard import Key


SERVER_IP = "127.0.0.1"
VIDEO_PORT = 12345
CONTROL_PORT = 12346

latest_frame = None
running = True
control_socket = None
control_connected = False

EV_MOUSE_DOWN = 0
EV_MOUSE_MOVE = 1
EV_MOUSE_UP   = 2
EV_KEY_DOWN   = 3
EV_KEY_UP     = 4

# Thread-safe queue for input events (mouse + keyboard)
event_queue = queue.Queue(maxsize=200)


WINDOW_NAME = "CAD Stream"
def is_cv2_window_focused() -> bool:
    try:
        active_title = pywinctl.getActiveWindowTitle()
        return active_title == WINDOW_NAME
    except Exception as e:
        return False

keys = {}

KEY_MAP = {
    Key.left:      0xFF51,
    Key.up:        0xFF52,
    Key.right:     0xFF53,
    Key.down:      0xFF54,
    Key.shift:     0xFFE1,
    Key.shift_r:   0xFFE2,
    Key.ctrl:      0xFFE3,
    Key.ctrl_r:    0xFFE4,
    Key.alt:       0xFFE9,
    Key.alt_r:     0xFFEA,
    Key.tab:       0xFF09,
    Key.enter:     0xFF0D,
    Key.esc:       0xFF1B,
    Key.backspace: 0xFF08,
}

def handle_key(key) -> int|None:
    if not is_cv2_window_focused():
        return

    char = getattr(key, 'char', None)
    if char is not None:
        code = ord(char.lower())  # Shift+W ('W') -> 'w'
        return code if code <= 0xFFFF else None

    code = KEY_MAP.get(key)
    if code is not None:
        return code

    vk = getattr(key, 'vk', None)
    if vk is None:
        vk = getattr(getattr(key, 'value', None), 'vk', None)
    return int(vk) if isinstance(vk, int) else None

def on_press(key):
    scancode = handle_key(key)
    if scancode is not None:
        if not keys.get(scancode):
            keys[scancode] = True
            try:
                event_queue.put_nowait((EV_KEY_DOWN, scancode, 0))
            except queue.Full:
                pass

def on_release(key):
    scancode = handle_key(key)
    if scancode is not None:
        keys[scancode] = False
        try:
            event_queue.put_nowait((EV_KEY_UP, scancode, 0))
        except queue.Full:
            pass


def video_receiver():
    global latest_frame, running

    while running:
        video_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        stream = None
        try:
            video_socket.connect((SERVER_IP, VIDEO_PORT))
            print("Video: Connected to server.")
            codec = av.CodecContext.create("h264", 'r')
            stream = video_socket.makefile("rb")

            while running:
                header = stream.read(4)
                if len(header) < 4:
                    break
                size = struct.unpack(">I", header)[0]
                packet_data = stream.read(size)
                if len(packet_data) < size:
                    break

                packets = codec.parse(packet_data)
                for packet in packets:
                    try:
                        frames = codec.decode(packet)
                        for frame in frames:
                            latest_frame = frame.to_ndarray(format="bgr24")
                    except Exception:
                        pass

        except ConnectionRefusedError:
            print("Video: Server not available. Retrying in 1s...")
        except Exception as e:
            print(f"Video: Error - {e}")
        finally:
            if stream is not None:
                stream.close()
            video_socket.close()

        if running:
            latest_frame = None
            time.sleep(1.0)

def control_sender():
    global running, control_socket, control_connected

    while running:
        if not control_connected:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect((SERVER_IP, CONTROL_PORT))
                control_socket = sock
                control_connected = True
                print("Control: Connected to server.")
            except ConnectionRefusedError:
                if control_socket:
                    control_socket.close()
                control_socket = None
                time.sleep(1.0)
                continue
            except Exception as e:
                print(f"Control: Error - {e}")
                time.sleep(1.0)
                continue

        # If we are here, we are connected
        try:
            ev, a, b = event_queue.get(timeout=0.1)
            if ev in (EV_MOUSE_DOWN, EV_MOUSE_MOVE, EV_MOUSE_UP):
                control_socket.sendall(struct.pack('>Bii', ev, a, b))
            else:  # EV_KEY_DOWN, EV_KEY_UP
                control_socket.sendall(struct.pack('>BH', ev, a))
        except queue.Empty:
            pass
        except (ConnectionResetError, BrokenPipeError, OSError):
            print("Control: Disconnected.")
            control_connected = False
            if control_socket:
                control_socket.close()
                control_socket = None

# Mouse state

def on_mouse(event, x, y, flags, param):
    ev = None
    if event == cv2.EVENT_LBUTTONDOWN:
        ev = EV_MOUSE_DOWN
    elif event == cv2.EVENT_MOUSEMOVE:
        ev = EV_MOUSE_MOVE
    elif event == cv2.EVENT_LBUTTONUP:
        ev = EV_MOUSE_UP
    if ev is None:
        return
    try:
        event_queue.put_nowait((ev, int(x), int(y)))
    except queue.Full:
        pass

def main():
    global latest_frame, running

    # Start background threads
    t_video = threading.Thread(target=video_receiver, daemon=True)
    t_video.start()

    t_control = threading.Thread(target=control_sender, daemon=True)
    t_control.start()

    cv2.namedWindow(WINDOW_NAME)
    cv2.setMouseCallback(WINDOW_NAME, on_mouse)

    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.start()

    try:
        while running:
            # 1. Check if user clicked the 'X' button.
            try:
                if cv2.getWindowProperty("CAD Stream", cv2.WND_PROP_VISIBLE) < 1:
                    break
            except cv2.error:
                break

            # 2. Draw the frame
            if latest_frame is not None:
                cv2.imshow(WINDOW_NAME, latest_frame)
            else:
                placeholder = np.zeros((600, 800, 3), dtype=np.uint8)
                cv2.putText(placeholder, "Waiting for server...", (230, 300),
                            cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255, 255, 255), 2)
                cv2.imshow("CAD Stream", placeholder)

            # 3. Process keyboard events
            key = cv2.waitKey(10) & 0xFF
            if key == ord('q'):
                break
    finally:
        listener.stop()

        # Cleanup
        running = False
        time.sleep(0.1)  # Give threads a moment to exit their loops

        if control_socket:
            control_socket.close()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
