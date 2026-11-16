#!/usr/bin/env python3
# client.pyw - SDL3 thin client with main callbacks
#
# Why SDL3 main callbacks: on Windows, the OS blocks the message loop while
# the user is dragging/resizing a window (WM_ENTERSIZEMOVE). A classic
# while-loop never gets a chance to run. SDL3's main callbacks invoke
# SDL_AppEvent and SDL_AppIterate even inside that modal loop, so we can
# react to resize and render the video stream live while the user drags.

import os
os.environ["SDL_MAIN_USE_CALLBACKS"] = "1"

import ctypes
import dataclasses
import queue
import socket
import struct
import sys
import threading
import time

import numpy as np
import av
import sdl3


SERVER_IP = "127.0.0.1"
VIDEO_PORT = 12345
CONTROL_PORT = 12346

INITIAL_W = 1024
INITIAL_H = 768

EV_MOUSE_DOWN = 0
EV_MOUSE_MOVE = 1
EV_MOUSE_UP   = 2
EV_KEY_DOWN   = 3
EV_KEY_UP     = 4
EV_RESIZE     = 5

# Read by control_sender after (re)connect; written by the main thread
# on resize. Tuple assignment is atomic under CPython, so the
# control thread either sees the old or the new size, never a torn one.
current_window_size = (INITIAL_W, INITIAL_H)

latest_frame = None
running = True
control_socket = None
control_connected = False

event_queue = queue.Queue(maxsize=200)

KEY_MAP = {
    sdl3.SDLK_LEFT:      0xFF51,
    sdl3.SDLK_UP:        0xFF52,
    sdl3.SDLK_RIGHT:     0xFF53,
    sdl3.SDLK_DOWN:      0xFF54,
    sdl3.SDLK_LSHIFT:    0xFFE1,
    sdl3.SDLK_RSHIFT:    0xFFE2,
    sdl3.SDLK_LCTRL:     0xFFE3,
    sdl3.SDLK_RCTRL:     0xFFE4,
    sdl3.SDLK_LALT:      0xFFE9,
    sdl3.SDLK_RALT:      0xFFEA,
    sdl3.SDLK_TAB:       0xFF09,
    sdl3.SDLK_RETURN:    0xFF0D,
    sdl3.SDLK_ESCAPE:    0xFF1B,
    sdl3.SDLK_BACKSPACE: 0xFF08,
}


def key_to_scancode(key):
    if key in KEY_MAP:
        return KEY_MAP[key]
    if 0 <= key <= 0xFFFF:
        return key
    return None


def pack_event(ev, a, b):
    if ev in (EV_MOUSE_DOWN, EV_MOUSE_MOVE, EV_MOUSE_UP):
        return struct.pack('>Bii', ev, int(a), int(b))
    if ev == EV_RESIZE:
        return struct.pack('>BHH', ev, int(a) & 0xFFFF, int(b) & 0xFFFF)
    return struct.pack('>BH', ev, int(a) & 0xFFFF)


def video_receiver():
    global latest_frame, running

    while running:
        video_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        stream = None
        try:
            video_socket.connect((SERVER_IP, VIDEO_PORT))
            print("Video: Connected to server.")
            codec = av.CodecContext.create("h264", "r")
            stream = video_socket.makefile('rb')

            while running:
                header = stream.read(4)
                if len(header) < 4:
                    break
                size = struct.unpack('>I', header)[0]
                packet_data = stream.read(size)
                if len(packet_data) < size:
                    break

                packets = codec.parse(packet_data)
                for packet in packets:
                    try:
                        for frame in codec.decode(packet):
                            # ascontiguousarray -- FFmpeg aligns lines to
                            # 32 bytes, PyAV's to_ndarray returns a strided
                            # view; pygame/SDL upload needs C-contiguous
                            latest_frame = np.ascontiguousarray(
                                frame.to_ndarray(format='bgr24'))
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
    global running, control_connected, control_socket

    while running:
        if not control_connected:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect((SERVER_IP, CONTROL_PORT))
                control_socket = sock
                control_connected = True
                w, h = current_window_size
                sock.sendall(pack_event(EV_RESIZE, w, h))
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

        try:
            ev, a, b = event_queue.get(timeout=0.1)
            control_socket.sendall(pack_event(ev, a, b))
        except queue.Empty:
            pass
        except (ConnectionResetError, BrokenPipeError, OSError):
            print("Control: Disconnected.")
            control_connected = False
            if control_socket:
                control_socket.close()
                control_socket = None


