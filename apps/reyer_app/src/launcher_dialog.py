"""Launcher dialog for runtime initialization (graphics + pipeline)."""

import logging

from PySide6.QtWidgets import (
    QDialog,
    QVBoxLayout,
    QHBoxLayout,
    QStackedWidget,
    QPushButton,
    QLabel,
    QMessageBox,
)
from PySide6.QtCore import Qt

from .client import ReyerClient
from .messages import GraphicsSettingsRequest, MonitorInfo, PluginInfo
from .pages.graphics_settings_page import GraphicsSettingsPage
from .pages.pipeline_config_page import PipelineConfigPage

logger = logging.getLogger(__name__)


class LauncherDialog(QDialog):
    """Two-step launcher dialog: graphics settings then pipeline configuration.

    This dialog is required for runtime initialization. There is no cancel
    option — both steps must be completed for the runtime to reach a ready
    state.
    """

    PAGE_GRAPHICS = 0
    PAGE_PIPELINE = 1

    def __init__(
        self,
        client: ReyerClient,
        monitors: list[MonitorInfo],
        sources: list[PluginInfo],
        stages: list[PluginInfo],
        parent=None,
    ):
        super().__init__(parent)
        self.client = client
        self.monitors = monitors
        self.sources = sources
        self.stages = stages
        self.settings_result = None

        self._init_ui()
        self.setWindowTitle("Runtime Configuration")
        self.setMinimumSize(700, 550)

    def _init_ui(self):
        layout = QVBoxLayout(self)

        # Title
        title = QLabel("<h1>Runtime Configuration</h1>")
        title.setTextFormat(Qt.RichText)
        title.setAlignment(Qt.AlignCenter)
        layout.addWidget(title)

        # Page indicator
        self.page_indicator = QLabel()
        self.page_indicator.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.page_indicator)

        # Stacked widget
        self.stacked_widget = QStackedWidget()

        self.graphics_page = GraphicsSettingsPage(self.monitors)
        self.stacked_widget.addWidget(self.graphics_page)

        self.pipeline_page = PipelineConfigPage(
            self.sources, self.stages
        )
        self.stacked_widget.addWidget(self.pipeline_page)

        layout.addWidget(self.stacked_widget, 1)

        # Navigation buttons (no cancel)
        button_layout = QHBoxLayout()
        button_layout.addStretch()

        self.next_button = QPushButton("Next")
        self.next_button.clicked.connect(self._on_next)
        button_layout.addWidget(self.next_button)

        self.launch_button = QPushButton("Launch")
        self.launch_button.clicked.connect(self._on_launch)
        self.launch_button.setVisible(False)
        button_layout.addWidget(self.launch_button)

        layout.addLayout(button_layout)

        # Re-validate when page content changes
        self.pipeline_page.content_changed.connect(self._update_ui)

        self._update_ui()

    def _update_ui(self):
        current = self.stacked_widget.currentIndex()
        total = self.stacked_widget.count()
        self.page_indicator.setText(f"Step {current + 1} of {total}")

        is_graphics = current == self.PAGE_GRAPHICS
        self.next_button.setVisible(is_graphics)
        self.launch_button.setVisible(not is_graphics)

        # Validate current page
        widget = self.stacked_widget.widget(current)
        if hasattr(widget, "is_valid"):
            is_valid, error_msg = widget.is_valid()
            self.next_button.setEnabled(is_valid)
            self.launch_button.setEnabled(is_valid)
            if not is_valid and error_msg:
                self.page_indicator.setText(
                    f"Step {current + 1} of {total} — {error_msg}"
                )

    def _on_next(self):
        """Validate graphics settings and move to pipeline page."""
        is_valid, error_msg = self.graphics_page.is_valid()
        if not is_valid:
            QMessageBox.warning(self, "Validation Error", error_msg)
            return

        self.stacked_widget.setCurrentIndex(self.PAGE_PIPELINE)
        self._update_ui()

    def _on_launch(self):
        """Validate both pages, send graphics then pipeline config, and close."""
        is_valid, error_msg = self.pipeline_page.is_valid()
        if not is_valid:
            QMessageBox.warning(self, "Validation Error", error_msg)
            return

        settings = GraphicsSettingsRequest(
            graphics_settings=self.graphics_page.get_data(),
            view_distance_mm=self.graphics_page.get_view_distance(),
        )
        success = self.client.send_graphics_settings(settings)
        if not success:
            QMessageBox.critical(self, "Error", "Failed to apply graphics settings.")
            return

        source, stages = self.pipeline_page.get_data()
        success = self.client.send_pipeline_config(source, stages=stages)
        if not success:
            QMessageBox.critical(
                self, "Error", "Failed to apply pipeline configuration."
            )
            return

        self.settings_result = settings
        self.accept()

    def get_settings(self):
        """Show dialog and return settings if applied (both steps completed)."""
        result = self.exec()
        if result == QDialog.Accepted:
            return self.settings_result
        return None

    def closeEvent(self, event):
        """Prevent closing without completing the launcher."""
        event.ignore()
