# sqWs - Simple Window System

sqWs is a lightweight window manager and server implementation for Linux, designed to provide basic windowing functionality using the Direct Rendering Manager (DRM) for rendering and handling input events. It supports creating, managing, and rendering windows with simple graphics capabilities, including text and basic shapes, and handles user input via mouse and keyboard

## Features

- **Window Management**: Create, move, minimize, maximize, and destroy windows
- **Rendering**: Draw windows with title bars, borders, and buttons (close, minimize, maximize/restore) using a simple pixel-based rendering system
- **Input Handling**: Process mouse and keyboard events, including window dragging and button interactions
- **Client-Server Architecture**: Communicate between a server (window manager) and clients via UNIX sockets
- **Framebuffer Support**: Utilizes DRM for direct rendering to the framebuffer
- **Basic Graphics**: Supports drawing text (using an 8x16 font) and rectangles with alpha blending
- **Example Client**: Includes a sample client application demonstrating window creation and basic animation

## Requirements

- **Operating System**: Linux with DRM support
- **Dependencies**:
  - `libdrm` for framebuffer access
  - Standard C libraries
- **Compiler**: `clang`
- **Hardware**: A system with a supported DRM-capable graphics device

## Building

1. Ensure `libdrm` and `clang` are installed on your system:
   ```bash
   sudo apt-get install libdrm-dev clang
   ```
2. Clone the repository:
   ```bash
   git clone https://github.com/Squizoff/sqWs.git
   cd sqWs
   ```
3. Build the server and client binaries:
   ```bash
   make
   ```
   This creates:
   - `bin/sqws`: The window manager server
   - `bin/client`: The example client application

## Running

1. Start the server:
   ```bash
   ./bin/sqws
   ```
   The server initializes the DRM framebuffer, sets up input devices, and listens for client connections via a UNIX socket at `sqws/sock`

2. In a separate terminal, run the example client:
   ```bash
   ./bin/client
   ```
   The client creates a window with a red square that moves horizontally and responds to keyboard input to exit

## Known Issues

- **Maximize Freeze**: The window manager may hang indefinitely when a window is maximized. This is a known bug and is being investigated. Avoid using the maximize button until this issue is resolved.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details