@dataclasses.dataclass
class AppState:
    window: ctypes.POINTER(sdl3.SDL_Window)  # type: ignore[valid-type]
    renderer: ctypes.POINTER(sdl3.SDL_Renderer)  # type: ignore[valid-type]
    texture: ctypes.POINTER(sdl3.SDL_Texture) = None  # type: ignore[valid-type]
    tex_w: int = 0
    tex_h: int = 0
    mouse_down: bool = False
    pressed_keys: set = dataclasses.field(default_factory=set)


app_state: AppState = None


@sdl3.SDL_AppInit_func
def SDL_AppInit(c_appstate, argc, argv):
    global app_state

    if not sdl3.SDL_Init(sdl3.SDL_INIT_VIDEO):
        print("SDL_Init failed:", sdl3.SDL_GetError().decode())
        return sdl3.SDL_APP_FAILURE

    p_window   = ctypes.POINTER(sdl3.SDL_Window)()
    p_renderer = ctypes.POINTER(sdl3.SDL_Renderer)()

    if not sdl3.SDL_CreateWindowAndRenderer(
            b"CAD Stream", INITIAL_W, INITIAL_H,
            sdl3.SDL_WINDOW_RESIZABLE,
            ctypes.pointer(p_window), ctypes.pointer(p_renderer)):
        print("SDL_CreateWindowAndRenderer failed:",
              sdl3.SDL_GetError().decode())
        return sdl3.SDL_APP_FAILURE

    texture = sdl3.SDL_CreateTexture(
        p_renderer,
        sdl3.SDL_PIXELFORMAT_BGR24,
        sdl3.SDL_TEXTUREACCESS_STREAMING,
        1, 1)

    state = AppState(
        window=p_window,
        renderer=p_renderer,
        texture=texture,
        tex_w=1,
        tex_h=1,
    )
    app_state = state
    c_appstate[0] = ctypes.cast(
        ctypes.pointer(ctypes.py_object(state)), ctypes.c_void_p)

    threading.Thread(target=video_receiver, daemon=True).start()
    threading.Thread(target=control_sender, daemon=True).start()

    return sdl3.SDL_APP_CONTINUE


