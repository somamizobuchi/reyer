"""Message types that mirror reyer_rt::net::message definitions using msgspec."""

from __future__ import annotations

import msgspec
from typing import List
from enum import IntEnum


class Message(msgspec.Struct):
    """Base message class for all IPC messages."""
    pass


class Ping(Message):
    """Ping request message."""
    timestamp: int = 0


class Pong(Message):
    """Pong response message."""
    timestamp: int = 0

class TaskInfo(Message):
    name: str = ""
    configuration: str = ""


class Response(Message):
    """Generic response message."""
    success: bool
    error_code: int = 0
    error_message: str = ""
    payload: str = ""


class ResourceCode(IntEnum):
    """Resource type codes for ResourceRequest."""
    RUNTIME_STATE = 0
    AVAILABLE_MONITORS = 1
    AVAILABLE_PLUGINS = 2
    CURRENT_GRAPHICS_SETTINGS = 3
    CURRENT_PROTOCOL = 4
    CURRENT_TASK = 5

class RuntimeState(IntEnum):
    """Runtime state values."""
    DEFAULT = 0
    STANDBY = 1
    RUNNING = 2
    SAVING = 3

class Command(IntEnum):
    """Commands for the graphics manager"""
    START = 0
    STOP = 1
    NEXT = 2
    PREVIOUS = 3
    RESTART = 4
    EXIT = 5

class BroadcastTopic(IntEnum):
    """Broadcast message topic types."""
    LOG = 0
    PROTOCOL = 1

class ProtocolEvent(IntEnum):
    """Protocol lifecycle events."""
    GRAPHICS_READY = 0
    PROTOCOL_NEW = 1
    TASK_START = 2
    TASK_END = 3

class ProtocolEventMessage(Message):
    """Protocol event broadcast message."""
    protocol_uuid: str
    event: int  # ProtocolEvent
    data: int

class BroadcastMessage(Message):
    """Wrapper for broadcast messages with topic routing."""
    topic: int  # BroadcastTopic
    payload: str

class CommandRequest(Message):
    command: int
    origin: str
    destination: str

    

class ResourceRequest(Message):
    """Request for resource information (monitors, plugins, etc)."""
    resource_code: int


class PluginInfo(Message):
    """Plugin information message."""
    name: str
    configuration_schema: str


class MonitorInfo(Message):
    """Monitor information message."""
    index: int
    width_px: int
    height_px: int
    width_mm: int
    height_mm: int
    refresh_rate: int
    name: str

class GraphicsSettings(Message):
    monitor_index: int
    vsync: bool
    anti_aliasing: bool
    full_screen: bool
    target_fps: int
    width: int
    height: int

class GraphicsSettingsRequest(Message):
    """Request to initialize graphics settings (sent once at startup)."""
    graphics_settings: GraphicsSettings
    view_distance_mm: int

class ProtocolRequest(Message):
    """Request to load/manage a protocol."""
    name: str
    participant_id: str
    notes: str
    tasks: List[TaskInfo]
    protocol_uuid: str = ""
    

# Type alias for any message variant
MessageType = (
    Ping | Pong | GraphicsSettingsRequest | ProtocolRequest |
    Response | ResourceRequest | PluginInfo | MonitorInfo |
    CommandRequest
)


def serialize_message(msg: Message) -> bytes:
    """Serialize a message to JSON format."""
    return msgspec.json.encode(msg)


def deserialize_message(data: bytes, msg_type: type[Message]) -> Message:
    """Deserialize JSON data to a message object."""
    return msgspec.json.decode(data, type=msg_type)
