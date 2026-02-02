"""Protocol builder dialog for creating experiment protocols."""

import json
import logging
import uuid
from typing import Optional

from PySide6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QStackedWidget,
    QPushButton, QLabel, QMessageBox, QWidget
)
from PySide6.QtCore import Qt

from .client import ReyerClient
from .messages import PluginInfo, MonitorInfo, ProtocolRequest, TaskInfo
from .pages import BasicInfoPage, TaskSelectionPage, TaskConfigurationPage

logger = logging.getLogger(__name__)


class ProtocolBuilderDialog(QDialog):
    """Wizard-style dialog for building experiment protocols."""

    # Page indices
    PAGE_BASIC_INFO = 0
    PAGE_TASK_SELECTION = 1
    PAGE_TASK_CONFIG_START = 2  # Dynamic task config pages start here

    def __init__(
        self,
        client: ReyerClient,
        parent=None
    ):
        """
        Initialize protocol builder dialog.

        Args:
            client: ReyerClient instance for communicating with reyer_rt
            parent: Parent widget
        """
        super().__init__(parent)
        self.client = client
        self.task_config_page = None  # Single task configuration page
        self.protocol_result = None

        # Fetch monitors and plugins from server
        self.monitors = []
        self.plugins = []
        self._fetch_resources()

        self._init_ui()
        self.setWindowTitle("Protocol Builder")
        self.setMinimumSize(800, 600)

    def _fetch_resources(self):
        """Fetch monitors and plugins from server."""
        logger.info("Fetching monitors and plugins from server...")

        # Fetch monitors
        monitors = self.client.get_monitors()
        if monitors:
            self.monitors = monitors
            logger.info(f"Fetched {len(monitors)} monitor(s)")
        else:
            logger.warning("Failed to fetch monitors or no monitors available")

        # Fetch plugins
        plugins = self.client.get_plugins()
        if plugins:
            self.plugins = plugins
            logger.info(f"Fetched {len(plugins)} plugin(s)")
        else:
            logger.warning("Failed to fetch plugins or no plugins available")

    def _init_ui(self):
        """Initialize the UI."""
        layout = QVBoxLayout(self)

        # Check if we have the required resources
        if not self.monitors or not self.plugins:
            error_msg = []
            if not self.monitors:
                error_msg.append("• No monitors available")
            if not self.plugins:
                error_msg.append("• No plugins available")

            error_label = QLabel(
                "<h2>Unable to Load Resources</h2>"
                "<p>The following resources could not be loaded from the server:</p>"
                f"<p>{'<br>'.join(error_msg)}</p>"
                "<p>Please ensure Reyer RT is running properly and try again.</p>"
            )
            error_label.setTextFormat(Qt.RichText)
            error_label.setWordWrap(True)
            error_label.setAlignment(Qt.AlignCenter)
            layout.addWidget(error_label)

            # Close button
            close_button = QPushButton("Close")
            close_button.clicked.connect(self.reject)
            layout.addWidget(close_button)
            return

        # Title
        title = QLabel("<h1>Protocol Builder</h1>")
        title.setTextFormat(Qt.RichText)
        title.setAlignment(Qt.AlignCenter)
        layout.addWidget(title)

        # Page indicator
        self.page_indicator = QLabel()
        self.page_indicator.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.page_indicator)

        # Stacked widget for pages
        self.stacked_widget = QStackedWidget()

        # Create base pages
        self.basic_info_page = BasicInfoPage()
        self.stacked_widget.addWidget(self.basic_info_page)

        self.task_selection_page = TaskSelectionPage(self.plugins)
        self.stacked_widget.addWidget(self.task_selection_page)

        layout.addWidget(self.stacked_widget, 1)

        # Connect page change signal
        self.stacked_widget.currentChanged.connect(self._update_navigation_buttons)

        # Connect content change signals from pages
        self.basic_info_page.content_changed.connect(self._update_navigation_buttons)
        self.task_selection_page.content_changed.connect(self._update_navigation_buttons)

        # Navigation buttons
        button_layout = QHBoxLayout()
        button_layout.addStretch()

        self.back_button = QPushButton("Back")
        self.back_button.clicked.connect(self._on_back)
        button_layout.addWidget(self.back_button)

        self.next_button = QPushButton("Next")
        self.next_button.clicked.connect(self._on_next)
        button_layout.addWidget(self.next_button)

        self.finish_button = QPushButton("Finish")
        self.finish_button.clicked.connect(self._on_finish)
        self.finish_button.setVisible(False)
        button_layout.addWidget(self.finish_button)

        self.cancel_button = QPushButton("Cancel")
        self.cancel_button.clicked.connect(self.reject)
        button_layout.addWidget(self.cancel_button)

        layout.addLayout(button_layout)

        # Update UI state
        self._update_navigation_buttons()

    def _update_navigation_buttons(self):
        """Update the state of navigation buttons."""
        current_index = self.stacked_widget.currentIndex()
        total_pages = self.stacked_widget.count()

        # Update page indicator
        self.page_indicator.setText(f"Page {current_index + 1} of {total_pages}")

        # Back button
        self.back_button.setEnabled(current_index > 0)

        # Next/Finish buttons
        # Special case: Task Selection page should always show Next (not Finish)
        # because we need to generate task config pages
        is_task_selection_page = current_index == self.PAGE_TASK_SELECTION
        is_last_page = current_index == total_pages - 1

        if is_task_selection_page:
            # Always show Next on task selection page
            self.next_button.setVisible(True)
            self.finish_button.setVisible(False)
        else:
            # Show Next/Finish based on whether we're on last page
            self.next_button.setVisible(not is_last_page)
            self.finish_button.setVisible(is_last_page)

        # Validate current page
        is_valid, error_msg = self._validate_current_page()
        self.next_button.setEnabled(is_valid)
        self.finish_button.setEnabled(is_valid)

        if not is_valid and error_msg:
            self.page_indicator.setText(
                f"Page {current_index + 1} of {total_pages} - {error_msg}"
            )

    def _validate_current_page(self) -> tuple[bool, str]:
        """
        Validate the current page.

        Returns:
            Tuple of (is_valid, error_message)
        """
        current_index = self.stacked_widget.currentIndex()
        current_widget = self.stacked_widget.widget(current_index)

        # Check if widget has is_valid method
        if hasattr(current_widget, 'is_valid'):
            return current_widget.is_valid()

        return True, ""

    def _on_back(self):
        """Handle back button click."""
        current_index = self.stacked_widget.currentIndex()
        if current_index > 0:
            self.stacked_widget.setCurrentIndex(current_index - 1)
            self._update_navigation_buttons()

    def _on_next(self):
        """Handle next button click."""
        current_index = self.stacked_widget.currentIndex()

        # Validate current page
        is_valid, error_msg = self._validate_current_page()
        if not is_valid:
            QMessageBox.warning(self, "Validation Error", error_msg)
            return

        # Special handling for task selection page
        if current_index == self.PAGE_TASK_SELECTION:
            self._generate_task_config_pages()

        # Move to next page
        if current_index < self.stacked_widget.count() - 1:
            self.stacked_widget.setCurrentIndex(current_index + 1)
            self._update_navigation_buttons()

    def _generate_task_config_pages(self):
        """Generate task configuration page based on selected tasks."""
        logger.info("_generate_task_config_pages called")

        # Remove existing task config page if it exists
        if self.task_config_page:
            self.stacked_widget.removeWidget(self.task_config_page)
            self.task_config_page.deleteLater()
            self.task_config_page = None

        # Get selected tasks
        tasks = self.task_selection_page.get_tasks()
        logger.info(f"Got {len(tasks)} tasks from task selection page")

        # Create a single config page for all tasks
        if len(tasks) > 0:
            logger.info(f"Creating task configuration page for {len(tasks)} tasks")
            self.task_config_page = TaskConfigurationPage(tasks, self)
            self.stacked_widget.addWidget(self.task_config_page)
            logger.info(f"Added task config page to stacked widget, total pages now: {self.stacked_widget.count()}")
        else:
            logger.warning("No tasks selected, skipping task config page creation")

    def _on_finish(self):
        """Handle finish button click."""
        # Validate current page
        is_valid, error_msg = self._validate_current_page()
        if not is_valid:
            QMessageBox.warning(self, "Validation Error", error_msg)
            return

        # Build protocol
        try:
            protocol = self._build_protocol()
            self.protocol_result = protocol
            self.accept()

        except Exception as e:
            logger.error(f"Error building protocol: {e}")
            import traceback
            traceback.print_exc()
            QMessageBox.critical(
                self,
                "Error",
                f"Failed to build protocol: {e}"
            )

    def _build_protocol(self) -> ProtocolRequest:
        """
        Build the protocol from all page data.

        Returns:
            ProtocolRequest message object
        """
        # Get basic info
        basic_info = self.basic_info_page.get_data()

        # Build tasks
        tasks = []

        if self.task_config_page:
            # Get task names from task selection page
            selected_tasks = self.task_selection_page.get_tasks()
            # Get configurations from task config page
            configurations = self.task_config_page.get_task_configurations()

            logger.info(f"Building protocol with {len(selected_tasks)} tasks")

            for i, ((plugin_name, schema), config) in enumerate(zip(selected_tasks, configurations)):
                logger.info(f"Adding task {i+1}: {plugin_name}")
                task_info = TaskInfo(
                    name=plugin_name,
                    configuration=json.dumps(config)
                )
                tasks.append(task_info)
        else:
            logger.warning("No task config page found")

        # Generate UUID for this protocol
        protocol_uuid = str(uuid.uuid4())
        logger.info(f"Generated protocol UUID: {protocol_uuid}")

        # Create protocol WITHOUT graphics settings
        protocol = ProtocolRequest(
            name=basic_info['name'],
            participant_id=basic_info['participant_id'],
            notes=basic_info['notes'],
            tasks=tasks,
            protocol_uuid=protocol_uuid
        )

        logger.info(f"Built protocol: {protocol.name} with {len(tasks)} tasks")
        return protocol

    def build_protocol(self) -> Optional[ProtocolRequest]:
        """
        Show dialog and return built protocol.

        Returns:
            ProtocolRequest object if dialog accepted, None if cancelled or resources unavailable
        """
        # Check if resources were successfully loaded
        if not self.monitors or not self.plugins:
            logger.error("Cannot build protocol: resources unavailable")
            result = self.exec()  # Show error dialog
            return None

        result = self.exec()

        if result == QDialog.Accepted:
            return self.protocol_result

        return None
