"""Page 2: Graphics settings configuration."""

import math
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QFormLayout, QComboBox, QCheckBox,
    QSpinBox, QLabel, QLineEdit, QHBoxLayout
)
from PySide6.QtCore import Qt

from ..messages import MonitorInfo, GraphicsSettings


class GraphicsSettingsPage(QWidget):
    """Page 2: Monitor selection and graphics settings."""

    def __init__(self, monitors: list[MonitorInfo], parent=None):
        """
        Initialize graphics settings page.

        Args:
            monitors: List of available monitors from reyer_rt
            parent: Parent widget
        """
        super().__init__(parent)
        self.monitors = monitors
        self._init_ui()

    def _init_ui(self):
        """Initialize the UI."""
        layout = QVBoxLayout(self)

        # Page title
        title = QLabel("<h2>Graphics Settings</h2>")
        title.setTextFormat(Qt.RichText)
        layout.addWidget(title)

        # Description
        description = QLabel(
            "Configure display and graphics settings for the experiment."
        )
        description.setWordWrap(True)
        layout.addWidget(description)

        # Form layout
        form_layout = QFormLayout()
        form_layout.setFieldGrowthPolicy(QFormLayout.ExpandingFieldsGrow)

        # Monitor selection
        self.monitor_combo = QComboBox()
        for i, monitor in enumerate(self.monitors):
            display_name = f"{monitor.name} ({monitor.width_px}x{monitor.height_px} @ {monitor.refresh_rate}Hz)"
            self.monitor_combo.addItem(display_name, userData=i)

        # Select first monitor by default
        if self.monitors:
            self.monitor_combo.setCurrentIndex(0)

        self.monitor_combo.currentIndexChanged.connect(self._on_monitor_changed)
        form_layout.addRow("Monitor", self.monitor_combo)

        # Monitor dimensions (physical size)
        self.dimensions_label = QLabel()
        self._update_dimensions_label()
        form_layout.addRow("Monitor Dimensions", self.dimensions_label)

        # Resolution (editable when not fullscreen)
        resolution_layout = QHBoxLayout()
        self.width_spinbox = QSpinBox()
        self.width_spinbox.setMinimum(640)
        self.width_spinbox.setMaximum(7680)  # 8K width
        self.width_spinbox.setValue(monitor.width_px)
        self.width_spinbox.setSuffix(" px")
        resolution_layout.addWidget(self.width_spinbox)

        resolution_layout.addWidget(QLabel("×"))

        self.height_spinbox = QSpinBox()
        self.height_spinbox.setMinimum(480)
        self.height_spinbox.setMaximum(4320)  # 8K height
        self.height_spinbox.setValue(monitor.height_px)
        self.height_spinbox.setSuffix(" px")
        resolution_layout.addWidget(self.height_spinbox)
        resolution_layout.addStretch()

        form_layout.addRow("Resolution", resolution_layout)

        # VSync
        self.vsync_checkbox = QCheckBox()
        self.vsync_checkbox.setChecked(True)  # Default to enabled
        form_layout.addRow("Enable VSync", self.vsync_checkbox)

        # Anti-aliasing
        self.anti_aliasing_checkbox = QCheckBox()
        self.anti_aliasing_checkbox.setChecked(False)
        form_layout.addRow("Enable Anti-Aliasing", self.anti_aliasing_checkbox)

        # Full screen
        self.fullscreen_checkbox = QCheckBox()
        self.fullscreen_checkbox.setChecked(True)  # Default to full screen
        self.fullscreen_checkbox.stateChanged.connect(self._on_fullscreen_changed)
        form_layout.addRow("Full Screen", self.fullscreen_checkbox)

        # Target FPS
        self.fps_spinbox = QSpinBox()
        self.fps_spinbox.setMinimum(30)
        self.fps_spinbox.setMaximum(540)
        self.fps_spinbox.setValue(monitor.refresh_rate)
        form_layout.addRow("Target FPS", self.fps_spinbox)

        # View distance with pixels per degree calculation
        view_distance_layout = QHBoxLayout()
        self.view_distance_spinbox = QSpinBox()
        self.view_distance_spinbox.setMinimum(100)  # Min 100mm (10cm)
        self.view_distance_spinbox.setMaximum(5000)  # Max 5000mm (5m)
        self.view_distance_spinbox.setValue(600)  # Default 600mm (60cm)
        self.view_distance_spinbox.setSuffix(" mm")
        self.view_distance_spinbox.valueChanged.connect(self._update_pixels_per_degree)
        view_distance_layout.addWidget(self.view_distance_spinbox)

        # Pixels per degree label
        self.pixels_per_degree_label = QLabel()
        self.pixels_per_degree_label.setStyleSheet("color: gray; font-style: italic;")
        view_distance_layout.addWidget(self.pixels_per_degree_label)
        view_distance_layout.addStretch()

        form_layout.addRow("View Distance", view_distance_layout)
        self._update_pixels_per_degree()

        # Initialize resolution input state based on fullscreen
        self._on_fullscreen_changed()

        layout.addLayout(form_layout)
        layout.addStretch()

    def _on_monitor_changed(self, index: int):
        """Handle monitor selection change."""
        self._update_dimensions_label()
        # Update FPS to match monitor refresh rate
        if 0 <= index < len(self.monitors):
            monitor = self.monitors[index]
            self.fps_spinbox.setValue(monitor.refresh_rate)
            # Update resolution to match monitor native resolution
            self.width_spinbox.setValue(monitor.width_px)
            self.height_spinbox.setValue(monitor.height_px)
        # Update pixels per degree when monitor changes
        self._update_pixels_per_degree()

    def _on_fullscreen_changed(self):
        """Handle fullscreen checkbox change."""
        is_fullscreen = self.fullscreen_checkbox.isChecked()
        # Disable resolution inputs when fullscreen is enabled
        self.width_spinbox.setEnabled(not is_fullscreen)
        self.height_spinbox.setEnabled(not is_fullscreen)

        # If switching to fullscreen, reset to monitor's native resolution
        if is_fullscreen:
            index = self.monitor_combo.currentIndex()
            if 0 <= index < len(self.monitors):
                monitor = self.monitors[index]
                self.width_spinbox.setValue(monitor.width_px)
                self.height_spinbox.setValue(monitor.height_px)

    def _update_dimensions_label(self):
        """Update the dimensions label to show physical monitor size."""
        index = self.monitor_combo.currentIndex()
        if 0 <= index < len(self.monitors):
            monitor = self.monitors[index]
            # Convert mm to inches for additional reference
            width_inches = monitor.width_mm / 25.4
            height_inches = monitor.height_mm / 25.4
            diagonal_inches = math.sqrt(width_inches**2 + height_inches**2)
            self.dimensions_label.setText(
                f"{monitor.width_mm} × {monitor.height_mm} mm "
                f"({width_inches:.1f}\" × {height_inches:.1f}\", {diagonal_inches:.1f}\" diagonal)"
            )
        else:
            self.dimensions_label.setText("No monitor selected")

    def _update_pixels_per_degree(self):
        """Calculate and update the pixels per degree label."""
        index = self.monitor_combo.currentIndex()
        if 0 <= index < len(self.monitors):
            monitor = self.monitors[index]
            view_distance_mm = self.view_distance_spinbox.value()

            # Calculate pixels per degree using the monitor width
            # Angular width = 2 * arctan(physical_width / (2 * viewing_distance))
            # Convert to degrees and then calculate pixels per degree
            angular_width_rad = 2 * math.atan(monitor.width_mm / (2 * view_distance_mm))
            angular_width_deg = math.degrees(angular_width_rad)
            pixels_per_degree = monitor.width_px / angular_width_deg

            self.pixels_per_degree_label.setText(f"({pixels_per_degree:.2f} px/deg)")
        else:
            self.pixels_per_degree_label.setText("")

    def get_data(self) -> GraphicsSettings:
        """
        Get the graphics settings.

        Returns:
            GraphicsSettings message object
        """
        monitor_index = self.monitor_combo.currentData()
        if monitor_index is None or monitor_index < 0 or monitor_index >= len(self.monitors):
            monitor_index = 0

        return GraphicsSettings(
            monitor_index=monitor_index,
            vsync=self.vsync_checkbox.isChecked(),
            anti_aliasing=self.anti_aliasing_checkbox.isChecked(),
            full_screen=self.fullscreen_checkbox.isChecked(),
            target_fps=self.fps_spinbox.value(),
            width=self.width_spinbox.value(),
            height=self.height_spinbox.value()
        )

    def get_view_distance(self) -> int:
        """
        Get the view distance in millimeters.

        Returns:
            View distance in millimeters
        """
        return self.view_distance_spinbox.value()

    def is_valid(self) -> tuple[bool, str]:
        """
        Validate the page data.

        Returns:
            Tuple of (is_valid, error_message)
        """
        # Always valid since we have defaults
        if not self.monitors:
            return False, "No monitors available"

        return True, ""

    def set_data(self, settings: GraphicsSettings, view_distance_mm: int = 600):
        """
        Set the page data from GraphicsSettings.

        Args:
            settings: GraphicsSettings object
            view_distance_mm: View distance in millimeters (default: 600mm)
        """
        # Set monitor selection
        if 0 <= settings.monitor_index < len(self.monitors):
            self.monitor_combo.setCurrentIndex(settings.monitor_index)

        # Set checkboxes
        self.vsync_checkbox.setChecked(settings.vsync)
        self.anti_aliasing_checkbox.setChecked(settings.anti_aliasing)
        self.fullscreen_checkbox.setChecked(settings.full_screen)

        # Set FPS
        self.fps_spinbox.setValue(settings.target_fps)

        # Set resolution
        self.width_spinbox.setValue(settings.width)
        self.height_spinbox.setValue(settings.height)

        # Set view distance
        self.view_distance_spinbox.setValue(view_distance_mm)
