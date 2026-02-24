"""PyNNG client for communicating with reyer_rt via IPC sockets."""

from __future__ import annotations
import msgspec

import pynng
from typing import Optional, Callable, List
from dataclasses import dataclass
import threading
import logging

from .messages import Message, Ping, Pong, Response, serialize_message, deserialize_message


logger = logging.getLogger(__name__)


@dataclass
class ClientConfig:
    """Configuration for the reyer_rt client."""
    req_socket_addr: str = "ipc:///tmp/reyer-rep.sock"  # ReplySocket address in reyer_rt
    pub_socket_addr: str = "ipc:///tmp/reyer-pub.sock"  # PublishSocket address in reyer_rt
    request_timeout_ms: int = 5000
    receive_timeout_ms: int = 5000


class ReyerClient:
    """
    Client for communicating with reyer_rt graphics application.

    Provides methods for:
    - Sending synchronous requests (REQ/REP pattern)
    - Publishing messages (PUB/SUB pattern)
    - Receiving published messages from subscriptions
    - Registering callbacks for connection/disconnection events
    """

    def __init__(self, config: Optional[ClientConfig] = None):
        """
        Initialize the reyer_rt client.

        Args:
            config: ClientConfig instance for customizing socket addresses and timeouts.
                   Defaults to standard IPC socket paths.
        """
        self.config = config or ClientConfig()
        self._request_socket: Optional[pynng.Req0] = None
        self._pub_socket: Optional[pynng.Pub0] = None
        self._sub_socket: Optional[pynng.Sub0] = None
        self._connected = False
        self._lock = threading.RLock()
        self._subscription_thread: Optional[threading.Thread] = None
        self._subscription_callbacks: dict[str, list[Callable]] = {}
        self._running = False

        # Pipe event callbacks
        self._on_connected_callback: Optional[Callable] = None
        self._on_disconnected_callback: Optional[Callable] = None


    def connect(self) -> None:
        """
        Connect to reyer_rt server asynchronously in a background thread.
        Connection status updates are emitted via callbacks.
        """
        # Run connection in background thread to avoid blocking the UI
        thread = threading.Thread(target=self._connect_async, daemon=True)
        thread.start()

    def _connect_async(self) -> None:
        """Async connection worker thread."""
        with self._lock:
            try:
                # Initialize request/reply socket
                self._request_socket = pynng.Req0()

                # Register pipe event callbacks
                self._request_socket.add_post_pipe_connect_cb(lambda pipe: self._handle_pipe_connect())
                self._request_socket.add_post_pipe_remove_cb(lambda pipe: self._handle_pipe_remove())

                self._request_socket.recv_timeout = self.config.receive_timeout_ms
                self._request_socket.dial(self.config.req_socket_addr)

                # Test the connection by sending a ping
                # This ensures the connection actually works, not just that the socket was created
                test_ping = Ping(timestamp=0)
                test_data = serialize_message(test_ping)

                try:
                    self._request_socket.send(test_data)
                    _ = self._request_socket.recv()
                    logger.info(f"Connected to reyer_rt server at {self.config.req_socket_addr}")
                except (pynng.Timeout, Exception) as e:
                    logger.error(f"Connection test failed (socket created but not responsive): {e}")
                    if self._request_socket:
                        self._request_socket.close()
                        self._request_socket = None
                    self._connected = False

            except Exception as e:
                logger.error(f"Failed to create/dial socket: {e}")
                if self._request_socket:
                    try:
                        self._request_socket.close()
                    except:
                        pass
                    self._request_socket = None
                self._connected = False

    def disconnect(self) -> None:
        """Disconnect from reyer_rt server."""
        self._running = False

        # Close sub socket first (outside lock) to unblock recv() in subscription loop
        if self._sub_socket:
            try:
                self._sub_socket.close()
            except Exception:
                pass

        # Wait for subscription thread to finish (outside lock to avoid deadlock)
        if self._subscription_thread and self._subscription_thread.is_alive():
            self._subscription_thread.join(timeout=2.0)

        with self._lock:
            self._sub_socket = None

            # Close remaining sockets
            if self._request_socket:
                self._request_socket.close()
                self._request_socket = None

            if self._pub_socket:
                self._pub_socket.close()
                self._pub_socket = None

            logger.info("Disconnected from reyer_rt server")

    def is_connected(self) -> bool:
        """Check if client is connected to server."""
        return self._connected

    def register_on_connected(self, callback: Callable) -> None:
        """
        Register a callback to be called when pipe connects.

        Args:
            callback: Function to call when pipe connects. Called with no arguments.
        """
        self._on_connected_callback = callback

    def register_on_disconnected(self, callback: Callable) -> None:
        """
        Register a callback to be called when pipe disconnects.

        Args:
            callback: Function to call when pipe disconnects. Called with no arguments.
        """
        self._on_disconnected_callback = callback

    def _handle_pipe_connect(self) -> None:
        """Internal handler for when a pipe connects."""
        logger.info("Pipe connected")
        self._connected = True
        if self._on_connected_callback:
            self._on_connected_callback()

    def _handle_pipe_remove(self) -> None:
        """Internal handler for when a pipe is removed/disconnected."""
        logger.info("Pipe disconnected")
        self._connected = False
        if self._on_disconnected_callback:
            self._on_disconnected_callback()

    def send_request(self, message: Message) -> Optional[bytes]:
        """
        Send a synchronous request to reyer_rt and wait for response.

        Args:
            message: Message object to send

        Returns:
            Response bytes from server, or None if request failed
        """
        if not self._connected:
            logger.error("Cannot send request: not connected to server")
            return None

        with self._lock:
            try:
                # Serialize and send message
                data = serialize_message(message)
                self._request_socket.send(data)

                # Wait for response
                response_data = self._request_socket.recv()
                logger.debug(f"Received response: {len(response_data)} bytes")
                return response_data

            except pynng.Timeout:
                logger.error("Request timeout while waiting for response")
                return None
            except Exception as e:
                logger.error(f"Error sending request: {e}")
                return None

    def send_ping(self, timestamp: int = 0) -> bool:
        """
        Send a ping message to verify server connectivity.

        Args:
            timestamp: Timestamp to include in ping (0 for current time)

        Returns:
            True if ping was successful and pong received
        """
        try:
            ping = Ping(timestamp=timestamp)
            response_data = self.send_request(ping)

            if response_data:
                pong = deserialize_message(response_data, Pong)
                logger.info(f"Received pong with timestamp {pong.timestamp}")
                return True
            return False
        except Exception as e:
            logger.error(f"Ping failed: {e}")
            return False

    def _get_plugins_by_type(self, resource_code: int, type_name: str) -> Optional[list]:
        """Internal helper to get plugins by resource type."""
        try:
            from .messages import ResourceRequest, Response, PluginInfo

            request = ResourceRequest(resource_code=resource_code)
            response_data = self.send_request(request)

            if response_data:
                response = deserialize_message(response_data, Response)
                if response.success and response.payload:
                    plugins = msgspec.json.decode(response.payload, type=List[PluginInfo])
                    logger.info(f"Received {len(plugins)} {type_name} from server")
                    return plugins
                else:
                    logger.error(f"Server returned error: {response.error_message}")
                    return None
            return None
        except Exception as e:
            logger.error(f"Failed to get {type_name}: {e}")
            import traceback
            traceback.print_exc()
            return None

    def get_sources(self) -> Optional[list]:
        """Get list of available source plugins (IEyeSource)."""
        from .messages import ResourceCode
        return self._get_plugins_by_type(ResourceCode.AVAILABLE_SOURCES, "sources")

    def get_stages(self) -> Optional[list]:
        """Get list of available stage plugins (IEyeStage)."""
        from .messages import ResourceCode
        return self._get_plugins_by_type(ResourceCode.AVAILABLE_STAGES, "stages")

    def get_sinks(self) -> Optional[list]:
        """Get list of available sink plugins (IEyeSink)."""
        from .messages import ResourceCode
        return self._get_plugins_by_type(ResourceCode.AVAILABLE_SINKS, "sinks")

    def get_tasks(self) -> Optional[list]:
        """Get list of available task plugins (IRender)."""
        from .messages import ResourceCode
        return self._get_plugins_by_type(ResourceCode.AVAILABLE_TASKS, "tasks")

    def get_monitors(self) -> Optional[list]:
        """
        Get list of available monitors from server.

        Returns:
            List of MonitorInfo objects, or None if request failed
        """
        try:
            from .messages import ResourceRequest, ResourceCode, Response, MonitorInfo

            request = ResourceRequest(resource_code=ResourceCode.AVAILABLE_MONITORS)
            response_data = self.send_request(request)

            if response_data:
                response = deserialize_message(response_data, Response)
                if response.success and response.payload:
                    monitors = msgspec.json.decode(response.payload, type=List[MonitorInfo])

                    logger.info(f"Received {len(monitors)} monitors from server")
                    return monitors
                else:
                    logger.error(f"Server returned error: {response.error_message}")
                    return None
            return None
        except Exception as e:
            logger.error(f"Failed to get monitors: {e}")
            import traceback
            traceback.print_exc()
            return None


    def send_pipeline_config(
        self,
        source: str,
        stages: list[str] | None = None,
    ) -> bool:
        """
        Send pipeline configuration to runtime.

        Args:
            source: Name of the source plugin
            stages: List of stage plugin names (optional)

        Returns:
            True if config was sent and accepted successfully
        """
        try:
            from .messages import PipelineConfigRequest, Response

            request = PipelineConfigRequest(
                pipeline_source=source,
                pipeline_stages=stages or []
            )
            response_data = self.send_request(request)
            if response_data:
                response = deserialize_message(response_data, Response)
                if response.success:
                    logger.info("Pipeline config sent successfully")
                    return True
                else:
                    logger.error(f"Server rejected pipeline config: {response.error_message}")
                    return False
            return False
        except Exception as e:
            logger.error(f"Failed to send pipeline config: {e}")
            return False

    def send_protocol(self, protocol) -> bool:
        """
        Send protocol to reyer_rt for execution.

        Args:
            protocol: ProtocolRequest message object

        Returns:
            True if protocol was sent and accepted successfully, False otherwise
        """
        try:
            from .messages import Response

            response_data = self.send_request(protocol)

            if response_data:
                response = deserialize_message(response_data, Response)
                if response.success:
                    logger.info(f"Protocol '{protocol.name}' sent successfully")
                    return True
                else:
                    logger.error(f"Server rejected protocol: {response.error_message}")
                    return False
            else:
                logger.error("No response received from server")
                return False
        except Exception as e:
            logger.error(f"Failed to send protocol: {e}")
            import traceback
            traceback.print_exc()
            return False

    def send_command(self, command: int, origin: str = "client", destination: str = "graphics") -> bool:
        """
        Send a command to the graphics manager.

        Args:
            command: Command enum value (Command.START, Command.STOP, etc.)
            origin: Origin identifier (default: "client")
            destination: Destination identifier (default: "graphics")

        Returns:
            True if command was sent and accepted successfully, False otherwise
        """
        try:
            from .messages import CommandRequest, Response

            cmd_request = CommandRequest(
                command=command,
                origin=origin,
                destination=destination
            )
            response_data = self.send_request(cmd_request)

            if response_data:
                response = deserialize_message(response_data, Response)
                if response.success:
                    logger.info(f"Command {command} sent successfully")
                    return True
                else:
                    logger.error(f"Server rejected command: {response.error_message}")
                    return False
            else:
                logger.error("No response received from server")
                return False
        except Exception as e:
            logger.error(f"Failed to send command: {e}")
            import traceback
            traceback.print_exc()
            return False

    def get_runtime_state(self) -> Optional[int]:
        """
        Get current runtime state from server.

        Returns:
            RuntimeState enum value, or None if request failed
        """
        try:
            from .messages import ResourceRequest, ResourceCode, Response, RuntimeState

            request = ResourceRequest(resource_code=ResourceCode.RUNTIME_STATE)
            response_data = self.send_request(request)

            if response_data:
                response = deserialize_message(response_data, Response)
                if response.success and response.payload:
                    return RuntimeState(int(response.payload))
            return None
        except Exception as e:
            logger.error(f"Failed to get runtime state: {e}")
            return None

    def send_graphics_settings(self, settings) -> bool:
        """
        Send graphics settings to runtime.

        Args:
            settings: GraphicsSettingsRequest message object

        Returns:
            True if settings were sent and accepted successfully, False otherwise
        """
        try:
            from .messages import Response

            response_data = self.send_request(settings)
            if response_data:
                response = deserialize_message(response_data, Response)
                if response.success:
                    logger.info("Graphics settings sent successfully")
                    return True
                else:
                    logger.error(f"Server rejected: {response.error_message}")
                    return False
            return False
        except Exception as e:
            logger.error(f"Failed to send graphics settings: {e}")
            return False

    def subscribe(self, callback: Optional[Callable[[bytes], None]] = None) -> bool:
        """
        Subscribe to published messages from reyer_rt.

        Args:
            callback: Optional callback function to execute when messages arrive.
                     Called with message bytes as argument.

        Returns:
            True if subscription successful, False otherwise
        """
        with self._lock:
            try:
                if self._sub_socket is None:
                    self._sub_socket = pynng.Sub0()
                    self._sub_socket.recv_timeout = self.config.receive_timeout_ms
                    self._sub_socket.dial(self.config.pub_socket_addr)

                    # Subscribe to all messages
                    self._sub_socket.subscribe(b"")
                    logger.info(f"Subscribed to messages from {self.config.pub_socket_addr}")

                if callback:
                    # Store callback
                    topic = "default"
                    if topic not in self._subscription_callbacks:
                        self._subscription_callbacks[topic] = []
                    self._subscription_callbacks[topic].append(callback)

                # Start subscription receive thread if not already running
                if not self._running:
                    self._running = True
                    self._subscription_thread = threading.Thread(
                        target=self._subscription_loop,
                        daemon=True
                    )
                    self._subscription_thread.start()
                    logger.debug("Started subscription receive thread")

                return True
            except Exception as e:
                logger.error(f"Failed to subscribe: {e}")
                return False

    def unsubscribe(self, callback: Optional[Callable] = None) -> None:
        """
        Unsubscribe from published messages.

        Args:
            callback: If provided, remove specific callback. Otherwise remove all.
        """
        with self._lock:
            topic = "default"
            if topic in self._subscription_callbacks:
                if callback:
                    try:
                        self._subscription_callbacks[topic].remove(callback)
                    except ValueError:
                        pass
                else:
                    self._subscription_callbacks[topic].clear()

    def subscribe_to_topic(self, topic: int, callback: Callable) -> bool:
        """
        Subscribe to a specific broadcast topic.

        Args:
            topic: BroadcastTopic value (PROTOCOL, LOG, etc.)
            callback: Callback function to handle messages for this topic.
                     For PROTOCOL topic, receives ProtocolEventMessage.

        Returns:
            True if subscription successful
        """
        with self._lock:
            # Initialize subscription if not already done
            if self._sub_socket is None:
                if not self.subscribe():
                    return False

            # Store callback under topic-specific key
            topic_key = f"topic_{topic}"
            if topic_key not in self._subscription_callbacks:
                self._subscription_callbacks[topic_key] = []
            self._subscription_callbacks[topic_key].append(callback)

            logger.info(f"Subscribed callback to topic {topic}")
            return True

    def _subscription_loop(self) -> None:
        """Internal thread loop for receiving published messages."""
        while self._running:
            try:
                if self._sub_socket:
                    message_data = self._sub_socket.recv()

                    # Parse BroadcastMessage
                    from .messages import BroadcastMessage, BroadcastTopic, ProtocolEventMessage

                    try:
                        broadcast_msg = msgspec.json.decode(message_data, type=BroadcastMessage)

                        # Route to topic-specific callbacks
                        topic_key = f"topic_{broadcast_msg.topic}"
                        callbacks = self._subscription_callbacks.get(topic_key, [])

                        for callback in callbacks:
                            try:
                                # Parse payload based on topic
                                if broadcast_msg.topic == BroadcastTopic.PROTOCOL:
                                    event = msgspec.json.decode(broadcast_msg.payload, type=ProtocolEventMessage)
                                    callback(event)
                                # Add other topic handlers as needed
                            except Exception as e:
                                logger.error(f"Error in topic callback: {e}")

                        # Also call legacy "default" callbacks with raw data for backward compatibility
                        default_callbacks = self._subscription_callbacks.get("default", [])
                        for callback in default_callbacks:
                            try:
                                callback(message_data)
                            except Exception as e:
                                logger.error(f"Error in default subscription callback: {e}")

                    except Exception as e:
                        logger.error(f"Error parsing broadcast message: {e}")

            except pynng.Timeout:
                continue
            except pynng.Closed:
                logger.info("Subscription socket closed, stopping loop")
                break
            except Exception as e:
                if not self._running:
                    break
                logger.error(f"Error in subscription loop: {e}")
                break

    def __enter__(self) -> ReyerClient:
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.disconnect()