@sdl3.SDL_AppEvent_func
def SDL_AppEvent(c_appstate, c_event):
    global running, current_window_size

    event = c_event.contents
    st = app_state

    if event.type == sdl3.SDL_EVENT_QUIT:
        running = False
        return sdl3.SDL_APP_SUCCESS

    elif event.type == sdl3.SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        # Defensive: if SDL doesn't fire SDL_EVENT_QUIT for some close
        # path, this catches it and stops the loop anyway
        running = False
        return sdl3.SDL_APP_SUCCESS

    elif event.type == sdl3.SDL_EVENT_WINDOW_RESIZED:
        # H.264 4:2:0 requires even dimensions
        w = event.window.data1 & ~1
        h = event.window.data2 & ~1
        if (w, h) != current_window_size:
            current_window_size = (w, h)
            try:
                event_queue.put_nowait((EV_RESIZE, w, h))
            except queue.Full:
                pass

    elif event.type == sdl3.SDL_EVENT_KEY_DOWN:
        key = event.key.key
        if key == sdl3.SDLK_ESCAPE:
            running = False
            return sdl3.SDL_APP_SUCCESS
        sc = key_to_scancode(key)
        if sc is not None and sc not in st.pressed_keys:
            st.pressed_keys.add(sc)
            try:
                event_queue.put_nowait((EV_KEY_DOWN, sc, 0))
            except queue.Full:
                pass

    elif event.type == sdl3.SDL_EVENT_KEY_UP:
        key = event.key.key
        sc = key_to_scancode(key)
        if sc is not None:
            st.pressed_keys.discard(sc)
            try:
                event_queue.put_nowait((EV_KEY_UP, sc, 0))
            except queue.Full:
                pass

    elif event.type == sdl3.SDL_EVENT_MOUSE_BUTTON_DOWN:
        if event.button.button == sdl3.SDL_BUTTON_LEFT:
            st.mouse_down = True
            try:
                event_queue.put_nowait((EV_MOUSE_DOWN,
                                        int(event.button.x),
                                        int(event.button.y)))
            except queue.Full:
                pass

    elif event.type == sdl3.SDL_EVENT_MOUSE_BUTTON_UP:
        if event.button.button == sdl3.SDL_BUTTON_LEFT:
            st.mouse_down = False
            try:
                event_queue.put_nowait((EV_MOUSE_UP,
                                        int(event.button.x),
                                        int(event.button.y)))
            except queue.Full:
                pass

    elif event.type == sdl3.SDL_EVENT_MOUSE_MOTION:
        if st.mouse_down:
            try:
                event_queue.put_nowait((EV_MOUSE_MOVE,
                                        int(event.motion.x),
                                        int(event.motion.y)))
            except queue.Full:
                pass

    return sdl3.SDL_APP_CONTINUE


@sdl3.SDL_AppIterate_func
def SDL_AppIterate(c_appstate):
    global latest_frame

    st = app_state
    renderer = st.renderer

    frame = latest_frame
    if frame is not None:
        h, w = frame.shape[:2]
        if w != st.tex_w or h != st.tex_h:
            sdl3.SDL_DestroyTexture(st.texture)
            st.texture = sdl3.SDL_CreateTexture(
                renderer,
                sdl3.SDL_PIXELFORMAT_BGR24,
                sdl3.SDL_TEXTUREACCESS_STREAMING,
                w, h)
            st.tex_w, st.tex_h = w, h

        pixels_ptr = frame.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        sdl3.SDL_UpdateTexture(st.texture, None, pixels_ptr, w * 3)

    sdl3.SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255)
    sdl3.SDL_RenderClear(renderer)

    if frame is not None:
        sdl3.SDL_RenderTexture(renderer, st.texture, None, None)

    sdl3.SDL_RenderPresent(renderer)
    return sdl3.SDL_APP_CONTINUE


@sdl3.SDL_AppQuit_func
def SDL_AppQuit(c_appstate, result):
    global running, control_socket

    running = False
    time.sleep(0.05)

    if control_socket:
        try:
            for sc in list(app_state.pressed_keys):
                control_socket.sendall(pack_event(EV_KEY_UP, sc, 0))
        except Exception:
            pass
        try:
            control_socket.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        control_socket.close()

    sdl3.SDL_DestroyTexture(app_state.texture)
    sdl3.SDL_DestroyRenderer(app_state.renderer)
    sdl3.SDL_DestroyWindow(app_state.window)
    sdl3.SDL_Quit()


def main():
    argv = [a.encode() for a in sys.argv]
    c_args = (ctypes.c_char_p * len(argv))(*argv)

    result = sdl3.SDL_EnterAppMainCallbacks(
        ctypes.c_int(len(argv)),
        c_args,
        SDL_AppInit,
        SDL_AppIterate,
        SDL_AppEvent,
        SDL_AppQuit)

    return result

if __name__ == "__main__":
    try:
        rc = main()
    except BaseException:
        import traceback
        traceback.print_exc()
        rc = 1
    os._exit(rc)
