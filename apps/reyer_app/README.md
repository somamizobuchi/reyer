# Reyer App - Frontend for reyer_rt

A PySide6-based frontend application that communicates with the reyer_rt graphics runtime via IPC sockets.

## Setup

### Prerequisites

- Python 3.13+
- reyer_rt server running (from the parent reyer project)

### Installation

```bash
# Install dependencies using uv
uv sync

# Or using pip
pip install -e .
```

## Running

### Start reyer_rt first

```bash
# From the reyer project root
./build/apps/reyer_rt/reyer_rt
```

### Then run reyer-app

```bash
python main.py
```

## Architecture

### IPC Communication Layer

The application communicates with reyer_rt using NNG (nanomsg) sockets with MessagePack serialization.

#### Socket Types

**Request-Reply (REQ/REP):**
- Client connects to `ipc:///tmp/reyer-rep.sock`
- Synchronous request/response pattern
- Used for sending configuration and protocol requests

**Publish-Subscribe (PUB/SUB):**
- Client subscribes to `ipc:///tmp/reyer-pub.sock`
- One-way broadcast messaging
- Used for receiving event updates from reyer_rt

### Message Types

All message types are defined in `src/messages.py` using msgspec:

- **`Ping`** / **`Pong`**: Connectivity testing
- **`ProtocolRequest`**: Load/manage experimental protocols
- **`Response`**: Generic response wrapper
- **`GraphicsConfiguration`**: Update graphics settings (resolution, FPS, vsync, etc.)
- **`Protocol`**: Protocol definition with tasks and metadata

Messages are automatically serialized to MessagePack format, matching the C++ reyer_rt serialization.

### Client Usage

#### Basic Connection

```python
from src.client import ReyerRTClient

client = ReyerRTClient()
if client.connect():
    print("Connected to reyer_rt")
    client.disconnect()
```

#### Using Context Manager

```python
from src.client import ReyerRTClient

with ReyerRTClient() as client:
    if client.is_connected():
        print("Connected")
```

#### Sending Requests

```python
from src.client import ReyerRTClient
from src.messages import GraphicsConfiguration

with ReyerRTClient() as client:
    # Configure graphics
    config = GraphicsConfiguration(
        width=1920,
        height=1080,
        frame_rate=60,
        vsync=True,
        anti_aliasing=True,
        full_screen=False
    )

    response = client.send_request(config)
    if response:
        print("Config updated successfully")
```

#### Subscribing to Messages

```python
from src.client import ReyerRTClient

def on_message(data: bytes):
    print(f"Received: {len(data)} bytes")

with ReyerRTClient() as client:
    client.subscribe(callback=on_message)
    # Listen for messages...
    import time
    time.sleep(10)
    client.unsubscribe(on_message)
```

## Configuration

To change the socket addresses or timeouts, pass a `ClientConfig`:

```python
from src.client import ReyerRTClient, ClientConfig

config = ClientConfig(
    req_socket_addr="ipc:///tmp/reyer-rep.sock",  # Request-reply socket
    pub_socket_addr="ipc:///tmp/reyer-pub.sock",  # Publish-subscribe socket
    request_timeout_ms=5000,
    receive_timeout_ms=5000
)

client = ReyerRTClient(config)
client.connect()
```

## Examples

See `src/example.py` for complete examples:

```bash
python -m src.example
```

## Project Structure

```
reyer_app/
├── main.py              # Application entry point
├── pyproject.toml       # Project configuration
├── README.md            # This file
└── src/
    ├── __init__.py
    ├── client.py        # ReyerRTClient IPC implementation
    ├── messages.py      # Message type definitions using msgspec
    └── example.py       # Usage examples
```

## Troubleshooting

### "Failed to connect to reyer_rt server"

1. Ensure reyer_rt is running:
   ```bash
   ./build/apps/reyer_rt/reyer_rt
   ```

2. Verify the socket address matches what reyer_rt is listening on:
   - Default: `ipc:///tmp/reyer-rep.sock`
   - Check reyer_rt logs for the actual address

3. Check file permissions on the socket file (if it exists)

### Connection Timeout

- Increase `request_timeout_ms` in `ClientConfig`
- Ensure reyer_rt is responsive (check server logs)

### Message Serialization Issues

- Verify message types in `src/messages.py` match C++ definitions
- Check that msgspec and pynng versions are compatible with the expected format
