"""Page 4+: Task configuration with list/detail view."""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QListWidget,
    QSplitter, QStackedWidget
)
from PySide6.QtCore import Qt

from ..schema_ui import PluginConfigWidget


class TaskConfigurationPage(QWidget):
    """Single page for configuring all tasks with list/detail view."""

    def __init__(self, tasks: list[tuple[str, str]], parent=None):
        """
        Initialize task configuration page.

        Args:
            tasks: List of tuples (plugin_name, schema)
            parent: Parent widget
        """
        super().__init__(parent)
        self.tasks = tasks
        self.task_widgets = {}  # Map task index to PluginConfigWidget
        self._init_ui()

    def _init_ui(self):
        """Initialize the UI."""
        layout = QVBoxLayout(self)

        # Page title
        title = QLabel("<h2>Task Configuration</h2>")
        title.setTextFormat(Qt.RichText)
        layout.addWidget(title)

        # Description
        description = QLabel(
            "Configure each task by selecting it from the list. "
            "All tasks must be configured before proceeding."
        )
        description.setWordWrap(True)
        layout.addWidget(description)

        # Create splitter for left and right panels
        splitter = QSplitter(Qt.Horizontal)

        # Left panel - Task list
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.setContentsMargins(0, 0, 0, 0)

        left_label = QLabel("<b>Tasks</b>")
        left_label.setTextFormat(Qt.RichText)
        left_layout.addWidget(left_label)

        self.task_list = QListWidget()
        self.task_list.currentRowChanged.connect(self._on_task_selected)

        # Populate task list
        for i, (plugin_name, schema) in enumerate(self.tasks):
            self.task_list.addItem(f"Task {i + 1}: {plugin_name}")

        left_layout.addWidget(self.task_list)
        splitter.addWidget(left_panel)

        # Right panel - Task configuration
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)

        config_label = QLabel("<b>Configuration</b>")
        config_label.setTextFormat(Qt.RichText)
        right_layout.addWidget(config_label)

        # Stacked widget to hold different task config UIs
        self.config_stack = QStackedWidget()

        # Default "no selection" widget
        no_selection = QLabel("Select a task to configure")
        no_selection.setAlignment(Qt.AlignCenter)
        no_selection.setStyleSheet("color: gray; font-size: 14pt;")
        self.config_stack.addWidget(no_selection)

        # Create config widgets for each task
        for i, (plugin_name, schema) in enumerate(self.tasks):
            try:
                # Create a simplified config widget without buttons
                widget = PluginConfigWidget(plugin_name, schema)
                # Remove the apply/reset buttons (they're at indices in the layout)
                # We'll just use the schema widget directly
                self.task_widgets[i] = widget
                self.config_stack.addWidget(widget)
            except Exception as e:
                import logging
                logger = logging.getLogger(__name__)
                logger.error(f"Error creating config widget for {plugin_name}: {e}")
                error_label = QLabel(f"Error loading configuration for {plugin_name}: {e}")
                error_label.setStyleSheet("color: red;")
                error_label.setWordWrap(True)
                self.config_stack.addWidget(error_label)

        right_layout.addWidget(self.config_stack, 1)
        splitter.addWidget(right_panel)

        # Set initial splitter sizes (30% list, 70% config)
        splitter.setSizes([250, 550])

        layout.addWidget(splitter, 1)

        # Select first task by default
        if len(self.tasks) > 0:
            self.task_list.setCurrentRow(0)

    def _on_task_selected(self, index: int):
        """Handle task selection from list."""
        if 0 <= index < len(self.tasks):
            # Switch to the corresponding config widget (index + 1 because of "no selection" widget)
            self.config_stack.setCurrentIndex(index + 1)

    def get_task_configurations(self) -> list[dict]:
        """
        Get configurations for all tasks.

        Returns:
            List of configuration dictionaries (one per task)
        """
        configurations = []
        for i in range(len(self.tasks)):
            if i in self.task_widgets:
                config = self.task_widgets[i].get_configuration()
                configurations.append(config)
            else:
                # Task widget failed to create, use empty config
                configurations.append({})
        return configurations

    def is_valid(self) -> tuple[bool, str]:
        """
        Validate all task configurations.

        Returns:
            Tuple of (is_valid, error_message)
        """
        # All tasks are valid by default (schemas provide defaults)
        # Could add custom validation here if needed
        return True, ""
