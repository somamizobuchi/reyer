import sys
import logging
import msgspec
from pathlib import Path
from datetime import datetime
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QTextEdit, QListWidget, QListWidgetItem,
    QFileDialog, QMessageBox
)
from PySide6.QtCore import Qt, QTimer, QSize, QObject, Signal
from PySide6.QtGui import QIcon

from src.client import ReyerClient, ClientConfig
from src.protocol_builder import ProtocolBuilderDialog
from src.messages import Command, Protocol, BroadcastTopic, ProtocolEvent, ProtocolEventMessage
from src.protocol_storage import ProtocolStorage

# Configure logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class ConnectionSignals(QObject):
    """Emits signals for pipe connection events."""
    connected = Signal()
    disconnected = Signal()


class ReyerMainWindow(QMainWindow):
    """Main window for the Reyer PySide6 application."""

    def __init__(self):
        super().__init__()
        self.client: ReyerClient = None
        self.assets_dir = Path(__file__).parent / "assets" / "icons"
        self.connection_signals = ConnectionSignals()
        self.protocol_storage = ProtocolStorage()
        self.protocol_history: list[dict] = []  # History of sent protocols

        # Protocol state tracking
        self.current_protocol_uuid: str | None = None
        self.current_protocol_name: str | None = None
        self.current_protocol: Protocol | None = None
        self.current_task_index: int = 0
        self.total_tasks: int = 0

        self.init_ui()
        self.init_client()
        # Auto-connect on startup
        QTimer.singleShot(100, self.auto_connect)

    def init_ui(self):
        """Initialize the user interface."""
        self.setWindowTitle("Reyer Client - Protocol Builder")
        self.setMinimumSize(900, 500)

        # Central widget and layout
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        # Main horizontal layout: sidebar + content
        main_horizontal_layout = QHBoxLayout(central_widget)

        # Create sidebar
        self._create_sidebar(main_horizontal_layout)

        # Create main content area
        main_layout = QVBoxLayout()
        main_layout.setSpacing(6)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_horizontal_layout.addLayout(main_layout, 1)  # Give main content stretch factor

        # Status bar at top
        status_layout = QHBoxLayout()

        # Status icon (circle-check or circle-x)
        self.status_icon = QLabel()
        self.status_icon.setFixedSize(24, 24)
        # Set initial disconnected icon
        icon = QIcon(str(self.assets_dir / "circle-x.svg"))
        pixmap = icon.pixmap(QSize(24, 24))
        self.status_icon.setPixmap(pixmap)
        status_layout.addWidget(self.status_icon)

        # Status text
        self.status_label = QLabel("Disconnected")
        self.status_label.setStyleSheet("font-weight: bold; padding: 10px; color: red;")
        status_layout.addWidget(self.status_label)

        status_layout.addStretch()

        # Exit button with power-off icon
        self.exit_btn = QPushButton()
        self.exit_btn.setIcon(QIcon(str(self.assets_dir / "power-off.svg")))
        self.exit_btn.setIconSize(QSize(24, 24))
        self.exit_btn.setToolTip("Exit")
        self.exit_btn.clicked.connect(self.send_exit_command)
        self.exit_btn.setEnabled(False)
        self.exit_btn.setFixedSize(44, 44)
        self.exit_btn.setStyleSheet("""
            QPushButton {
                background-color: #f0f0f0;
                border-radius: 6px;
                border: none;
            }
            QPushButton:hover {
                background-color: #e0e0e0;
            }
            QPushButton:pressed {
                background-color: #d0d0d0;
            }
            QPushButton:disabled {
                background-color: #f5f5f5;
                color: #ccc;
            }
        """)
        status_layout.addWidget(self.exit_btn)

        main_layout.addLayout(status_layout)

        # Protocol buttons (centered above controls)
        protocol_buttons_layout = QHBoxLayout()
        protocol_buttons_layout.addStretch()

        # New Protocol button
        self.new_protocol_btn = QPushButton("New Protocol")
        self.new_protocol_btn.clicked.connect(self.open_protocol_builder)
        self.new_protocol_btn.setEnabled(False)
        self.new_protocol_btn.setMaximumWidth(150)
        button_style = """
            QPushButton {
                background-color: #f0f0f0;
                border-radius: 6px;
                border: none;
                padding: 8px 16px;
                font-weight: bold;
                color: #000;
            }
            QPushButton:hover {
                background-color: #e0e0e0;
            }
            QPushButton:pressed {
                background-color: #d0d0d0;
            }
            QPushButton:disabled {
                background-color: #f5f5f5;
                color: #ccc;
            }
        """
        self.new_protocol_btn.setStyleSheet(button_style)
        protocol_buttons_layout.addWidget(self.new_protocol_btn)

        # Load Protocol button
        self.load_protocol_btn = QPushButton("Load Protocol")
        self.load_protocol_btn.clicked.connect(self.load_and_send_protocol)
        self.load_protocol_btn.setEnabled(False)
        self.load_protocol_btn.setMaximumWidth(150)
        self.load_protocol_btn.setStyleSheet(button_style)
        protocol_buttons_layout.addWidget(self.load_protocol_btn)

        protocol_buttons_layout.addStretch()
        main_layout.addLayout(protocol_buttons_layout)

        # Command buttons (media control panel)
        command_layout = QHBoxLayout()
        command_layout.addStretch()

        # Control button stylesheet
        control_btn_style = """
            QPushButton {
                background-color: #f0f0f0;
                border-radius: 6px;
                border: none;
            }
            QPushButton:hover {
                background-color: #e0e0e0;
            }
            QPushButton:pressed {
                background-color: #d0d0d0;
            }
            QPushButton:disabled {
                background-color: #f5f5f5;
                color: #ccc;
            }
        """

        # Start button style (larger with highlight)
        start_btn_style = """
            QPushButton {
                background-color: #4CAF50;
                border-radius: 26px;
                border: none;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
            QPushButton:pressed {
                background-color: #3d8b40;
            }
            QPushButton:disabled {
                background-color: #ccc;
            }
        """

        # Start button with play icon (main control)
        self.start_btn = QPushButton()
        self.start_btn.setIcon(QIcon(str(self.assets_dir / "play.svg")))
        self.start_btn.setIconSize(QSize(28, 28))
        self.start_btn.setToolTip("Start")
        self.start_btn.clicked.connect(self.send_start_command)
        self.start_btn.setEnabled(False)
        self.start_btn.setFixedSize(52, 52)
        self.start_btn.setStyleSheet(start_btn_style)
        command_layout.addWidget(self.start_btn)

        # Previous button with arrow-left icon
        self.previous_btn = QPushButton()
        self.previous_btn.setIcon(QIcon(str(self.assets_dir / "arrow-left.svg")))
        self.previous_btn.setIconSize(QSize(24, 24))
        self.previous_btn.setToolTip("Previous")
        self.previous_btn.clicked.connect(self.send_previous_command)
        self.previous_btn.setEnabled(False)
        self.previous_btn.setFixedSize(44, 44)
        self.previous_btn.setStyleSheet(control_btn_style)
        command_layout.addWidget(self.previous_btn)

        # Next button with arrow-right icon
        self.next_btn = QPushButton()
        self.next_btn.setIcon(QIcon(str(self.assets_dir / "arrow-right.svg")))
        self.next_btn.setIconSize(QSize(24, 24))
        self.next_btn.setToolTip("Next")
        self.next_btn.clicked.connect(self.send_next_command)
        self.next_btn.setEnabled(False)
        self.next_btn.setFixedSize(44, 44)
        self.next_btn.setStyleSheet(control_btn_style)
        command_layout.addWidget(self.next_btn)

        # Stop button with square icon
        self.stop_btn = QPushButton()
        self.stop_btn.setIcon(QIcon(str(self.assets_dir / "square.svg")))
        self.stop_btn.setIconSize(QSize(24, 24))
        self.stop_btn.setToolTip("Stop")
        self.stop_btn.clicked.connect(self.send_stop_command)
        self.stop_btn.setEnabled(False)
        self.stop_btn.setFixedSize(44, 44)
        self.stop_btn.setStyleSheet(control_btn_style)
        command_layout.addWidget(self.stop_btn)

        # Restart button with rotate-ccw icon
        self.restart_btn = QPushButton()
        self.restart_btn.setIcon(QIcon(str(self.assets_dir / "rotate-ccw.svg")))
        self.restart_btn.setIconSize(QSize(24, 24))
        self.restart_btn.setToolTip("Restart")
        self.restart_btn.clicked.connect(self.send_restart_command)
        self.restart_btn.setEnabled(False)
        self.restart_btn.setFixedSize(44, 44)
        self.restart_btn.setStyleSheet(control_btn_style)
        command_layout.addWidget(self.restart_btn)

        command_layout.addStretch()
        main_layout.addLayout(command_layout)

        # Current Protocol Display
        protocol_display_layout = QHBoxLayout()
        protocol_display_layout.addStretch()
        protocol_label = QLabel("<b>Current Protocol:</b>")
        protocol_label.setTextFormat(Qt.RichText)
        protocol_display_layout.addWidget(protocol_label)
        self.current_protocol_label = QLabel("None")
        self.current_protocol_label.setStyleSheet("font-style: italic;")
        protocol_display_layout.addWidget(self.current_protocol_label)
        protocol_display_layout.addStretch()
        main_layout.addLayout(protocol_display_layout)

        main_layout.addStretch()

        # Log output at bottom
        log_label = QLabel("<b>Log Output</b>")
        log_label.setTextFormat(Qt.RichText)
        main_layout.addWidget(log_label)

        self.log_output = QTextEdit()
        self.log_output.setReadOnly(True)
        self.log_output.setMaximumHeight(150)
        main_layout.addWidget(self.log_output)

    def _create_sidebar(self, parent_layout):
        """Create the sidebar with protocol history and management buttons."""
        sidebar_widget = QWidget()
        sidebar_widget.setMaximumWidth(250)
        sidebar_widget.setMinimumWidth(200)
        sidebar_layout = QVBoxLayout(sidebar_widget)

        # Sidebar title
        title_label = QLabel("<b>Protocol History</b>")
        title_label.setTextFormat(Qt.RichText)
        title_label.setAlignment(Qt.AlignCenter)
        sidebar_layout.addWidget(title_label)

        # Protocol list widget
        self.protocol_list_widget = QListWidget()
        self.protocol_list_widget.setSelectionMode(QListWidget.SingleSelection)
        self.protocol_list_widget.itemSelectionChanged.connect(self._on_protocol_selection_changed)
        sidebar_layout.addWidget(self.protocol_list_widget, 1)  # Stretch factor

        # Button layout
        button_layout = QVBoxLayout()

        # Save button (full width)
        self.save_protocol_btn = QPushButton("Save As...")
        self.save_protocol_btn.setToolTip("Save selected protocol to user-specified location")
        self.save_protocol_btn.clicked.connect(self.save_selected_protocol)
        self.save_protocol_btn.setEnabled(False)  # Disabled until protocol selected
        button_layout.addWidget(self.save_protocol_btn)

        # Resend button (full width)
        self.resend_protocol_btn = QPushButton("Resend")
        self.resend_protocol_btn.setToolTip("Resend selected protocol from history")
        self.resend_protocol_btn.clicked.connect(self.resend_protocol_from_history)
        self.resend_protocol_btn.setEnabled(False)  # Disabled until connected and protocol selected
        button_layout.addWidget(self.resend_protocol_btn)

        sidebar_layout.addLayout(button_layout)

        parent_layout.addWidget(sidebar_widget)

    def init_client(self):
        """Initialize the Reyer client."""
        config = ClientConfig()
        self.client = ReyerClient(config)

        # Register callbacks with lambdas that emit signals
        self.client.register_on_connected(lambda: self.connection_signals.connected.emit())
        self.client.register_on_disconnected(lambda: self.connection_signals.disconnected.emit())

        # Connect signals to UI update methods
        self.connection_signals.connected.connect(self.on_pipe_connected)
        self.connection_signals.disconnected.connect(self.on_pipe_disconnected)

        self.log("ReyerClient initialized")

    def auto_connect(self):
        """Automatically connect to server on startup."""
        self.log("Auto-connecting to Reyer RT server...")
        self.client.connect()


    def on_pipe_connected(self):
        """Handle pipe connected event from NNG."""
        # Set connected icon and text
        icon = QIcon(str(self.assets_dir / "circle-check.svg"))
        pixmap = icon.pixmap(QSize(24, 24))
        self.status_icon.setPixmap(pixmap)

        self.status_label.setText("Connected")
        self.status_label.setStyleSheet("font-weight: bold; padding: 10px; color: green;")
        self.new_protocol_btn.setEnabled(True)
        self.load_protocol_btn.setEnabled(True)
        self.exit_btn.setEnabled(True)

        # Control buttons will be updated based on protocol state
        # Initially disabled until protocol is loaded
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(False)
        self.next_btn.setEnabled(False)
        self.previous_btn.setEnabled(False)
        self.restart_btn.setEnabled(False)

        # Enable resend button if protocol is selected
        has_selection = bool(self.protocol_list_widget.selectedItems())
        self.resend_protocol_btn.setEnabled(has_selection)

        # Subscribe to protocol events
        self.client.subscribe_to_topic(BroadcastTopic.PROTOCOL, self.handle_protocol_event)

        self.log("Pipe connected to Reyer RT server")

    def on_pipe_disconnected(self):
        """Handle pipe disconnected event from NNG."""
        # Set disconnected icon and text
        icon = QIcon(str(self.assets_dir / "circle-x.svg"))
        pixmap = icon.pixmap(QSize(24, 24))
        self.status_icon.setPixmap(pixmap)

        self.status_label.setText("Disconnected")
        self.status_label.setStyleSheet("font-weight: bold; padding: 10px; color: red;")
        self.exit_btn.setEnabled(False)
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(False)
        self.next_btn.setEnabled(False)
        self.previous_btn.setEnabled(False)
        self.restart_btn.setEnabled(False)
        self.new_protocol_btn.setEnabled(False)
        self.load_protocol_btn.setEnabled(False)
        self.resend_protocol_btn.setEnabled(False)
        self.log("Pipe disconnected from Reyer RT server")

    def open_protocol_builder(self):
        """Open protocol builder dialog."""
        if not self.client.is_connected():
            self.log("Not connected to server")
            return

        # Open protocol builder dialog (it will fetch its own data)
        self.log("Opening protocol builder...")
        dialog = ProtocolBuilderDialog(self.client, self)
        protocol = dialog.build_protocol()

        if protocol:
            # Send protocol to server
            self.log(f"Sending protocol '{protocol.name}' to server...")
            success = self.client.send_protocol(protocol)
            logger.info(f"send_protocol returned: {success}")

            if success:
                self.log(f"Protocol '{protocol.name}' sent successfully")
                # Add to history
                self._add_to_history(protocol)
            else:
                self.log(f"Warning: Protocol send returned False - check server logs")
                # Don't show error popup as protocol may have been received by server
        else:
            self.log("Protocol creation cancelled")

    def load_and_send_protocol(self):
        """Load a protocol from file and send it to the server."""
        if not self.client.is_connected():
            self.log("Cannot load protocol: not connected")
            return

        # Open file dialog
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Load Protocol",
            "",
            "JSON Files (*.json);;All Files (*)"
        )

        if not file_path:
            return  # User cancelled

        try:
            # Read and deserialize protocol
            with open(file_path, 'rb') as f:
                json_data = f.read()

            protocol = msgspec.json.decode(json_data, type=Protocol)

            # Send protocol to server
            self.log(f"Sending protocol '{protocol.name}' to server...")
            success = self.client.send_protocol(protocol)
            logger.info(f"send_protocol returned: {success}")

            if success:
                self.log(f"Protocol '{protocol.name}' sent successfully")
                # Add to history
                self._add_to_history(protocol)
            else:
                self.log(f"Warning: Protocol send returned False - check server logs")

        except Exception as e:
            self.log(f"Error loading protocol: {e}")
            QMessageBox.critical(
                self,
                "Load Error",
                f"Failed to load protocol:\n{str(e)}"
            )

    def send_start_command(self):
        """Send START command to graphics manager."""
        if not self.client.is_connected():
            self.log("Cannot send command: not connected")
            return

        success = self.client.send_command(Command.START)
        if success:
            self.log("START command sent successfully")
        else:
            self.log("Failed to send START command")

    def send_stop_command(self):
        """Send STOP command to graphics manager."""
        if not self.client.is_connected():
            self.log("Cannot send command: not connected")
            return

        success = self.client.send_command(Command.STOP)
        if success:
            self.log("STOP command sent successfully")
        else:
            self.log("Failed to send STOP command")

    def send_next_command(self):
        """Send NEXT command to graphics manager."""
        if not self.client.is_connected():
            self.log("Cannot send command: not connected")
            return

        success = self.client.send_command(Command.NEXT)
        if success:
            self.log("NEXT command sent successfully")
        else:
            self.log("Failed to send NEXT command")

    def send_previous_command(self):
        """Send PREVIOUS command to graphics manager."""
        if not self.client.is_connected():
            self.log("Cannot send command: not connected")
            return

        success = self.client.send_command(Command.PREVIOUS)
        if success:
            self.log("PREVIOUS command sent successfully")
        else:
            self.log("Failed to send PREVIOUS command")

    def send_restart_command(self):
        """Send RESTART command to graphics manager."""
        if not self.client.is_connected():
            self.log("Cannot send command: not connected")
            return

        success = self.client.send_command(Command.RESTART)
        if success:
            self.log("RESTART command sent successfully")
        else:
            self.log("Failed to send RESTART command")

    def send_exit_command(self):
        """Send EXIT command to graphics manager."""
        if not self.client.is_connected():
            self.log("Cannot send command: not connected")
            return

        success = self.client.send_command(Command.EXIT)
        if success:
            self.log("EXIT command sent successfully")
        else:
            self.log("Failed to send EXIT command")

    def log(self, message: str):
        """Add a message to the log output."""
        self.log_output.append(message)
        logger.info(message)

    def handle_protocol_event(self, event_msg: ProtocolEventMessage):
        """
        Handle protocol event messages from the server.

        Args:
            event_msg: ProtocolEventMessage containing event type and data
        """
        if event_msg.event == ProtocolEvent.PROTOCOL_NEW:
            self.current_protocol_uuid = event_msg.protocol_uuid
            # Match UUID to protocol in history to get name and full protocol
            for entry in self.protocol_history:
                if hasattr(entry['protocol'], 'protocol_uuid') and \
                   entry['protocol'].protocol_uuid == event_msg.protocol_uuid:
                    self.current_protocol = entry['protocol']
                    self.current_protocol_name = entry['protocol'].name
                    self.total_tasks = len(entry['protocol'].tasks)
                    break
            self.current_protocol_label.setText(f"{self.current_protocol_name} (Loaded)")
            self.log(f"Protocol '{self.current_protocol_name}' loaded")
            self.update_control_buttons(event_msg.event, 0, self.total_tasks)

        elif event_msg.event == ProtocolEvent.TASK_START:
            self.current_task_index = event_msg.data

            # Get task name from protocol
            task_name = "Unknown"
            if self.current_protocol and self.current_task_index < len(self.current_protocol.tasks):
                task_name = self.current_protocol.tasks[self.current_task_index].name

            self.current_protocol_label.setText(
                f"{self.current_protocol_name} - Task {self.current_task_index + 1}/{self.total_tasks}: {task_name}"
            )
            self.log(f"Task {self.current_task_index + 1}/{self.total_tasks} started: {task_name}")
            self.update_control_buttons(event_msg.event, self.current_task_index, self.total_tasks)

        elif event_msg.event == ProtocolEvent.TASK_END:
            # Check if this was the last task
            if self.current_task_index >= self.total_tasks - 1:
                self.current_protocol_label.setText(f"{self.current_protocol_name} (Completed)")
                self.log(f"Protocol '{self.current_protocol_name}' completed")
            else:
                # Get task name for log
                task_name = "Unknown"
                if self.current_protocol and self.current_task_index < len(self.current_protocol.tasks):
                    task_name = self.current_protocol.tasks[self.current_task_index].name
                self.log(f"Task {self.current_task_index + 1} ended: {task_name}")
            self.update_control_buttons(event_msg.event, self.current_task_index, self.total_tasks)

    def update_control_buttons(self, event: int, task_index: int, total_tasks: int):
        """
        Update control button states based on protocol event and task position.

        Args:
            event: ProtocolEvent value
            task_index: Current task index (0-based)
            total_tasks: Total number of tasks in protocol
        """
        if event == ProtocolEvent.PROTOCOL_NEW:
            # Protocol loaded but not started - only START enabled
            self.start_btn.setEnabled(True)
            self.stop_btn.setEnabled(False)
            self.next_btn.setEnabled(False)
            self.previous_btn.setEnabled(False)
            self.restart_btn.setEnabled(False)

        elif event == ProtocolEvent.TASK_START:
            # Task is running - enable based on position
            self.start_btn.setEnabled(False)  # Can't start when already running
            self.stop_btn.setEnabled(True)
            self.restart_btn.setEnabled(True)

            # PREVIOUS only if not on first task
            self.previous_btn.setEnabled(task_index > 0)

            # NEXT only if not on last task
            is_last_task = (task_index >= total_tasks - 1)
            self.next_btn.setEnabled(not is_last_task)

        elif event == ProtocolEvent.TASK_END:
            # Task ended - check if protocol finished
            if task_index >= total_tasks - 1:
                # Protocol finished - disable all controls
                self.start_btn.setEnabled(False)
                self.stop_btn.setEnabled(False)
                self.next_btn.setEnabled(False)
                self.previous_btn.setEnabled(False)
                self.restart_btn.setEnabled(False)
            else:
                # Task ended but protocol not finished - back to STANDBY state
                self.start_btn.setEnabled(True)
                self.stop_btn.setEnabled(False)
                self.next_btn.setEnabled(False)
                self.previous_btn.setEnabled(False)
                self.restart_btn.setEnabled(False)

    def closeEvent(self, event):
        """Handle window close event."""
        if self.client and self.client.is_connected():
            self.client.disconnect()
        event.accept()

    # Protocol Management Methods

    def _add_to_history(self, protocol: Protocol):
        """Add a protocol to the history list."""
        history_entry = {
            'protocol': protocol,
            'timestamp': datetime.now(),
            'name': protocol.name,
            'participant_id': protocol.participant_id
        }
        self.protocol_history.insert(0, history_entry)  # Add to beginning (most recent first)
        self._refresh_protocol_history()

    def _refresh_protocol_history(self):
        """Refresh the protocol history display."""
        self.protocol_list_widget.clear()

        for entry in self.protocol_history:
            # Display format: "name (participant_id) - timestamp"
            timestamp_str = entry['timestamp'].strftime("%H:%M:%S")
            display_text = f"{entry['name']} ({entry['participant_id']}) - {timestamp_str}"
            item = QListWidgetItem(display_text)
            # Store protocol object in item data for later retrieval
            item.setData(Qt.UserRole, entry['protocol'])
            self.protocol_list_widget.addItem(item)

    def _on_protocol_selection_changed(self):
        """Handle protocol selection change in list."""
        has_selection = bool(self.protocol_list_widget.selectedItems())

        # Enable save button if protocol selected
        self.save_protocol_btn.setEnabled(has_selection)

        # Only enable resend button if connected and protocol selected
        self.resend_protocol_btn.setEnabled(
            has_selection and self.client.is_connected()
        )

    def save_selected_protocol(self):
        """Save the selected protocol to user-specified location via file dialog."""
        selected_items = self.protocol_list_widget.selectedItems()
        if not selected_items:
            self.log("No protocol selected")
            return

        # Get protocol from selected item
        item = selected_items[0]
        protocol = item.data(Qt.UserRole)

        if not protocol:
            self.log("Failed to retrieve protocol from selection")
            return

        # Suggest a filename based on protocol name
        suggested_name = "".join(c if c.isalnum() or c in ('-', '_') else '_'
                                for c in protocol.name)
        suggested_name = f"{suggested_name}.json"

        # Open save file dialog
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save Protocol",
            suggested_name,
            "JSON Files (*.json);;All Files (*)"
        )

        if not file_path:
            return  # User cancelled

        filepath = Path(file_path)

        # Ensure .json extension
        if filepath.suffix.lower() != '.json':
            filepath = filepath.with_suffix('.json')

        try:
            # Serialize and save protocol
            json_data = msgspec.json.encode(protocol)

            with open(filepath, 'wb') as f:
                f.write(json_data)

            self.log(f"Protocol saved to {filepath}")
            QMessageBox.information(
                self,
                "Save Successful",
                f"Protocol saved to:\n{filepath}"
            )

        except Exception as e:
            QMessageBox.critical(
                self,
                "Save Error",
                f"Failed to save protocol:\n{str(e)}"
            )
            self.log(f"Error saving protocol: {e}")

    def resend_protocol_from_history(self):
        """Resend the selected protocol from history."""
        if not self.client.is_connected():
            self.log("Cannot send protocol: not connected")
            return

        selected_items = self.protocol_list_widget.selectedItems()
        if not selected_items:
            self.log("No protocol selected")
            return

        # Get protocol from selected item
        item = selected_items[0]
        protocol = item.data(Qt.UserRole)

        if not protocol:
            self.log("Failed to retrieve protocol from history")
            return

        # Send protocol to server
        self.log(f"Resending protocol '{protocol.name}' to server...")
        success = self.client.send_protocol(protocol)
        logger.info(f"send_protocol returned: {success}")

        if success:
            self.log(f"Protocol '{protocol.name}' resent successfully")
            # Add to history again
            self._add_to_history(protocol)
        else:
            self.log(f"Warning: Protocol send returned False - check server logs")


def main():
    """Main entry point for the application."""
    app = QApplication(sys.argv)

    # Create and show main window
    window = ReyerMainWindow()
    window.show()

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
