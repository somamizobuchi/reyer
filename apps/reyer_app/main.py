import shutil
import sys
import logging
import msgspec
from pathlib import Path
from datetime import datetime
from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QLabel,
    QTextEdit,
    QTableWidget,
    QTableWidgetItem,
    QFileDialog,
    QMessageBox,
    QTabWidget,
    QHeaderView,
    QAbstractItemView,
    QStatusBar,
    QFrame,
)
from PySide6.QtCore import Qt, QTimer, QSize, QObject, Signal
from PySide6.QtGui import QIcon

from src.client import ReyerClient, ClientConfig
from src.protocol_builder import ProtocolBuilderDialog
from src.messages import (
    Command,
    ProtocolRequest,
    BroadcastTopic,
    ProtocolEvent,
    ProtocolEventMessage,
    RuntimeState,
)
from src.protocol_storage import ProtocolStorage

# Configure logging
logging.basicConfig(
    level=logging.DEBUG, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


class ProtocolLabelWidget(QFrame):
    """Custom widget for displaying protocol name and current task in stacked layout."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameStyle(QFrame.NoFrame)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 0, 8, 0)
        layout.setSpacing(2)

        # Protocol name label (top, bold, larger)
        self.protocol_name_label = QLabel("")
        self.protocol_name_label.setStyleSheet(
            "font-weight: bold; font-size: 24px; color: #fff;"
        )
        layout.addWidget(self.protocol_name_label)

        # Task label (bottom, lighter, smaller)
        self.task_label = QLabel("")
        self.task_label.setStyleSheet("font-size: 14px; color: #666;")
        layout.addWidget(self.task_label)

    def set_protocol_name(self, name: str):
        """Set the protocol name."""
        self.protocol_name_label.setText(name)

    def set_task_label(self, task_text: str):
        """Set the task label text."""
        self.task_label.setText(task_text)

    def set_text(self, protocol_name: str, task_text: str = ""):
        """Set both labels at once."""
        self.set_protocol_name(protocol_name)
        self.set_task_label(task_text)


class ConnectionSignals(QObject):
    """Emits signals for pipe connection events."""

    connected = Signal()
    disconnected = Signal()
    protocol_completed = Signal()


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
        self.current_protocol: ProtocolRequest | None = None
        self.current_task_index: int = 0
        self.total_tasks: int = 0
        self.current_data_file: str | None = None

        # Graphics initialization state
        self.graphics_initialized = False
        self.runtime_state = RuntimeState.DEFAULT

        self.init_ui()
        self.init_client()
        # Auto-connect on startup
        QTimer.singleShot(100, self.auto_connect)

    def init_ui(self):
        """Initialize the user interface."""
        self.setWindowTitle("Reyer Client")
        self.setMinimumSize(900, 500)

        # Central widget and layout
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        main_layout = QVBoxLayout(central_widget)
        main_layout.setSpacing(0)
        main_layout.setContentsMargins(0, 0, 0, 0)

        # Top toolbar
        toolbar_layout = QHBoxLayout()

        # Control button stylesheet
        control_btn_style = """
            QPushButton {
                background-color: #f0f0f0;
                border: 1px solid #ccc;
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

        button_size = QSize(54, 54)
        # Start button (larger with green highlight)
        self.start_btn = QPushButton()
        self.start_btn.setIcon(QIcon(str(self.assets_dir / "play.svg")))
        self.start_btn.setIconSize(QSize(28, 28))
        self.start_btn.setToolTip("Start")
        self.start_btn.clicked.connect(self.send_start_command)
        self.start_btn.setEnabled(False)
        self.start_btn.setFixedSize(button_size)
        self.start_btn.setStyleSheet(
            """
            QPushButton {
                background-color: #4CAF50;
                border: none;
            }
            QPushButton:hover { background-color: #45a049; }
            QPushButton:pressed { background-color: #3d8b40; }
            QPushButton:disabled { background-color: #ccc; }
        """
        )
        toolbar_layout.addWidget(self.start_btn)

        # Next button
        self.next_btn = QPushButton()
        self.next_btn.setIcon(QIcon(str(self.assets_dir / "arrow-right.svg")))
        self.next_btn.setIconSize(QSize(28, 28))
        self.next_btn.setToolTip("Next")
        self.next_btn.clicked.connect(self.send_next_command)
        self.next_btn.setEnabled(False)
        self.next_btn.setFixedSize(button_size)
        self.next_btn.setStyleSheet(control_btn_style)
        toolbar_layout.addWidget(self.next_btn)

        # Stop button
        self.stop_btn = QPushButton()
        self.stop_btn.setIcon(QIcon(str(self.assets_dir / "square.svg")))
        self.stop_btn.setIconSize(QSize(28, 28))
        self.stop_btn.setToolTip("Stop")
        self.stop_btn.clicked.connect(self.send_stop_command)
        self.stop_btn.setEnabled(False)
        self.stop_btn.setFixedSize(button_size)
        self.stop_btn.setStyleSheet(control_btn_style)
        toolbar_layout.addWidget(self.stop_btn)

        # Current protocol label (left side) - stacked protocol name and task
        self.current_protocol_label = ProtocolLabelWidget()
        toolbar_layout.addWidget(self.current_protocol_label)

        toolbar_layout.addStretch()

        # New Protocol button (Create - file-plus icon)
        self.new_protocol_btn = QPushButton()
        self.new_protocol_btn.setIcon(QIcon(str(self.assets_dir / "file-plus.svg")))
        self.new_protocol_btn.setIconSize(QSize(28, 28))
        self.new_protocol_btn.setToolTip("Create")
        self.new_protocol_btn.clicked.connect(self.open_protocol_builder)
        self.new_protocol_btn.setEnabled(False)
        self.new_protocol_btn.setFixedSize(button_size)
        self.new_protocol_btn.setStyleSheet(control_btn_style)

        # Load Protocol button (Load - file-up icon)
        self.load_protocol_btn = QPushButton()
        self.load_protocol_btn.setIcon(QIcon(str(self.assets_dir / "file-up.svg")))
        self.load_protocol_btn.setIconSize(QSize(28, 28))
        self.load_protocol_btn.setToolTip("Load")
        self.load_protocol_btn.clicked.connect(self.load_and_send_protocol)
        self.load_protocol_btn.setEnabled(False)
        self.load_protocol_btn.setFixedSize(button_size)
        self.load_protocol_btn.setStyleSheet(control_btn_style)

        toolbar_layout.addWidget(self.new_protocol_btn)
        toolbar_layout.addWidget(self.load_protocol_btn)

        # Exit button with power-off icon
        self.exit_btn = QPushButton()
        self.exit_btn.setIcon(QIcon(str(self.assets_dir / "power-off.svg")))
        self.exit_btn.setIconSize(QSize(28, 28))
        self.exit_btn.setToolTip("Exit")
        self.exit_btn.clicked.connect(self.send_exit_command)
        self.exit_btn.setEnabled(False)
        self.exit_btn.setFixedSize(button_size)
        self.exit_btn.setStyleSheet(
            """
            QPushButton {
                background-color: #f0f0f0;
                border: 1px solid #ccc;
            }
            QPushButton:hover { background-color: #e0e0e0; }
            QPushButton:pressed { background-color: #d0d0d0; }
            QPushButton:disabled { background-color: #f5f5f5; color: #ccc; }
        """
        )
        toolbar_layout.addWidget(self.exit_btn)

        main_layout.addLayout(toolbar_layout)

        # Tabbed bottom panel: Log + History
        self.bottom_tabs = QTabWidget()
        main_layout.addWidget(self.bottom_tabs, 1)

        # Log tab
        self.log_output = QTextEdit()
        self.log_output.setReadOnly(True)
        self.bottom_tabs.addTab(self.log_output, "Log")

        # History tab
        history_widget = QWidget()
        history_layout = QVBoxLayout(history_widget)
        history_layout.setContentsMargins(0, 0, 0, 0)

        self.history_table = QTableWidget(0, 4)
        self.history_table.setHorizontalHeaderLabels(
            ["Participant", "Protocol", "UUID", "Time"]
        )
        self.history_table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.history_table.setSelectionMode(QAbstractItemView.SingleSelection)
        self.history_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.history_table.horizontalHeader().setStretchLastSection(True)
        self.history_table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.Stretch
        )
        self.history_table.horizontalHeader().setSectionResizeMode(
            1, QHeaderView.Stretch
        )
        self.history_table.horizontalHeader().setSectionResizeMode(
            2, QHeaderView.ResizeToContents
        )
        self.history_table.horizontalHeader().setSectionResizeMode(
            3, QHeaderView.ResizeToContents
        )
        self.history_table.verticalHeader().setVisible(False)
        self.history_table.itemSelectionChanged.connect(
            self._on_protocol_selection_changed
        )
        history_layout.addWidget(self.history_table)

        # History action buttons
        history_btn_layout = QHBoxLayout()
        history_btn_layout.addStretch()

        self.save_protocol_btn = QPushButton("Save Protocol...")
        self.save_protocol_btn.setToolTip("Save selected protocol to file")
        self.save_protocol_btn.clicked.connect(self.save_selected_protocol)
        self.save_protocol_btn.setEnabled(False)
        history_btn_layout.addWidget(self.save_protocol_btn)

        self.resend_protocol_btn = QPushButton("Resend")
        self.resend_protocol_btn.setToolTip("Resend selected protocol")
        self.resend_protocol_btn.clicked.connect(self.resend_protocol_from_history)
        self.resend_protocol_btn.setEnabled(False)
        history_btn_layout.addWidget(self.resend_protocol_btn)

        self.save_data_btn = QPushButton("Save Data...")
        self.save_data_btn.setToolTip("Save HDF5 data file from last completed run")
        self.save_data_btn.clicked.connect(self.save_data_file)
        self.save_data_btn.setEnabled(False)
        history_btn_layout.addWidget(self.save_data_btn)

        history_btn_layout.addStretch()
        history_layout.addLayout(history_btn_layout)

        self.bottom_tabs.addTab(history_widget, "History")

        # Bottom status bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_indicator = QLabel("\u25cf Disconnected")
        self.status_indicator.setStyleSheet("color: red;")
        self.status_bar.addWidget(self.status_indicator)

    def init_client(self):
        """Initialize the Reyer client."""
        config = ClientConfig()
        self.client = ReyerClient(config)

        # Register callbacks with lambdas that emit signals
        self.client.register_on_connected(
            lambda: self.connection_signals.connected.emit()
        )
        self.client.register_on_disconnected(
            lambda: self.connection_signals.disconnected.emit()
        )

        # Connect signals to UI update methods
        self.connection_signals.connected.connect(self.on_pipe_connected)
        self.connection_signals.disconnected.connect(self.on_pipe_disconnected)
        self.connection_signals.protocol_completed.connect(self.save_data_file)

        # Subscribe to broadcast events once
        self.client.subscribe_to_topic(
            BroadcastTopic.PROTOCOL, self.handle_protocol_event
        )

        self.log("ReyerClient initialized")

    def auto_connect(self):
        """Automatically connect to server on startup."""
        self.log("Auto-connecting to Reyer RT server...")
        self.client.connect()

    def on_pipe_connected(self):
        """Handle pipe connected event from NNG."""
        self.status_indicator.setText("\u25cf Connected")
        self.status_indicator.setStyleSheet("color: green;")

        # Query runtime state
        self.runtime_state = self.client.get_runtime_state()

        if self.runtime_state == RuntimeState.DEFAULT:
            self.log("Runtime in DEFAULT state - initializing graphics...")
            self._initialize_graphics()
        elif self.runtime_state == RuntimeState.STANDBY:
            self.graphics_initialized = True
            self.log("Runtime ready (graphics already initialized)")
            self._enable_protocol_controls()

        self.log("Pipe connected to Reyer RT server")

    def on_pipe_disconnected(self):
        """Handle pipe disconnected event from NNG."""
        self.status_indicator.setText("\u25cf Disconnected")
        self.status_indicator.setStyleSheet("color: red;")
        self.exit_btn.setEnabled(False)
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(False)
        self.next_btn.setEnabled(False)

        self.new_protocol_btn.setEnabled(False)
        self.load_protocol_btn.setEnabled(False)
        self.resend_protocol_btn.setEnabled(False)
        self.save_data_btn.setEnabled(False)
        self.log("Pipe disconnected from Reyer RT server")

    def _initialize_graphics(self):
        """Show launcher dialog (graphics + pipeline) and initialize runtime."""
        from src.launcher_dialog import LauncherDialog

        monitors = self.client.get_monitors()
        if not monitors:
            QMessageBox.critical(
                self,
                "Error",
                "Failed to fetch monitor information.\n"
                "Please check the connection and try again.",
            )
            return

        sources = self.client.get_sources() or []
        stages = self.client.get_stages() or []
        calibrations = self.client.get_calibrations() or []
        filters = self.client.get_filters() or []

        dialog = LauncherDialog(
            self.client, monitors, sources, stages, calibrations, filters, self
        )
        settings = dialog.get_settings()

        if settings:
            self.graphics_initialized = True
            self.log("Runtime initialized successfully")
            self._enable_protocol_controls()
        else:
            self.log("Runtime initialization incomplete")
            self.client.disconnect()

    def _enable_protocol_controls(self):
        """Enable protocol-related controls after graphics initialization."""
        self.new_protocol_btn.setEnabled(True)
        self.load_protocol_btn.setEnabled(True)
        self.exit_btn.setEnabled(True)

        has_selection = bool(self.history_table.selectedItems())
        self.resend_protocol_btn.setEnabled(has_selection)

    def open_protocol_builder(self):
        """Open protocol builder dialog."""
        if not self.client.is_connected():
            self.log("Not connected to server")
            return

        if not self.graphics_initialized:
            QMessageBox.warning(
                self,
                "Graphics Not Initialized",
                "Graphics settings must be initialized before creating protocols.",
            )
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
            else:
                self.log(f"Warning: Protocol send returned False - check server logs")
        else:
            self.log("Protocol creation cancelled")

    def load_and_send_protocol(self):
        """Load a protocol from file and send it to the server."""
        if not self.client.is_connected():
            self.log("Cannot load protocol: not connected")
            return

        # Open file dialog
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Load Protocol", "", "JSON Files (*.json);;All Files (*)"
        )

        if not file_path:
            return  # User cancelled

        try:
            # Read and deserialize protocol
            with open(file_path, "rb") as f:
                json_data = f.read()

            protocol = msgspec.json.decode(json_data, type=ProtocolRequest)

            # Send protocol to server
            self.log(f"Sending protocol '{protocol.name}' to server...")
            success = self.client.send_protocol(protocol)
            logger.info(f"send_protocol returned: {success}")

            if success:
                self.log(f"Protocol '{protocol.name}' sent successfully")
            else:
                self.log(f"Warning: Protocol send returned False - check server logs")

        except Exception as e:
            self.log(f"Error loading protocol: {e}")
            QMessageBox.critical(
                self, "Load Error", f"Failed to load protocol:\n{str(e)}"
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
        if event_msg.event == ProtocolEvent.GRAPHICS_READY:
            self.graphics_initialized = True
            self.runtime_state = RuntimeState.STANDBY
            self.log("Graphics initialized and ready")
            self._enable_protocol_controls()

        elif event_msg.event == ProtocolEvent.PROTOCOL_LOADED:
            self.current_protocol_name = event_msg.protocol_name
            self.current_protocol = ProtocolRequest(
                name=event_msg.protocol_name,
                participant_id=event_msg.participant_id,
                notes=event_msg.notes,
                tasks=event_msg.tasks,
            )
            self.total_tasks = len(event_msg.tasks)
            self.current_protocol_label.set_text(self.current_protocol_name, "(Loaded)")
            self.log(f"Protocol '{self.current_protocol_name}' loaded")
            self.update_control_buttons(ProtocolEvent.PROTOCOL_NEW, 0, self.total_tasks)

        elif event_msg.event == ProtocolEvent.PROTOCOL_NEW:
            self.current_protocol_uuid = event_msg.protocol_uuid
            self.current_data_file = event_msg.file_path
            if self.current_protocol:
                self.current_protocol.protocol_uuid = event_msg.protocol_uuid
            self.save_data_btn.setEnabled(False)
            self.current_protocol_label.set_text(
                self.current_protocol_name, "(Running)"
            )
            self.log(
                f"Protocol run started (UUID: {event_msg.protocol_uuid}, file: {event_msg.file_path})"
            )
            self._add_to_history(self.current_protocol)

        elif event_msg.event == ProtocolEvent.TASK_START:
            self.current_task_index = event_msg.data

            # Get task name from protocol
            task_name = "Unknown"
            if self.current_protocol and self.current_task_index < len(
                self.current_protocol.tasks
            ):
                task_name = self.current_protocol.tasks[self.current_task_index].name

            self.current_protocol_label.set_text(
                self.current_protocol_name or "Unknown",
                f"Task {self.current_task_index + 1}/{self.total_tasks}: {task_name}",
            )
            self.log(
                f"Task {self.current_task_index + 1}/{self.total_tasks} started: {task_name}"
            )
            self.update_control_buttons(
                event_msg.event, self.current_task_index, self.total_tasks
            )

        elif event_msg.event == ProtocolEvent.TASK_END:
            # Check if this was the last task
            if self.current_task_index >= self.total_tasks - 1:
                self.current_protocol_label.set_text(
                    self.current_protocol_name or "Unknown", "(Completed)"
                )
                self.log(f"Protocol '{self.current_protocol_name}' completed")
                if self.current_data_file:
                    self.save_data_btn.setEnabled(True)
                    self.connection_signals.protocol_completed.emit()
            else:
                # Get task name for log
                task_name = "Unknown"
                if self.current_protocol and self.current_task_index < len(
                    self.current_protocol.tasks
                ):
                    task_name = self.current_protocol.tasks[
                        self.current_task_index
                    ].name
                self.log(f"Task {self.current_task_index + 1} ended: {task_name}")
            self.update_control_buttons(
                event_msg.event, self.current_task_index, self.total_tasks
            )

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

        elif event == ProtocolEvent.TASK_START:
            # Task is running - enable based on position
            self.start_btn.setEnabled(False)  # Can't start when already running
            self.stop_btn.setEnabled(True)

            # NEXT only if not on last task
            is_last_task = task_index >= total_tasks - 1
            self.next_btn.setEnabled(not is_last_task)

        elif event == ProtocolEvent.TASK_END:
            # Task ended (mid-protocol stop or natural completion)
            # Protocol is still loaded — enable START for re-run
            self.start_btn.setEnabled(True)
            self.stop_btn.setEnabled(False)
            self.next_btn.setEnabled(False)

    def closeEvent(self, event):
        """Handle window close event."""
        if self.client and self.client.is_connected():
            self.client.disconnect()
        event.accept()

    # Protocol Management Methods

    def _add_to_history(self, protocol: ProtocolRequest):
        """Add a protocol run to the history list."""
        uuid_short = (protocol.protocol_uuid or "")[:8]
        history_entry = {
            "protocol": protocol,
            "timestamp": datetime.now(),
            "name": protocol.name,
            "participant_id": protocol.participant_id,
            "uuid_short": uuid_short,
            "data_file": self.current_data_file,
        }
        self.protocol_history.insert(0, history_entry)
        self._refresh_protocol_history()

    def _refresh_protocol_history(self):
        """Refresh the protocol history table."""
        self.history_table.setRowCount(0)

        for entry in self.protocol_history:
            row = self.history_table.rowCount()
            self.history_table.insertRow(row)

            participant_item = QTableWidgetItem(entry["participant_id"])
            participant_item.setData(Qt.UserRole, entry)
            self.history_table.setItem(row, 0, participant_item)
            self.history_table.setItem(row, 1, QTableWidgetItem(entry["name"]))
            self.history_table.setItem(row, 2, QTableWidgetItem(entry["uuid_short"]))
            self.history_table.setItem(
                row, 3, QTableWidgetItem(entry["timestamp"].strftime("%H:%M:%S"))
            )

    def _get_selected_history_entry(self):
        """Get the history entry dict from the currently selected table row."""
        selected = self.history_table.selectedItems()
        if not selected:
            return None
        row = selected[0].row()
        item = self.history_table.item(row, 0)
        return item.data(Qt.UserRole) if item else None

    def _on_protocol_selection_changed(self):
        """Handle protocol selection change in table."""
        has_selection = self.history_table.currentRow() >= 0

        self.save_protocol_btn.setEnabled(has_selection)
        self.resend_protocol_btn.setEnabled(
            has_selection and self.client.is_connected()
        )

    def save_selected_protocol(self):
        """Save the selected protocol to user-specified location via file dialog."""
        entry = self._get_selected_history_entry()
        if not entry:
            self.log("No protocol selected")
            return

        protocol = entry["protocol"]

        # Suggest a filename based on protocol name
        suggested_name = "".join(
            c if c.isalnum() or c in ("-", "_") else "_" for c in protocol.name
        )
        suggested_name = f"{suggested_name}.json"

        # Open save file dialog
        file_path, _ = QFileDialog.getSaveFileName(
            self, "Save Protocol", suggested_name, "JSON Files (*.json);;All Files (*)"
        )

        if not file_path:
            return  # User cancelled

        filepath = Path(file_path)

        # Ensure .json extension
        if filepath.suffix.lower() != ".json":
            filepath = filepath.with_suffix(".json")

        try:
            # Serialize and save protocol
            json_data = msgspec.json.encode(protocol)

            with open(filepath, "wb") as f:
                f.write(json_data)

            self.log(f"Protocol saved to {filepath}")
            QMessageBox.information(
                self, "Save Successful", f"Protocol saved to:\n{filepath}"
            )

        except Exception as e:
            QMessageBox.critical(
                self, "Save Error", f"Failed to save protocol:\n{str(e)}"
            )
            self.log(f"Error saving protocol: {e}")

    def save_data_file(self):
        """Copy the HDF5 data file from /tmp/ to a user-chosen location."""
        if not self.current_data_file:
            self.log("No data file available")
            return

        src = Path(self.current_data_file)
        if not src.exists():
            QMessageBox.warning(self, "File Not Found", f"Data file not found:\n{src}")
            self.log(f"Data file not found: {src}")
            return

        # Suggest filename based on protocol info
        name = self.current_protocol_name or "protocol"
        safe_name = "".join(c if c.isalnum() or c in ("-", "_") else "_" for c in name)
        pid = ""
        if self.current_protocol:
            pid = self.current_protocol.participant_id
            pid = "".join(c if c.isalnum() or c in ("-", "_") else "_" for c in pid)
        uuid_short = (self.current_protocol_uuid or "")[:8]
        suggested = (
            f"{safe_name}_{pid}_{uuid_short}.h5"
            if pid
            else f"{safe_name}_{uuid_short}.h5"
        )

        file_path, _ = QFileDialog.getSaveFileName(
            self, "Save Data File", suggested, "HDF5 Files (*.h5);;All Files (*)"
        )
        if not file_path:
            return

        dest = Path(file_path)
        if dest.suffix.lower() != ".h5":
            dest = dest.with_suffix(".h5")

        try:
            shutil.copy2(src, dest)
            self.log(f"Data file saved to {dest}")
            QMessageBox.information(
                self, "Save Successful", f"Data file saved to:\n{dest}"
            )
        except Exception as e:
            QMessageBox.critical(
                self, "Save Error", f"Failed to save data file:\n{str(e)}"
            )
            self.log(f"Error saving data file: {e}")

    def resend_protocol_from_history(self):
        """Resend the selected protocol from history."""
        if not self.client.is_connected():
            self.log("Cannot send protocol: not connected")
            return

        entry = self._get_selected_history_entry()
        if not entry:
            self.log("No protocol selected")
            return

        protocol = entry["protocol"]

        # Send protocol to server
        self.log(f"Resending protocol '{protocol.name}' to server...")
        success = self.client.send_protocol(protocol)
        logger.info(f"send_protocol returned: {success}")

        if success:
            self.log(f"Protocol '{protocol.name}' resent successfully")
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
