"""Page 3: Task selection from available plugins."""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QListWidget, QPushButton,
    QLabel, QSplitter
)
from PySide6.QtCore import Qt, Signal

from ..messages import PluginInfo


class TaskSelectionPage(QWidget):
    """Page 3: Select plugins to add as tasks."""

    content_changed = Signal()

    def __init__(self, plugins: list[PluginInfo], parent=None):
        """
        Initialize task selection page.

        Args:
            plugins: List of available plugins from reyer_rt
            parent: Parent widget
        """
        super().__init__(parent)
        self.plugins = plugins
        self._plugin_map = {plugin.name: plugin for plugin in plugins}
        self._init_ui()

    def _init_ui(self):
        """Initialize the UI."""
        layout = QVBoxLayout(self)

        # Page title
        title = QLabel("<h2>Task Selection</h2>")
        title.setTextFormat(Qt.RichText)
        layout.addWidget(title)

        # Description
        description = QLabel(
            "Select plugins to add as tasks to your protocol. "
            "You can add the same plugin multiple times with different configurations."
        )
        description.setWordWrap(True)
        layout.addWidget(description)

        # Create splitter for left and right panels
        splitter = QSplitter(Qt.Horizontal)

        # Left panel - Available plugins
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.setContentsMargins(0, 0, 0, 0)

        left_label = QLabel("<b>Available Plugins</b>")
        left_label.setTextFormat(Qt.RichText)
        left_layout.addWidget(left_label)

        self.available_list = QListWidget()
        for plugin in self.plugins:
            self.available_list.addItem(plugin.name)
        left_layout.addWidget(self.available_list)

        splitter.addWidget(left_panel)

        # Center panel - Control buttons
        center_panel = QWidget()
        center_layout = QVBoxLayout(center_panel)
        center_layout.addStretch()

        self.add_button = QPushButton("Add Task >>")
        self.add_button.clicked.connect(self._on_add_task)
        center_layout.addWidget(self.add_button)

        self.remove_button = QPushButton("<< Remove Task")
        self.remove_button.clicked.connect(self._on_remove_task)
        center_layout.addWidget(self.remove_button)

        center_layout.addStretch()
        splitter.addWidget(center_panel)

        # Right panel - Selected tasks
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)

        right_label = QLabel("<b>Selected Tasks (in order)</b>")
        right_label.setTextFormat(Qt.RichText)
        right_layout.addWidget(right_label)

        self.selected_list = QListWidget()
        right_layout.addWidget(self.selected_list)

        # Move up/down buttons
        move_buttons_layout = QHBoxLayout()
        self.move_up_button = QPushButton("Move Up")
        self.move_up_button.clicked.connect(self._on_move_up)
        move_buttons_layout.addWidget(self.move_up_button)

        self.move_down_button = QPushButton("Move Down")
        self.move_down_button.clicked.connect(self._on_move_down)
        move_buttons_layout.addWidget(self.move_down_button)

        right_layout.addLayout(move_buttons_layout)
        splitter.addWidget(right_panel)

        # Set initial splitter sizes
        splitter.setSizes([300, 100, 300])

        layout.addWidget(splitter, 1)

    def _on_add_task(self):
        """Handle add task button click."""
        current_item = self.available_list.currentItem()
        if current_item:
            plugin_name = current_item.text()
            # Create numbered task name
            task_count = self.selected_list.count()
            task_label = f"Task {task_count + 1}: {plugin_name}"
            self.selected_list.addItem(task_label)
            self.content_changed.emit()

    def _on_remove_task(self):
        """Handle remove task button click."""
        current_row = self.selected_list.currentRow()
        if current_row >= 0:
            self.selected_list.takeItem(current_row)
            # Renumber remaining tasks
            self._renumber_tasks()
            self.content_changed.emit()

    def _on_move_up(self):
        """Move selected task up in the list."""
        current_row = self.selected_list.currentRow()
        if current_row > 0:
            item = self.selected_list.takeItem(current_row)
            self.selected_list.insertItem(current_row - 1, item)
            self.selected_list.setCurrentRow(current_row - 1)
            self._renumber_tasks()

    def _on_move_down(self):
        """Move selected task down in the list."""
        current_row = self.selected_list.currentRow()
        if 0 <= current_row < self.selected_list.count() - 1:
            item = self.selected_list.takeItem(current_row)
            self.selected_list.insertItem(current_row + 1, item)
            self.selected_list.setCurrentRow(current_row + 1)
            self._renumber_tasks()

    def _renumber_tasks(self):
        """Renumber all tasks in the selected list."""
        for i in range(self.selected_list.count()):
            item = self.selected_list.item(i)
            text = item.text()
            # Extract plugin name after ": "
            if ": " in text:
                plugin_name = text.split(": ", 1)[1]
            else:
                plugin_name = text
            item.setText(f"Task {i + 1}: {plugin_name}")

    def get_tasks(self) -> list[tuple[str, str, str]]:
        """
        Get the list of selected tasks.

        Returns:
            List of tuples (plugin_name, schema, default_configuration)
        """
        import logging
        logger = logging.getLogger(__name__)

        tasks = []
        logger.info(f"Getting tasks, selected_list count: {self.selected_list.count()}")

        for i in range(self.selected_list.count()):
            item = self.selected_list.item(i)
            text = item.text()
            logger.info(f"Task {i}: text='{text}'")

            # Extract plugin name after ": "
            if ": " in text:
                plugin_name = text.split(": ", 1)[1]
            else:
                plugin_name = text

            logger.info(f"Task {i}: extracted plugin_name='{plugin_name}'")
            logger.info(f"Available plugins in map: {list(self._plugin_map.keys())}")

            # Get schema from plugin map
            if plugin_name in self._plugin_map:
                plugin = self._plugin_map[plugin_name]
                tasks.append((plugin.name, plugin.configuration_schema, plugin.default_configuration))
                logger.info(f"Task {i}: Added to tasks list")
            else:
                logger.warning(f"Task {i}: Plugin '{plugin_name}' not found in plugin map")

        logger.info(f"Returning {len(tasks)} tasks")
        return tasks

    def is_valid(self) -> tuple[bool, str]:
        """
        Validate the page data.

        Returns:
            Tuple of (is_valid, error_message)
        """
        if self.selected_list.count() == 0:
            return False, "At least one task must be selected"

        return True, ""

    def set_tasks(self, task_names: list[str]):
        """
        Set the selected tasks from a list of plugin names.

        Args:
            task_names: List of plugin names
        """
        self.selected_list.clear()
        for i, plugin_name in enumerate(task_names):
            task_label = f"Task {i + 1}: {plugin_name}"
            self.selected_list.addItem(task_label)
