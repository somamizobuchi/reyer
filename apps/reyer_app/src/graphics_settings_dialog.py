"""Standalone graphics settings dialog for runtime initialization."""

from PySide6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QPushButton, QLabel, QMessageBox
)
from PySide6.QtCore import Qt

from .client import ReyerClient
from .messages import GraphicsSettingsRequest, MonitorInfo
from .pages.graphics_settings_page import GraphicsSettingsPage


class GraphicsSettingsDialog(QDialog):
    """Dialog for configuring graphics settings at runtime startup."""

    def __init__(self, client: ReyerClient, monitors: list[MonitorInfo], parent=None):
        super().__init__(parent)
        self.client = client
        self.monitors = monitors
        self.settings_result = None

        self._init_ui()
        self.setWindowTitle("Initialize Graphics Settings")
        self.setMinimumSize(600, 500)

    def _init_ui(self):
        layout = QVBoxLayout(self)

        # Title
        title = QLabel("<h1>Graphics Initialization</h1>")
        title.setTextFormat(Qt.RichText)
        title.setAlignment(Qt.AlignCenter)
        layout.addWidget(title)

        # Description
        desc = QLabel(
            "Configure graphics settings for this runtime session.\n"
            "These settings will be used for the entire lifetime of the runtime."
        )
        desc.setWordWrap(True)
        desc.setAlignment(Qt.AlignCenter)
        layout.addWidget(desc)

        # Reuse GraphicsSettingsPage
        self.graphics_page = GraphicsSettingsPage(self.monitors)
        layout.addWidget(self.graphics_page, 1)

        # Buttons
        button_layout = QHBoxLayout()
        button_layout.addStretch()

        self.apply_button = QPushButton("Apply")
        self.apply_button.clicked.connect(self._on_apply)
        button_layout.addWidget(self.apply_button)

        self.cancel_button = QPushButton("Cancel")
        self.cancel_button.clicked.connect(self.reject)
        button_layout.addWidget(self.cancel_button)

        layout.addLayout(button_layout)

    def _on_apply(self):
        is_valid, error_msg = self.graphics_page.is_valid()
        if not is_valid:
            QMessageBox.warning(self, "Validation Error", error_msg)
            return

        settings = GraphicsSettingsRequest(
            graphics_settings=self.graphics_page.get_data(),
            view_distance_mm=self.graphics_page.get_view_distance()
        )

        success = self.client.send_graphics_settings(settings)

        if success:
            self.settings_result = settings
            self.accept()
        else:
            QMessageBox.critical(
                self,
                "Error",
                "Failed to apply graphics settings."
            )

    def get_settings(self):
        """Show dialog and return settings if applied."""
        result = self.exec()
        if result == QDialog.Accepted:
            return self.settings_result
        return None
